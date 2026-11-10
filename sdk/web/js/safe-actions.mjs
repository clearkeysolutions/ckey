// CKey Safe Actions for the receiving app (spec/CKey-Safe-Actions.md).
//
// The SDK, not the app, decides whether a Safe Action runs:
//   - anything that is not a verified Safe Action for this app runs nothing;
//   - a verified same-device action runs its registered handler at once;
//   - a verified cross-device action runs only after the SDK's own confirmation,
//     which names the issuer and the requesting device (request.present()).
// The app registers actions and pins issuer keys; it never sees a way to run an
// action that skips these checks. Verification itself is the C core's.

export const SAME_DEVICE = 'same-device';
export const CROSS_DEVICE = 'cross-device';
const PREFIX = 'ckey-action:1:';
const MAX_LIFETIME_CAP = 3600;
const KEY_FILE_TTL_MS = 3600 * 1000;
const utf8 = new TextEncoder();
const utf8d = new TextDecoder('utf-8', { fatal: true });

const hexToBytes = (hex) => {
  if (typeof hex !== 'string' || !/^[0-9a-fA-F]{64}$/.test(hex)) throw new TypeError('a public key is 64 hexadecimal digits (32 bytes)');
  return Uint8Array.from(hex.match(/../g), (b) => parseInt(b, 16));
};
const keyBytes = (key) => {
  if (key instanceof Uint8Array) { if (key.length !== 32) throw new TypeError('a public key is 32 bytes'); return key; }
  return hexToBytes(key);
};
const shortText = (v, max = 120) => (typeof v === 'string' && v.length && v.length <= max ? v : null);

/** True when text is a Safe Action payload (says nothing about whether it is valid). */
export function isSafeAction(text) {
  return typeof text === 'string' && text.startsWith(PREFIX) && text.length > PREFIX.length;
}

function requestInfoUrl(template, issuer, requestId) {
  if (typeof template !== 'string') return null;
  let url;
  try { url = new URL(template.replace('{request_id}', encodeURIComponent(requestId))); } catch { return null; }
  // Request information must come over HTTPS from the issuer's own domain.
  if (url.protocol !== 'https:' || !(url.hostname === issuer || url.hostname.endsWith('.' + issuer))) return null;
  return url.href;
}

/** A verified cross-device Safe Action waiting for the person to confirm it. */
export class SafeActionRequest {
  #approve;
  #decided = false;
  constructor(details, approve) {
    Object.assign(this, details);
    Object.freeze(this.requestInfo);
    this.#approve = approve;
    Object.freeze(this);
  }

  /**
   * Shows the SDK's confirmation and resolves to { approved, result }. The
   * handler runs only if the person approves here; there is no other way to
   * run a cross-device action.
   */
  present({ document = globalThis.document } = {}) {
    if (this.#decided) return Promise.resolve({ approved: false, reason: 'already decided' });
    if (!document?.createElement) return Promise.resolve({ approved: false, reason: 'no document to show the confirmation in' });
    return new Promise((resolve) => {
      const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
      const dialog = el('dialog', 'ckey-safe-action');
      dialog.setAttribute('aria-labelledby', 'ckey-sa-title');
      const title = el('h2', 'ckey-sa-title', this.title);
      title.id = 'ckey-sa-title';
      const info = this.requestInfo;
      const facts = el('dl', 'ckey-sa-facts');
      for (const [k, v] of [['Requested by', this.issuer + ' (verified)'], ['Device', info.device], ['Location', info.location], ['Requested', info.requestedAt]]) {
        if (!v) continue;
        facts.append(el('dt', null, k), el('dd', null, v));
      }
      const note = el('p', 'ckey-sa-note', 'Only continue if this is the device and place you are using right now. Someone may be trying to use a code meant for their own session.');
      const approve = el('button', 'ckey-sa-approve', 'Continue');
      const decline = el('button', 'ckey-sa-decline', 'Cancel');
      approve.type = decline.type = 'button';
      const finish = async (ok) => {
        if (this.#decided) return;
        this.#decided = true;
        dialog.close?.();
        dialog.remove();
        if (!ok) { resolve({ approved: false }); return; }
        try { resolve({ approved: true, result: await this.#approve() }); }
        catch (error) { resolve({ approved: true, error }); }
      };
      approve.addEventListener('click', () => finish(true));
      decline.addEventListener('click', () => finish(false));
      dialog.addEventListener('cancel', (e) => { e.preventDefault(); finish(false); });
      const row = el('div', 'ckey-sa-buttons');
      row.append(decline, approve);
      dialog.append(title, facts, note, row);
      document.body.append(dialog);
      if (dialog.showModal) dialog.showModal(); else dialog.setAttribute('open', '');
      decline.focus?.();
    });
  }
}

export class SafeActions {
  #core;
  #appId;
  #keys = new Map(); // `${issuer} ${keyId}` -> { key: Uint8Array, requestInfo }
  #actions = new Map(); // action -> { issuers: Set, scope, handler, title }
  #seen = new Map(); // requestId -> expiresAtMs
  #keyFiles = new Map(); // issuer -> { at, file }
  #fetch;
  #now;
  #maxLifetime;
  #useKeyFiles;

  /**
   * @param core   { verify(payloadBytes, publicKey, nowSeconds, maxLifetime) -> fields, parse(payloadBytes) -> fields }
   * @param appId  this app's ID, e.g. 'com.viadere'
   */
  constructor(core, appId, { maxLifetime = 600, keyFiles = false, fetch = globalThis.fetch?.bind(globalThis), now = () => Date.now() } = {}) {
    if (!core?.verify || !core?.parse) throw new TypeError('SafeActions needs the CKey core');
    if (typeof appId !== 'string' || !/^[A-Za-z0-9._-]{1,255}$/.test(appId)) throw new TypeError('appId must be an app ID such as com.example.app');
    if (!Number.isInteger(maxLifetime) || maxLifetime < 1 || maxLifetime > MAX_LIFETIME_CAP) throw new RangeError('maxLifetime is 1 to 3600 seconds');
    this.#core = core;
    this.#appId = appId;
    this.#maxLifetime = maxLifetime;
    this.#useKeyFiles = Boolean(keyFiles);
    this.#fetch = fetch;
    this.#now = now;
  }

  /** Pins an issuer's public key (64 hex digits or 32 bytes). requestInfo is the issuer's request_info URL template. */
  trust(issuer, keyId, publicKey, { requestInfo } = {}) {
    if (typeof issuer !== 'string' || !/^[a-z0-9.-]+\.[a-z0-9-]+$/.test(issuer)) throw new TypeError('issuer is a lower-case domain name');
    if (typeof keyId !== 'string' || !/^[A-Za-z0-9._-]{1,32}$/.test(keyId)) throw new TypeError('keyId is 1 to 32 letters, digits, . _ or -');
    this.#keys.set(issuer + ' ' + keyId, { key: keyBytes(publicKey), requestInfo: requestInfo ?? null });
    return this;
  }

  /**
   * Registers an action this app accepts. scope is SAME_DEVICE (runs at once) or
   * CROSS_DEVICE (runs after the SDK's confirmation). The stricter of this and
   * the envelope's own scope applies.
   */
  register(action, { issuers, issuer, scope = SAME_DEVICE, title } = {}, handler) {
    if (typeof action !== 'string' || !/^[a-z0-9._-]{1,64}$/.test(action)) throw new TypeError('action is 1 to 64 of a-z 0-9 . _ -');
    if (scope !== SAME_DEVICE && scope !== CROSS_DEVICE) throw new TypeError('scope is SAME_DEVICE or CROSS_DEVICE');
    if (typeof handler !== 'function') throw new TypeError('a handler function is required');
    const list = issuers ?? (issuer ? [issuer] : []);
    if (!list.length) throw new TypeError('name the issuer or issuers allowed to request this action');
    this.#actions.set(action, { issuers: new Set(list), scope, handler, title: shortText(title, 80) });
    return this;
  }

  async #keyFor(issuer, keyId) {
    const pinned = this.#keys.get(issuer + ' ' + keyId);
    if (pinned) return pinned;
    if (!this.#useKeyFiles || !this.#fetch) return null;
    const file = await this.#keyFile(issuer);
    const entry = file?.keys?.find?.((k) => k && k.id === keyId && k.alg === 'Ed25519');
    if (!entry) return null;
    if (entry.not_after && Date.parse(entry.not_after) < this.#now()) return null;
    try { return { key: hexToBytes(entry.public_key), requestInfo: file.request_info ?? null }; } catch { return null; }
  }

  async #keyFile(issuer) {
    const cached = this.#keyFiles.get(issuer);
    if (cached && this.#now() - cached.at < KEY_FILE_TTL_MS) return cached.file;
    try {
      const response = await this.#fetch('https://' + issuer + '/.well-known/ckey-safe-actions.json', { headers: { Accept: 'application/json' }, redirect: 'error' });
      const file = response.ok ? await response.json() : null;
      const valid = file && file.issuer === issuer && Array.isArray(file.keys) ? file : null;
      this.#keyFiles.set(issuer, { at: this.#now(), file: valid });
      return valid;
    } catch {
      return null;
    }
  }

  async #requestInfo(template, issuer, requestId) {
    const url = requestInfoUrl(template, issuer, requestId);
    if (!url || !this.#fetch) return null;
    try {
      const response = await this.#fetch(url, { headers: { Accept: 'application/json' }, redirect: 'error' });
      if (!response.ok) return null;
      const info = await response.json();
      if (!info || info.request_id !== requestId || info.status !== 'pending') return null;
      return { device: shortText(info.device), location: shortText(info.location), requestedAt: shortText(info.requested_at, 40) };
    } catch {
      return null;
    }
  }

  /**
   * Handles a decoded payload. Resolves to one of:
   *   { kind: 'not-safe-action' }               follow the ordinary payload rules
   *   { kind: 'rejected', reason, action? }     claims to be a Safe Action but did not verify; run nothing
   *   { kind: 'ran', action, result }           a same-device action that ran
   *   { kind: 'confirm', request }              cross-device: call request.present()
   */
  async handle(text) {
    if (!isSafeAction(text)) return { kind: 'not-safe-action' };
    const bytes = utf8.encode(text);
    let claimed;
    try { claimed = this.#core.parse(bytes); }
    catch (error) { return { kind: 'rejected', reason: error.message }; }
    const reject = (reason) => ({ kind: 'rejected', reason, action: { issuer: claimed.issuer, app: claimed.app, action: claimed.action } });
    if (claimed.app !== this.#appId) return reject('this code is for another app (' + claimed.app + ')');
    const registered = this.#actions.get(claimed.action);
    if (!registered) return reject('this app does not accept the action ' + claimed.action);
    if (!registered.issuers.has(claimed.issuer)) return reject(claimed.issuer + ' may not request ' + claimed.action);
    const trusted = await this.#keyFor(claimed.issuer, claimed.key_id);
    if (!trusted) return reject('no trusted key ' + claimed.key_id + ' for ' + claimed.issuer);
    let verified;
    try { verified = this.#core.verify(bytes, trusted.key, Math.floor(this.#now() / 1000), this.#maxLifetime); }
    catch (error) { return reject(error.message); }
    const nowMs = this.#now();
    for (const [id, until] of this.#seen) if (until < nowMs) this.#seen.delete(id);
    if (this.#seen.has(verified.request_id)) return reject('this code has already been used');
    this.#seen.set(verified.request_id, verified.expires_at * 1000 + 120000);
    let paramsText = null;
    let paramsJSON = null;
    try { paramsText = verified.params.length ? utf8d.decode(verified.params) : ''; paramsJSON = paramsText ? JSON.parse(paramsText) : null; } catch {}
    const action = Object.freeze({
      issuer: verified.issuer, app: verified.app, action: verified.action, keyId: verified.key_id, requestId: verified.request_id,
      scope: verified.scope, issuedAt: verified.issued_at, expiresAt: verified.expires_at, params: verified.params, paramsText, paramsJSON
    });
    const scope = verified.scope === CROSS_DEVICE || registered.scope === CROSS_DEVICE ? CROSS_DEVICE : SAME_DEVICE;
    if (scope === SAME_DEVICE) {
      return { kind: 'ran', action, result: await registered.handler(action) };
    }
    const info = await this.#requestInfo(trusted.requestInfo, verified.issuer, verified.request_id);
    if (!info) return reject('the issuer did not confirm a pending request, so this cross-device action cannot run');
    const request = new SafeActionRequest({
      issuer: verified.issuer, action: verified.action, title: registered.title || 'Continue with ' + verified.issuer + '?', requestInfo: info, details: action
    }, () => registered.handler(action));
    return { kind: 'confirm', request };
  }
}
