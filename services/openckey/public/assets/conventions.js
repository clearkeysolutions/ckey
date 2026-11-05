// Payload convention detection, following spec/CKey-Payload-Conventions.md.
// Returns { kind, label, action, valid } where `action` is what a conforming
// reader would offer (null for plain text), or null for an empty payload.

export function detectConvention(input) {
  const t = String(input).trim();
  if (t === '') return null;

  if (/^BEGIN:VCARD\r?\n/i.test(t)) {
    const valid = /\r?\nEND:VCARD$/i.test(t);
    return {
      kind: 'vcard',
      label: 'vCard contact (RFC 6350)',
      action: valid ? 'Offer to add the contact' : null,
      valid,
      problem: valid ? null : 'must end with END:VCARD',
    };
  }

  const m = /^([A-Za-z][A-Za-z0-9+.-]*):/.exec(t);
  const scheme = m ? m[1].toLowerCase() : null;
  const rest = m ? t.slice(m[0].length) : '';
  const out = (kind, label, valid, action, problem = null) => ({ kind, label, valid, action: valid ? action : null, problem });

  switch (scheme) {
    case 'https':
    case 'http': {
      let host = '';
      try {
        host = new URL(t).host;
      } catch {
        /* invalid */
      }
      return out(scheme, `Web link (${scheme})`, Boolean(host), `Open ${host}`, 'needs a host');
    }
    case 'mailto': {
      const addr = decodeSafe(rest.split('?')[0]);
      return out('mailto', 'Email (mailto)', rest.includes('@'), `Email ${addr}`, 'must contain an @');
    }
    case 'tel':
    case 'sms': {
      const number = rest.split(/[?;]/)[0].replace(/[^+0-9]/g, '');
      const digits = number.replace(/\+/g, '').length;
      return out(
        scheme,
        scheme === 'tel' ? 'Phone number (tel)' : 'Text message (sms)',
        digits >= 3,
        `${scheme === 'tel' ? 'Call' : 'Message'} ${number}`,
        'needs at least three digits',
      );
    }
    case 'geo': {
      const g = /^(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)/.exec(rest);
      const ok = Boolean(g) && Math.abs(Number(g[1])) <= 90 && Math.abs(Number(g[2])) <= 180;
      return out('geo', 'Map coordinates (geo)', ok, ok ? `Show ${g[1]}, ${g[2]} on a map` : null, 'latitude must be within ±90 and longitude within ±180');
    }
    case 'address': {
      const joined = rest
        .split(/\r?\n/)
        .map((s) => s.trim())
        .filter(Boolean)
        .join(', ');
      return out('address', 'Postal address (CKey address: convention)', joined !== '', `Show ${joined} on a map`, 'must not be empty');
    }
    default:
      return {
        kind: 'text',
        label: scheme ? `Plain text (the ${scheme}: scheme is not on the allowlist, so readers offer no action)` : 'Plain text',
        action: null,
        valid: true,
        problem: null,
      };
  }
}

function decodeSafe(s) {
  try {
    return decodeURIComponent(s);
  } catch {
    return s;
  }
}
