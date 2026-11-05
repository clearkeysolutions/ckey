import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { startServer } from './helpers.js';
import { detectConvention } from '../public/assets/conventions.js';

describe('static files and SDK', () => {
  test('index page is served with security headers', async () => {
    const s = await startServer();
    try {
      const r = await fetch(`${s.base}/`);
      assert.equal(r.status, 200);
      assert.match(r.headers.get('content-type'), /text\/html/);
      const csp = r.headers.get('content-security-policy');
      assert.match(csp, /script-src 'self' 'wasm-unsafe-eval'/);
      assert.match(csp, /frame-ancestors 'none'/);
      assert.equal(r.headers.get('x-content-type-options'), 'nosniff');
      assert.equal(r.headers.get('x-frame-options'), 'DENY');
      const html = await r.text();
      assert.match(html, /<title>CKey renderer<\/title>/);
      assert.match(html, /<canvas id="canvas"/);
      assert.doesNotMatch(html, /<iframe/i);
      for (const id of ['payload', 'version', 'level', 'layout', 'size', 'transparent']) {
        assert.match(html, new RegExp(`<label for="${id}">`), `label for ${id}`);
      }

      for (const [p, type] of [
        ['/assets/app.js', /javascript/],
        ['/assets/conventions.js', /javascript/],
        ['/assets/style.css', /text\/css/],
        ['/assets/icon.svg', /image\/svg/],
      ]) {
        const a = await fetch(`${s.base}${p}`);
        assert.equal(a.status, 200, p);
        assert.match(a.headers.get('content-type'), type);
      }
      assert.equal((await fetch(`${s.base}/assets/..%2Fpackage.json`)).status, 404);
      assert.equal((await fetch(`${s.base}/assets/missing.js`)).status, 404);
      assert.equal((await fetch(`${s.base}/nope`)).status, 404);
      assert.equal((await fetch(`${s.base}/healthz`)).status, 200);
    } finally {
      await s.stop();
    }
  });

  test('missing SDK gives a 404 the page handles', async () => {
    const s = await startServer();
    try {
      const r = await fetch(`${s.base}/sdk/ckey.mjs`);
      assert.equal(r.status, 404);
      assert.match((await r.json()).error, /not been built/);
      const app = await (await fetch(`${s.base}/assets/app.js`)).text();
      assert.match(app, /has not been built yet/);
      assert.match(app, /await import\('\/sdk\/ckey\.mjs'\)/);
    } finally {
      await s.stop();
    }
  });

  test('SDK files are served from OPENCKEY_SDK_DIR', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'openckey-sdk-'));
    writeFileSync(join(dir, 'ckey.mjs'), 'export async function loadCKey() {}\n');
    writeFileSync(join(dir, 'ckey.wasm'), Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]));
    const s = await startServer({ sdkDir: dir });
    try {
      const m = await fetch(`${s.base}/sdk/ckey.mjs`);
      assert.equal(m.status, 200);
      assert.match(m.headers.get('content-type'), /text\/javascript/);
      const w = await fetch(`${s.base}/sdk/ckey.wasm`);
      assert.equal(w.status, 200);
      assert.equal(w.headers.get('content-type'), 'application/wasm');
      assert.equal(Buffer.from(await w.arrayBuffer()).length, 8);
      assert.equal((await fetch(`${s.base}/sdk/secret.txt`)).status, 404);
    } finally {
      await s.stop();
      rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe('payload convention hint', () => {
  const kind = (t) => detectConvention(t)?.kind;
  test('recognises the allowlisted forms', () => {
    assert.equal(kind('https://citeory.com/tour'), 'https');
    assert.equal(detectConvention(' HTTPS://citeory.com/tour ').action, 'Open citeory.com');
    assert.equal(kind('http://example.com'), 'http');
    assert.equal(kind('mailto:hello@citeory.com?subject=Hi'), 'mailto');
    assert.equal(kind('tel:+15551234567'), 'tel');
    assert.equal(kind('sms:+15551234567?body=Hi'), 'sms');
    assert.equal(kind('geo:37.33490,-122.00900?q=Apple%20Park'), 'geo');
    assert.equal(kind('address:1 Infinite Loop, Cupertino, CA 95014'), 'address');
    assert.equal(kind('BEGIN:VCARD\nVERSION:4.0\nFN:A\nEND:VCARD'), 'vcard');
    assert.equal(detectConvention(''), null);
  });
  test('validates and refuses non-allowlisted schemes', () => {
    assert.equal(detectConvention('mailto:nobody').valid, false);
    assert.equal(detectConvention('tel:12').valid, false);
    assert.equal(detectConvention('geo:91,0').valid, false);
    assert.equal(detectConvention('address:   ').valid, false);
    assert.equal(kind('javascript:alert(1)'), 'text');
    assert.equal(detectConvention('javascript:alert(1)').action, null);
    assert.equal(kind('data:text/html,x'), 'text');
    assert.equal(kind('hello world'), 'text');
  });
});
