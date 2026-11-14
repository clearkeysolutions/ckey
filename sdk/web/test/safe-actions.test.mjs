// The Safe Actions policy layer (js/safe-actions.mjs) against real Ed25519
// signatures. The core adapter here is a small reference verifier built on
// node:crypto (the wasm core is tested in test/native and wasm.test.mjs);
// what this file pins is who runs what, when.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createPrivateKey, createPublicKey, sign, verify } from 'node:crypto';
import { CROSS_DEVICE, SAME_DEVICE, SafeActions, isSafeAction } from '../js/safe-actions.mjs';

const CONTEXT = Buffer.concat([Buffer.from('CKey Safe Action v1'), Buffer.from([0])]);
const SEED = Buffer.from('9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60', 'hex');
const PRIV = createPrivateKey({ key: Buffer.concat([Buffer.from('302e020100300506032b657004220420', 'hex'), SEED]), format: 'der', type: 'pkcs8' });
const PUB_DER = createPublicKey(PRIV).export({ format: 'der', type: 'spki' });
const PUB_HEX = PUB_DER.subarray(12).toString('hex');
const NOW = 1794000000;

const str = (s) => Buffer.concat([Buffer.from([Buffer.byteLength(s)]), Buffer.from(s)]);
const u64 = (n) => { const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(n)); return b; };
function make(f) {
  const params = Buffer.from(f.params ?? '');
  const len = Buffer.alloc(2); len.writeUInt16BE(params.length);
  const body = Buffer.concat([Buffer.from([1, f.scope === CROSS_DEVICE ? 1 : 0]), str(f.issuer ?? 'citeory.com'), str(f.app ?? 'com.viadere'),
    str(f.action ?? 'open.library'), str(f.key_id ?? 'k1'), str(f.request_id ?? 'req-' + Math.random().toString(36).slice(2, 12)),
    u64(f.issued_at ?? NOW - 10), u64(f.expires_at ?? NOW + 60), len, params]);
  return 'ckey-action:1:' + Buffer.concat([body, sign(null, Buffer.concat([CONTEXT, body]), PRIV)]).toString('base64url');
}

// Reference core: the same contract as the wasm bridge (fields, or throw with the reason).
function parseBytes(bytes) {
  const text = Buffer.from(bytes).toString();
  if (!text.startsWith('ckey-action:1:')) throw new Error('not a Safe Action');
  const b = Buffer.from(text.slice(14), 'base64url');
  let at = 2;
  const read = () => { const n = b[at++]; const s = b.subarray(at, at + n).toString('latin1'); at += n; return s; };
  const [issuer, app, action, key_id, request_id] = [read(), read(), read(), read(), read()];
  const issued_at = Number(b.readBigUInt64BE(at)); const expires_at = Number(b.readBigUInt64BE(at + 8)); at += 16;
  const plen = b.readUInt16BE(at); at += 2;
  const params = new Uint8Array(b.subarray(at, at + plen)); at += plen;
  return { b, sigAt: at, fields: { scope: b[1] ? CROSS_DEVICE : SAME_DEVICE, issuer, app, action, key_id, request_id, issued_at, expires_at, params } };
}
const core = {
  parse: (bytes) => parseBytes(bytes).fields,
  verify(bytes, key, now, maxLifetime) {
    const { b, sigAt, fields } = parseBytes(bytes);
    const spki = Buffer.concat([PUB_DER.subarray(0, 12), Buffer.from(key)]);
    const ok = verify(null, Buffer.concat([CONTEXT, b.subarray(0, sigAt)]), createPublicKey({ key: spki, format: 'der', type: 'spki' }), b.subarray(sigAt));
    if (!ok) throw new Error('the signature does not match this key');
    if (fields.expires_at - fields.issued_at > maxLifetime || now >= fields.expires_at || now + 120 < fields.issued_at) throw new Error('Safe Action expired');
    return fields;
  }
};

function setup({ requestInfo = { request_id: null, status: 'pending', device: 'Chrome on Windows', location: 'near Lagos', requested_at: '2026-11-07T20:14:03Z' } } = {}) {
  const fetched = [];
  const fetch = async (url) => {
    fetched.push(url);
    const id = decodeURIComponent(url.split('/').pop());
    return Response.json({ ...requestInfo, request_id: requestInfo.request_id ?? id });
  };
  const calls = [];
  const sa = new SafeActions(core, 'com.viadere', { now: () => NOW * 1000, fetch })
    .trust('citeory.com', 'k1', PUB_HEX, { requestInfo: 'https://citeory.com/api/ckey/requests/{request_id}' })
    .register('open.library', { issuer: 'citeory.com' }, (a) => { calls.push(['open', a.paramsJSON]); return 'opened'; })
    .register('signin.request', { issuer: 'citeory.com', scope: CROSS_DEVICE, title: 'Sign in to citeory.com?' }, (a) => { calls.push(['signin', a.requestId]); return 'signed-in'; });
  return { sa, calls, fetched };
}

test('anything that is not a Safe Action is left to the ordinary rules', async () => {
  const { sa, calls } = setup();
  for (const text of ['https://citeory.com', 'ckey-action:2:abc', 'hello', '']) assert.deepEqual(await sa.handle(text), { kind: 'not-safe-action' });
  assert.equal(isSafeAction('ckey-action:1:x'), true);
  assert.equal(calls.length, 0);
});

test('a verified same-device action runs at once, with no prompt', async () => {
  const { sa, calls } = setup();
  const out = await sa.handle(make({ params: '{"item":"apa-7"}' }));
  assert.equal(out.kind, 'ran');
  assert.equal(out.result, 'opened');
  assert.deepEqual(calls, [['open', { item: 'apa-7' }]]);
  assert.equal(out.action.issuer, 'citeory.com');
  assert.ok(Object.isFrozen(out.action));
});

test('nothing runs unless every check passes', async () => {
  const { sa, calls } = setup();
  const cases = [
    [make({ app: 'com.other' }), /another app/],
    [make({ action: 'delete.everything' }), /does not accept/],
    [make({ issuer: 'evil.example' }), /may not request/],
    [make({ key_id: 'k2' }), /no trusted key/],
    [make({ expires_at: NOW - 1, issued_at: NOW - 60 }), /expired/],
    [make({ expires_at: NOW + 7200 }), /expired/],
  ];
  for (const [payload, reason] of cases) {
    const out = await sa.handle(payload);
    assert.equal(out.kind, 'rejected', String(reason));
    assert.match(out.reason, reason);
  }
  const good = make({});
  const tampered = good.slice(0, -6) + (good.slice(-6, -5) === 'A' ? 'B' : 'A') + good.slice(-5);
  assert.equal((await sa.handle(tampered)).kind, 'rejected');
  assert.equal(calls.length, 0);
});

test('a code works once', async () => {
  const { sa, calls } = setup();
  const payload = make({ request_id: 'req-only-once' });
  assert.equal((await sa.handle(payload)).kind, 'ran');
  const again = await sa.handle(payload);
  assert.equal(again.kind, 'rejected');
  assert.match(again.reason, /already been used/);
  assert.equal(calls.length, 1);
});

test('a cross-device action waits for the SDK confirmation, which shows who is asking', async () => {
  const { sa, calls, fetched } = setup();
  const out = await sa.handle(make({ action: 'signin.request', scope: CROSS_DEVICE, request_id: 'sess-0123456789' }));
  assert.equal(out.kind, 'confirm');
  assert.equal(calls.length, 0, 'nothing ran before the confirmation');
  assert.deepEqual(fetched, ['https://citeory.com/api/ckey/requests/sess-0123456789']);
  assert.equal(out.request.requestInfo.device, 'Chrome on Windows');
  assert.equal(typeof out.request.approve, 'undefined', 'there is no public way to approve');
  assert.ok(Object.isFrozen(out.request));
  // The SDK's own dialog: decline runs nothing; a second present() cannot approve after a decision.
  const doc = fakeDocument();
  const declined = out.request.present({ document: doc });
  doc.click('Cancel');
  assert.deepEqual(await declined, { approved: false });
  assert.deepEqual(await out.request.present({ document: doc }), { approved: false, reason: 'already decided' });
  assert.equal(calls.length, 0);
  const second = await sa.handle(make({ action: 'signin.request', scope: CROSS_DEVICE, request_id: 'sess-abcdefghij' }));
  const approved = second.request.present({ document: doc });
  assert.match(doc.text(), /citeory\.com \(verified\)/);
  assert.match(doc.text(), /Chrome on Windows/);
  doc.click('Continue');
  assert.deepEqual(await approved, { approved: true, result: 'signed-in' });
  assert.deepEqual(calls, [['signin', 'sess-abcdefghij']]);
});

test('the stricter scope wins: a same-device envelope for a cross-device registration still needs confirmation', async () => {
  const { sa } = setup();
  assert.equal((await sa.handle(make({ action: 'signin.request', scope: SAME_DEVICE }))).kind, 'confirm');
});

test('a cross-device action cannot run without the issuer confirming a pending request', async () => {
  const { sa, calls } = setup({ requestInfo: { status: 'used' } });
  const out = await sa.handle(make({ action: 'signin.request', scope: CROSS_DEVICE }));
  assert.equal(out.kind, 'rejected');
  assert.match(out.reason, /did not confirm a pending request/);
  const off = new SafeActions(core, 'com.viadere', { now: () => NOW * 1000, fetch: async () => Response.json({}) })
    .trust('citeory.com', 'k1', PUB_HEX, { requestInfo: 'http://citeory.com/{request_id}' })
    .register('signin.request', { issuer: 'citeory.com', scope: CROSS_DEVICE }, () => calls.push('x'));
  assert.equal((await off.handle(make({ action: 'signin.request', scope: CROSS_DEVICE }))).kind, 'rejected', 'request information must be HTTPS from the issuer');
  assert.equal(calls.length, 0);
});

test('keys can come from the issuer key file when the app opts in', async () => {
  const urls = [];
  const fetch = async (url) => { urls.push(url); return Response.json({ issuer: 'citeory.com', keys: [{ id: 'k1', alg: 'Ed25519', public_key: PUB_HEX }] }); };
  const sa = new SafeActions(core, 'com.viadere', { now: () => NOW * 1000, fetch, keyFiles: true })
    .register('open.library', { issuer: 'citeory.com' }, () => 'ok');
  assert.equal((await sa.handle(make({}))).kind, 'ran');
  assert.equal((await sa.handle(make({}))).kind, 'ran');
  assert.deepEqual(urls, ['https://citeory.com/.well-known/ckey-safe-actions.json'], 'cached');
  const pinnedOnly = new SafeActions(core, 'com.viadere', { now: () => NOW * 1000, fetch }).register('open.library', { issuer: 'citeory.com' }, () => 'ok');
  assert.equal((await pinnedOnly.handle(make({}))).kind, 'rejected', 'pinned keys only by default');
});

test('registration is validated', () => {
  const sa = new SafeActions(core, 'com.viadere');
  assert.throws(() => sa.register('Bad Name', { issuer: 'citeory.com' }, () => {}));
  assert.throws(() => sa.register('ok', {}, () => {}), /issuer/);
  assert.throws(() => sa.register('ok', { issuer: 'citeory.com', scope: 'everywhere' }, () => {}));
  assert.throws(() => sa.trust('Citeory.com', 'k1', PUB_HEX));
  assert.throws(() => sa.trust('citeory.com', 'k1', 'abcd'));
  assert.throws(() => new SafeActions(core, 'com.viadere', { maxLifetime: 7200 }));
});

// A minimal DOM for the confirmation dialog.
function fakeDocument() {
  const nodes = [];
  const make = (tag) => {
    const node = { tag, children: [], listeners: {}, attributes: {}, className: '', textContent: '', type: '', id: '',
      append(...c) { this.children.push(...c); }, setAttribute(k, v) { this.attributes[k] = v; },
      addEventListener(k, fn) { this.listeners[k] = fn; }, remove() { const i = nodes.indexOf(this); if (i >= 0) nodes.splice(i, 1); },
      showModal() {}, close() {}, focus() {} };
    return node;
  };
  const all = (n) => [n, ...n.children.flatMap(all)];
  return {
    createElement: make,
    body: { append: (n) => nodes.push(n) },
    text: () => nodes.flatMap(all).map((n) => n.textContent).join(' '),
    click: (label) => nodes.flatMap(all).find((n) => n.tag === 'button' && n.textContent === label).listeners.click(),
  };
}

// ES256 (ECDSA P-256, raw r || s, low-S): the same policy layer, with the
// algorithm passed to the core from a pinned key's length or the key file's alg.
test('ES256 issuer keys: pinned by length or from the key file, and the alg reaches the core', async () => {
  const { generateKeyPairSync, createPublicKey: pub } = await import('node:crypto');
  const N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
  const { privateKey, publicKey } = generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const jwk = publicKey.export({ format: 'jwk' });
  const x = Buffer.from(jwk.x, 'base64url'), y = Buffer.from(jwk.y, 'base64url');
  const uncompressed = Buffer.concat([Buffer.from([4]), x, y]), compressed = Buffer.concat([Buffer.from([2 + (y[31] & 1)]), x]);
  const signEs = (payload) => {
    const { b, sigAt } = parseBytes(Buffer.from(payload));
    const raw = sign('sha256', Buffer.concat([CONTEXT, b.subarray(0, sigAt)]), { key: privateKey, dsaEncoding: 'ieee-p1363' });
    let s = BigInt('0x' + raw.subarray(32).toString('hex')); if (s > N / 2n) s = N - s;
    const sig = Buffer.concat([raw.subarray(0, 32), Buffer.from(s.toString(16).padStart(64, '0'), 'hex')]);
    return 'ckey-action:1:' + Buffer.concat([b.subarray(0, sigAt), sig]).toString('base64url');
  };
  const seen = [];
  const esCore = {
    parse: core.parse,
    verify(bytes, key, now, maxLifetime, alg) {
      seen.push([alg, key.length]);
      if (alg !== 'ES256') return core.verify(bytes, key, now, maxLifetime);
      const { b, sigAt, fields } = parseBytes(bytes);
      const spki = Buffer.concat([Buffer.from(key.length === 33 ? '3039301306072a8648ce3d020106082a8648ce3d030107032200' : '3059301306072a8648ce3d020106082a8648ce3d030107034200', 'hex'), Buffer.from(key)]);
      const ok = verify('sha256', Buffer.concat([CONTEXT, b.subarray(0, sigAt)]), { key: pub({ key: spki, format: 'der', type: 'spki' }), dsaEncoding: 'ieee-p1363' }, b.subarray(sigAt));
      if (!ok) throw new Error('the signature does not match this key');
      return fields;
    }
  };
  const keyFile = { issuer: 'viadere.com', keys: [
    { id: 'rsa-1', alg: 'RS256', public_key: 'ab' },
    { id: 'vault-p256-1', alg: 'ES256', public_key: compressed.toString('hex') },
  ] };
  const fetch = async () => Response.json(keyFile);
  const sa = new SafeActions(esCore, 'com.viadere', { now: () => NOW * 1000, fetch, keyFiles: true })
    .trust('citeory.com', 'pinned-p256', uncompressed.toString('hex'))
    .register('open.library', { issuers: ['citeory.com', 'viadere.com'] }, () => 'opened');
  const fromFile = await sa.handle(signEs(make({ issuer: 'viadere.com', key_id: 'vault-p256-1' })));
  assert.equal(fromFile.kind, 'ran');
  const pinned = await sa.handle(signEs(make({ key_id: 'pinned-p256' })));
  assert.equal(pinned.kind, 'ran');
  assert.deepEqual(seen, [['ES256', 33], ['ES256', 65]]);
  // A key file entry with an unknown alg is ignored.
  const unknown = await sa.handle(signEs(make({ issuer: 'viadere.com', key_id: 'rsa-1' })));
  assert.equal(unknown.kind, 'rejected');
  assert.match(unknown.reason, /no trusted key/);
  // Pinned keys of the wrong length, or an alg that does not fit, are refused.
  assert.throws(() => sa.trust('citeory.com', 'bad', 'ab'.repeat(40)), /alg is Ed25519 or ES256/);
  assert.throws(() => sa.trust('citeory.com', 'bad', uncompressed, { alg: 'Ed25519' }), /32 bytes/);
});
