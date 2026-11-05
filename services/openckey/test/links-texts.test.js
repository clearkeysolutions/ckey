import { test, describe, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, admin, TOKEN } from './helpers.js';

describe('short links', () => {
  let s;
  before(async () => {
    s = await startServer();
  });
  after(() => s.stop());

  const create = (body, opts) => admin(s.base, '/api/links', { body, ...opts });

  test('create requires a valid bearer token', async () => {
    assert.equal((await create({ url: 'https://example.com/' }, { token: null })).status, 401);
    const r = await create({ url: 'https://example.com/' }, { token: 'wrong' });
    assert.equal(r.status, 401);
    assert.match(r.headers.get('www-authenticate'), /Bearer/);
    assert.equal((await create({ url: 'https://example.com/' }, { token: `${TOKEN}x` })).status, 401);
    // Second configured token also works.
    assert.equal((await create({ url: 'https://example.com/' }, { token: 'other-token-xyz' })).status, 201);
  });

  test('admin routes refuse everything when no tokens are configured', async () => {
    const s2 = await startServer({ env: { OPENCKEY_ADMIN_TOKENS: '' } });
    try {
      const r = await admin(s2.base, '/api/links', { body: { url: 'https://example.com/' }, token: '' });
      assert.equal(r.status, 401);
    } finally {
      await s2.stop();
    }
  });

  for (const url of [
    'javascript:alert(1)',
    'data:text/html,<script>alert(1)</script>',
    'ftp://example.com/file',
    'file:///etc/passwd',
    'https://',
    'not a url',
    'https://user:pass@example.com/',
    'https://example.com/\nx',
    `https://example.com/${'a'.repeat(2048)}`,
  ]) {
    test(`rejects destination ${JSON.stringify(url.slice(0, 40))}`, async () => {
      const r = await create({ url });
      assert.equal(r.status, 400);
      assert.equal(typeof (await r.json()).error, 'string');
    });
  }

  test('rejects unknown fields, bad count and wrong content type', async () => {
    assert.equal((await create({ url: 'https://example.com/', extra: 1 })).status, 400);
    assert.equal((await create({ url: 'https://example.com/', count: 'yes' })).status, 400);
    assert.equal((await create({ count: true })).status, 400);
    const r = await fetch(`${s.base}/api/links`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${TOKEN}`, 'Content-Type': 'text/plain' },
      body: '{"url":"https://example.com/"}',
    });
    assert.equal(r.status, 415);
  });

  test('interstitial shows the host first and never redirects by itself', async () => {
    const r = await create({ url: 'https://shop.example.org/a/very/long/path?x=1&y=<2>' });
    assert.equal(r.status, 201);
    const { id, short_url } = await r.json();
    assert.match(id, /^[0-9A-Za-z]{7}$/);
    assert.equal(short_url, `https://openckey.example/r/${id}`);

    const page = await fetch(`${s.base}/r/${id}`, { redirect: 'manual' });
    assert.equal(page.status, 200);
    assert.match(page.headers.get('content-type'), /text\/html/);
    assert.match(page.headers.get('content-security-policy'), /default-src 'none'/);
    assert.equal(page.headers.get('referrer-policy'), 'no-referrer');
    const html = await page.text();
    assert.match(html, /<h1 class="host">shop\.example\.org<\/h1>/);
    assert.ok(html.indexOf('shop.example.org') < html.indexOf('/a/very/long/path'), 'host comes before the full URL');
    assert.match(html, /Continue to shop\.example\.org<\/a>/);
    assert.match(html, /href="https:\/\/shop\.example\.org\/a\/very\/long\/path\?x=1&amp;y=%3C2%3E"/);
    assert.doesNotMatch(html, /<script|http-equiv="refresh"/i);
  });

  test('go=1 returns 302 to the destination', async () => {
    const { id } = await (await create({ url: 'https://example.net/landing' })).json();
    const r = await fetch(`${s.base}/r/${id}?go=1`, { redirect: 'manual' });
    assert.equal(r.status, 302);
    assert.equal(r.headers.get('location'), 'https://example.net/landing');
  });

  test('counts only when opted in, per day, and not on HEAD', async () => {
    const counted = (await (await create({ url: 'https://example.com/c', count: true })).json()).id;
    const silent = (await (await create({ url: 'https://example.com/s' })).json()).id;
    await fetch(`${s.base}/r/${counted}`);
    await fetch(`${s.base}/r/${counted}?go=1`, { redirect: 'manual' });
    await fetch(`${s.base}/r/${counted}`, { method: 'HEAD' });
    await fetch(`${s.base}/r/${silent}`);
    await fetch(`${s.base}/r/${silent}?go=1`, { redirect: 'manual' });

    const st = await (await admin(s.base, `/api/links/${counted}/stats`, { method: 'GET' })).json();
    assert.equal(st.count, true);
    assert.equal(st.total, 2);
    assert.deepEqual(st.days, [{ day: '2026-11-05', hits: 2 }]);

    const st2 = await (await admin(s.base, `/api/links/${silent}/stats`, { method: 'GET' })).json();
    assert.equal(st2.count, false);
    assert.equal(st2.total, 0);
    assert.deepEqual(st2.days, []);
    assert.equal(s.app.db.prepare('SELECT COUNT(*) AS n FROM link_daily WHERE id = ?').get(silent).n, 0);

    assert.equal((await admin(s.base, `/api/links/${counted}/stats`, { method: 'GET', token: null })).status, 401);
  });

  test('delete disables the link (410 afterwards)', async () => {
    const { id } = await (await create({ url: 'https://example.com/gone' })).json();
    assert.equal((await admin(s.base, `/api/links/${id}`, { method: 'DELETE', token: 'bad' })).status, 401);
    assert.equal((await admin(s.base, `/api/links/${id}`, { method: 'DELETE' })).status, 204);
    assert.equal((await fetch(`${s.base}/r/${id}`)).status, 410);
    assert.equal((await fetch(`${s.base}/r/${id}?go=1`, { redirect: 'manual' })).status, 410);
    const html = await fetch(`${s.base}/r/${id}`, { headers: { Accept: 'text/html' } });
    assert.equal(html.status, 410);
    assert.match(html.headers.get('content-type'), /text\/html/);
    assert.equal((await admin(s.base, '/api/links/ZZZZZZZ', { method: 'DELETE' })).status, 404);
  });

  test('unknown link is 404', async () => {
    const r = await fetch(`${s.base}/r/abcdefg`);
    assert.equal(r.status, 404);
    assert.deepEqual(await r.json(), { error: 'link not found' });
  });
});

describe('hosted texts', () => {
  let s;
  before(async () => {
    s = await startServer();
  });
  after(() => s.stop());

  const create = (body, opts) => admin(s.base, '/api/texts', { body, ...opts });

  test('requires auth', async () => {
    assert.equal((await create({ text: 'hi' }, { token: null })).status, 401);
  });

  test('renders escaped text with newlines and no active links', async () => {
    const text = '<script>alert("x")</script>\nLine two & more\nhttps://evil.example/<b>';
    const r = await create({ text, title: 'Notes <i>' });
    assert.equal(r.status, 201);
    const j = await r.json();
    assert.match(j.id, /^[0-9A-Za-z]{7}$/);
    assert.equal(j.url, `https://openckey.example/t/${j.id}`);

    const page = await fetch(`${s.base}/t/${j.id}`);
    assert.equal(page.status, 200);
    assert.equal(page.headers.get('content-security-policy').startsWith("default-src 'none'; style-src 'unsafe-inline'"), true);
    assert.equal(page.headers.get('x-content-type-options'), 'nosniff');
    assert.equal(page.headers.get('referrer-policy'), 'no-referrer');
    const html = await page.text();
    assert.doesNotMatch(html, /<script>/);
    assert.match(html, /&lt;script&gt;alert\(&quot;x&quot;\)&lt;\/script&gt;\nLine two &amp; more\n/);
    assert.match(html, /<h1 class="title">Notes &lt;i&gt;<\/h1>/);
    assert.doesNotMatch(html, /href="https:\/\/evil/);
    assert.match(html, /white-space:pre-wrap/);

    const txt = await fetch(`${s.base}/t/${j.id}.txt`);
    assert.equal(txt.status, 200);
    assert.match(txt.headers.get('content-type'), /^text\/plain; charset=utf-8/);
    assert.equal(txt.headers.get('x-content-type-options'), 'nosniff');
    assert.equal(await txt.text(), text);
  });

  test('size and content limits', async () => {
    assert.equal((await create({ text: 'é'.repeat(32 * 1024) })).status, 201); // exactly 64 KiB
    assert.equal((await create({ text: `${'é'.repeat(32 * 1024)}a` })).status, 400);
    assert.equal((await create({ text: '' })).status, 400);
    assert.equal((await create({ text: 'a\u0000b' })).status, 400);
    assert.equal((await create({ text: 'ok', extra: 1 })).status, 400);
    assert.equal((await create({ text: 'ok', title: 5 })).status, 400);
  });

  test('delete purges the text (410 afterwards)', async () => {
    const { id } = await (await create({ text: 'secret' })).json();
    assert.equal((await admin(s.base, `/api/texts/${id}`, { method: 'DELETE' })).status, 204);
    assert.equal((await fetch(`${s.base}/t/${id}`)).status, 410);
    assert.equal((await fetch(`${s.base}/t/${id}.txt`)).status, 410);
    assert.equal(s.app.db.prepare('SELECT body FROM texts WHERE id = ?').get(id).body, '');
  });
});
