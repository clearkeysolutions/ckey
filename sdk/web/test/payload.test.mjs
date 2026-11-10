// Pure-JS tests: payload classification (spec/CKey-Payload-Conventions.md).
import test from 'node:test';
import assert from 'node:assert/strict';
import { classifyPayload, readableText, toHex } from '../js/payload.mjs';

const kind = (t) => classifyPayload(t).kind;

test('recognized forms', () => {
  assert.deepEqual(classifyPayload('https://citeory.com/tour'), {
    kind: 'https', target: 'https://citeory.com/tour', label: 'Open citeory.com',
  });
  assert.equal(classifyPayload('  HTTP://Example.com/x  ').kind, 'http');
  assert.equal(classifyPayload('  HTTP://Example.com/x  ').target, 'HTTP://Example.com/x');
  assert.deepEqual(classifyPayload('mailto:hello@citeory.com?subject=Hi'), {
    kind: 'mailto', target: 'mailto:hello@citeory.com?subject=Hi', label: 'Email hello@citeory.com',
  });
  assert.equal(classifyPayload('tel:+1 (555) 123-4567').label, 'Call +15551234567');
  assert.equal(classifyPayload('sms:+15551234567?body=Hi').label, 'Message +15551234567');
  assert.equal(classifyPayload('geo:37.33490,-122.00900?q=Apple%20Park').label, 'Show 37.33490, -122.00900 on a map');
  assert.deepEqual(classifyPayload('address:1 Infinite Loop\n Cupertino, CA 95014\n\n'), {
    kind: 'address', target: '1 Infinite Loop, Cupertino, CA 95014', label: 'Show 1 Infinite Loop, Cupertino, CA 95014 on a map',
  });
  const vcard = 'BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Ada Lovelace\r\nEND:VCARD';
  assert.deepEqual(classifyPayload(vcard), { kind: 'vcard', target: vcard, label: 'Add contact Ada Lovelace' });
});

test('validation failures are plain text (rule 4)', () => {
  assert.equal(kind('mailto:nobody'), 'text');
  assert.equal(kind('tel:12'), 'text');
  assert.equal(kind('sms:+1'), 'text');
  assert.equal(kind('geo:91,0'), 'text');
  assert.equal(kind('geo:0,181'), 'text');
  assert.equal(kind('geo:abc'), 'text');
  assert.equal(kind('address:   \n  '), 'text');
  assert.equal(kind('https://'), 'text');
  assert.equal(kind('BEGIN:VCARD\nFN:x'), 'text');
});

test('allowlist only (rule 2)', () => {
  for (const t of ['javascript:alert(1)', 'data:text/html,hi', 'file:///etc/passwd', 'intent://x#Intent;end', 'myapp://open', 'ftp://x.org']) {
    assert.deepEqual(classifyPayload(t), { kind: 'text', target: null, label: 'Plain text' }, t);
  }
  assert.equal(kind('just words'), 'text');
  assert.equal(kind(''), 'text');
});

test('bytes input and readable text', () => {
  const enc = new TextEncoder();
  assert.equal(classifyPayload(enc.encode('https://openckey.org')).kind, 'https');
  assert.equal(classifyPayload(Uint8Array.of(0xff, 0x00)).label, 'Binary data');
  assert.equal(readableText(enc.encode('a\tb\r\nc')), 'a\tb\r\nc');
  assert.equal(readableText(enc.encode('Grüße 👋')), 'Grüße 👋');
  assert.equal(readableText(Uint8Array.of(0x61, 0x00)), null);
  assert.equal(readableText(Uint8Array.of(0x61, 0x1b)), null);
  assert.equal(readableText(enc.encode('a\u0085b')), null);
  assert.equal(readableText(Uint8Array.of(0xc3, 0x28)), null);
  assert.equal(readableText(new Uint8Array(0)), null);
  assert.equal(toHex(Uint8Array.of(0, 15, 255)), '000fff');
});
