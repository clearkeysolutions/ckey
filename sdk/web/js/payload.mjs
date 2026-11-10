// Payload interpretation per spec/CKey-Payload-Conventions.md. Pure
// JavaScript. These helpers only describe a proposed action; they never open,
// dial, navigate or otherwise act. The host shows the payload and the action
// and lets the user decide (safety rule 1).

const ALLOWED_CONTROL = new Set([0x09, 0x0a, 0x0d]); // tab, LF, CR (vCards use CR LF)

/**
 * Text if the bytes are valid UTF-8 with no control characters other than
 * tab, LF and CR; otherwise null (show the payload as binary, e.g. hex).
 */
export function readableText(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length === 0) return null;
  let text;
  try {
    text = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes);
  } catch {
    return null;
  }
  for (const ch of text) {
    const c = ch.codePointAt(0);
    if ((c < 0x20 || (c >= 0x7f && c <= 0x9f)) && !ALLOWED_CONTROL.has(c)) return null;
  }
  return text;
}

/** Lowercase hexadecimal of bytes. */
export function toHex(bytes) {
  let s = '';
  for (const b of bytes) s += b.toString(16).padStart(2, '0');
  return s;
}

function decodeSafe(s) {
  try {
    return decodeURIComponent(s);
  } catch {
    return s;
  }
}

const TEXT = (label = 'Plain text') => ({ kind: 'text', target: null, label });

/**
 * Classifies a text payload into the action a conforming reader may OFFER.
 *   kind:   'https' | 'http' | 'mailto' | 'tel' | 'sms' | 'geo' | 'address' | 'vcard' | 'text'
 *   target: what the host would hand to the platform if the user accepts:
 *           the trimmed URI for https/http/mailto/tel/sms/geo, the joined
 *           address for address:, the vCard text for vcard; null for text.
 *   label:  the action stating its target, e.g. "Open openckey.org",
 *           "Call +15551234567", "Show 1 Infinite Loop, Cupertino on a map".
 * Anything not on the allowlist, or failing validation (rule 4), is 'text'
 * with no action (javascript:, data:, file:, intent:, custom schemes ...).
 */
export function classifyPayload(input) {
  if (input === null || input === undefined) return TEXT();
  if (input instanceof Uint8Array) {
    const t = readableText(input);
    if (t === null) return { kind: 'text', target: null, label: 'Binary data' };
    input = t;
  }
  const t = String(input).trim();
  if (t === '') return TEXT();

  if (/^BEGIN:VCARD\r?\n/i.test(t)) {
    if (!/\r?\nEND:VCARD$/i.test(t)) return TEXT();
    const fn = /(?:^|\r?\n)FN(?:;[^:\r\n]*)?:([^\r\n]*)/i.exec(t);
    const name = fn ? fn[1].trim() : '';
    return { kind: 'vcard', target: t, label: name ? `Add contact ${name}` : 'Add contact' };
  }

  const m = /^([A-Za-z][A-Za-z0-9+.-]*):/.exec(t);
  if (!m) return TEXT();
  const scheme = m[1].toLowerCase();
  const rest = t.slice(m[0].length);

  switch (scheme) {
    case 'https':
    case 'http': {
      let url;
      try {
        url = new URL(t);
      } catch {
        return TEXT();
      }
      if (url.protocol !== `${scheme}:` || !url.host) return TEXT();
      return { kind: scheme, target: t, label: `Open ${url.host}` };
    }
    case 'mailto': {
      if (!rest.includes('@')) return TEXT();
      const addr = decodeSafe(rest.split('?')[0]);
      return { kind: 'mailto', target: t, label: addr ? `Email ${addr}` : 'Compose an email' };
    }
    case 'tel':
    case 'sms': {
      const number = rest.split(/[?;]/)[0].replace(/[^+0-9]/g, '');
      if (number.replace(/\+/g, '').length < 3) return TEXT();
      return { kind: scheme, target: t, label: `${scheme === 'tel' ? 'Call' : 'Message'} ${number}` };
    }
    case 'geo': {
      const g = /^(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)/.exec(rest);
      if (!g || Math.abs(Number(g[1])) > 90 || Math.abs(Number(g[2])) > 180) return TEXT();
      return { kind: 'geo', target: t, label: `Show ${g[1]}, ${g[2]} on a map` };
    }
    case 'address': {
      const joined = rest
        .split(/\r?\n/)
        .map((s) => s.trim())
        .filter(Boolean)
        .join(', ');
      if (joined === '') return TEXT();
      return { kind: 'address', target: joined, label: `Show ${joined} on a map` };
    }
    default:
      return TEXT();
  }
}
