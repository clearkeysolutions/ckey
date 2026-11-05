import { test, describe, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { request } from 'node:http';
import { startServer, validBatch, postBatch, NOW } from './helpers.js';
import { SUMMARY_LABEL } from '../src/telemetry.js';

const clone = (o) => JSON.parse(JSON.stringify(o));

describe('telemetry ingestion', () => {
  let s;
  before(async () => {
    s = await startServer({ env: { OPENCKEY_TELEMETRY_RATE_PER_HOUR: '1000', OPENCKEY_TELEMETRY_BURST: '1000' } });
  });
  after(() => s.stop());

  test('valid batch is accepted and aggregated', async () => {
    const r1 = await postBatch(s.base, validBatch());
    assert.equal(r1.status, 204);
    const r2 = await postBatch(s.base, validBatch(), { contentType: 'application/json; charset=utf-8' });
    assert.equal(r2.status, 204);
    assert.equal(r1.headers.get('access-control-allow-origin'), '*');

    const rows = s.app.db.prepare('SELECT * FROM telemetry_daily').all();
    assert.equal(rows.length, 1);
    const row = rows[0];
    assert.equal(row.day, '2026-11-05');
    assert.equal(row.sdk, 'swift');
    assert.equal(row.sdk_version, '0.1.0-build.61');
    assert.equal(row.platform, 'ios');
    assert.equal(row.batches, 2);
    assert.equal(row.sessions_started, 24);
    assert.equal(row.frames, 6240);
    assert.equal(row.decoded_p0, 14);
    assert.equal(row.ttd_sum, 8420);
    assert.equal(row.ttd_b3, 8);
    assert.equal(row.frame_b2, 4000);

    // A different platform is a separate aggregate row.
    assert.equal((await postBatch(s.base, validBatch({ platform: 'macos' }))).status, 204);
    assert.equal(s.app.db.prepare('SELECT COUNT(*) AS n FROM telemetry_daily').get().n, 2);
  });

  test('raw bodies and IP-derived data are never stored', async () => {
    const tables = s.app.db.prepare("SELECT name, sql FROM sqlite_master WHERE type = 'table'").all();
    for (const t of tables) assert.doesNotMatch(t.sql, /\b(ip|ip_\w+|\w+_ip|addr\w*|user_agent|referr?er)\b/i, t.name);
    for (const t of tables.filter((x) => x.name.startsWith('telemetry'))) {
      assert.doesNotMatch(t.sql, /\b(body|raw|payload)\b/i, t.name);
    }
  });

  test('log lines carry only method, route, status and duration', async () => {
    await postBatch(s.base, validBatch(), { headers: { 'X-Forwarded-For': '203.0.113.9' } });
    assert.ok(s.lines.length > 0);
    for (const l of s.lines) {
      assert.match(l, /^(GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS|OTHER) \S+ \d{3} \d+\.\d+ms$/);
      assert.doesNotMatch(l, /127\.0\.0\.1|203\.0\.113/);
    }
  });

  test('CORS preflight is answered', async () => {
    const r = await fetch(`${s.base}/api/telemetry/v1/batch`, { method: 'OPTIONS' });
    assert.equal(r.status, 204);
    assert.equal(r.headers.get('access-control-allow-origin'), '*');
    assert.match(r.headers.get('access-control-allow-headers'), /content-type/i);
  });

  const invalid = {
    'unknown top-level field': (b) => { b.device_id = 'x'; },
    'unknown nested field (sessions)': (b) => { b.sessions.extra = 1; },
    'unknown nested field (histogram)': (b) => { b.frame_ms.max = 5; },
    'missing top-level key': (b) => { delete b.frames; },
    'missing nested key': (b) => { delete b.decoded.p3; },
    'schema not 1': (b) => { b.schema = 2; },
    'bad sdk enum': (b) => { b.sdk = 'python'; },
    'bad platform enum': (b) => { b.platform = 'tvos'; },
    'bad sdk_version': (b) => { b.sdk_version = '1.0.0'; },
    'sdk_version trailing newline': (b) => { b.sdk_version = '0.1.0\n'; },
    'negative count': (b) => { b.frames = -1; },
    'float count': (b) => { b.sessions.started = 1.5; },
    'oversize count': (b) => { b.decoded.p0 = 10_000_001; },
    'oversize sum': (b) => { b.frame_ms.sum = 1_000_000_000_001; },
    'string count': (b) => { b.frames = '3120'; },
    'boolean count': (b) => { b.decoded.p1 = true; },
    'bucket sum mismatch': (b) => { b.time_to_decode_ms.buckets[0] = 1; },
    'wrong bucket length': (b) => { b.frame_ms.buckets.push(0); },
    'buckets not array': (b) => { b.frame_ms.buckets = {}; },
    'sessions is array': (b) => { b.sessions = [12, 10, 2]; },
    'day too far in the future': (b) => { b.day = '2026-11-08'; },
    'day too far in the past': (b) => { b.day = '2026-09-30'; },
    'day not a calendar date': (b) => { b.day = '2026-02-30'; },
    'day wrong format': (b) => { b.day = '2026-11-5'; },
  };
  for (const [name, mutate] of Object.entries(invalid)) {
    test(`rejects: ${name}`, async () => {
      const b = clone(validBatch());
      mutate(b);
      const r = await postBatch(s.base, b);
      assert.equal(r.status, 400);
      const j = await r.json();
      assert.equal(typeof j.error, 'string');
    });
  }

  test('day window edges are accepted (+2 and -35)', async () => {
    assert.equal((await postBatch(s.base, validBatch({ day: '2026-11-07' }))).status, 204);
    assert.equal((await postBatch(s.base, validBatch({ day: '2026-10-01' }))).status, 204);
  });

  const rawInvalid = {
    'float literal 1.0': JSON.stringify(validBatch()).replace('"frames":3120', '"frames":3120.0'),
    'exponent literal': JSON.stringify(validBatch()).replace('"frames":3120', '"frames":3.12e3'),
    'duplicate key': JSON.stringify(validBatch()).replace('"frames":3120', '"frames":3120,"frames":1'),
    'not JSON': 'hello',
    'top-level array': '[1]',
    'trailing garbage': `${JSON.stringify(validBatch())}x`,
    'empty body': '',
  };
  for (const [name, body] of Object.entries(rawInvalid)) {
    test(`rejects raw body: ${name}`, async () => {
      assert.equal((await postBatch(s.base, body)).status, 400);
    });
  }

  test('wrong content type gets 415', async () => {
    assert.equal((await postBatch(s.base, validBatch(), { contentType: 'text/plain' })).status, 415);
    assert.equal((await postBatch(s.base, validBatch(), { contentType: 'application/x-www-form-urlencoded' })).status, 415);
    assert.equal((await postBatch(s.base, validBatch(), { contentType: 'application/json; charset=latin1' })).status, 415);
  });

  test('oversize body (declared length) gets 413', async () => {
    const b = validBatch();
    const body = JSON.stringify(b) + ' '.repeat(4097 - JSON.stringify(b).length);
    assert.equal(Buffer.byteLength(body), 4097);
    const r = await postBatch(s.base, body);
    assert.equal(r.status, 413);
  });

  test('oversize chunked body gets 413 without reading it all', async () => {
    const status = await new Promise((resolve, reject) => {
      const u = new URL(`${s.base}/api/telemetry/v1/batch`);
      const req = request(
        { host: u.hostname, port: u.port, path: u.pathname, method: 'POST', headers: { 'Content-Type': 'application/json' } },
        (res) => {
          res.resume();
          resolve(res.statusCode);
        },
      );
      req.on('error', reject);
      req.write('{"pad":"');
      req.write('x'.repeat(5000));
      // Keep the request open: the server must answer before the body ends.
    });
    assert.equal(status, 413);
  });

  test('exactly 4096 bytes is accepted', async () => {
    const json = JSON.stringify(validBatch());
    const body = json + ' '.repeat(4096 - json.length);
    assert.equal((await postBatch(s.base, body)).status, 204);
  });

  test('summary carries the label and sums', async () => {
    const r = await fetch(`${s.base}/api/telemetry/v1/summary?days=30`);
    assert.equal(r.status, 200);
    const j = await r.json();
    assert.equal(j.label, SUMMARY_LABEL);
    assert.equal(j.label, 'Reported by SDKs with telemetry on; not total usage, unique users, or worldwide scans.');
    assert.equal(j.days, 30);
    assert.ok(j.batches >= 3);
    assert.equal(j.sessions.started, j.batches * 12);
    assert.equal(j.time_to_decode_ms.buckets.length, 10);
    assert.ok(j.by_sdk.some((x) => x.sdk === 'swift'));
    assert.ok(j.by_platform.some((x) => x.platform === 'macos'));
    // The -35 day batch is outside a 30-day window.
    assert.ok(!j.by_day.some((d) => d.day === '2026-10-01'));
  });

  test('summary rejects bad days', async () => {
    assert.equal((await fetch(`${s.base}/api/telemetry/v1/summary?days=0`)).status, 400);
    assert.equal((await fetch(`${s.base}/api/telemetry/v1/summary?days=401`)).status, 400);
    assert.equal((await fetch(`${s.base}/api/telemetry/v1/summary?days=abc`)).status, 400);
    assert.equal((await fetch(`${s.base}/api/telemetry/v1/summary`)).status, 200);
  });

  test('GET on batch endpoint is 405', async () => {
    const r = await fetch(`${s.base}/api/telemetry/v1/batch`);
    assert.equal(r.status, 405);
    assert.match(r.headers.get('allow'), /POST/);
  });
});

describe('telemetry rate limits', () => {
  test('per-key limit returns 429 with Retry-After', async () => {
    const s = await startServer({ env: { OPENCKEY_TELEMETRY_RATE_PER_HOUR: '3', OPENCKEY_TELEMETRY_BURST: '3' } });
    try {
      for (let i = 0; i < 3; i++) assert.equal((await postBatch(s.base, validBatch())).status, 204);
      const r = await postBatch(s.base, validBatch());
      assert.equal(r.status, 429);
      assert.ok(Number(r.headers.get('retry-after')) > 0);
      // X-Forwarded-For is ignored unless OPENCKEY_TRUST_PROXY=1.
      const r2 = await postBatch(s.base, validBatch(), { headers: { 'X-Forwarded-For': '198.51.100.7' } });
      assert.equal(r2.status, 429);
    } finally {
      await s.stop();
    }
  });

  test('with a trusted proxy, the first X-Forwarded-For hop is the key', async () => {
    const s = await startServer({
      env: { OPENCKEY_TELEMETRY_RATE_PER_HOUR: '1', OPENCKEY_TELEMETRY_BURST: '1', OPENCKEY_TRUST_PROXY: '1' },
    });
    try {
      const xff = (ip) => ({ headers: { 'X-Forwarded-For': `${ip}, 10.0.0.1` } });
      assert.equal((await postBatch(s.base, validBatch(), xff('198.51.100.1'))).status, 204);
      assert.equal((await postBatch(s.base, validBatch(), xff('198.51.100.1'))).status, 429);
      assert.equal((await postBatch(s.base, validBatch(), xff('198.51.100.2'))).status, 204);
    } finally {
      await s.stop();
    }
  });

  test('global daily cap returns 429', async () => {
    const s = await startServer({ env: { OPENCKEY_TELEMETRY_DAILY_CAP: '2' } });
    try {
      assert.equal((await postBatch(s.base, validBatch())).status, 204);
      assert.equal((await postBatch(s.base, validBatch())).status, 204);
      assert.equal((await postBatch(s.base, validBatch())).status, 429);
      // Invalid batches do not count toward the cap but still get 400.
      assert.equal((await postBatch(s.base, validBatch({ sdk: 'nope' }))).status, 400);
    } finally {
      await s.stop();
    }
  });

  test('salt rotates and buckets reset at the UTC day boundary', async () => {
    let t = NOW;
    const s = await startServer({ now: () => t, env: { OPENCKEY_TELEMETRY_RATE_PER_HOUR: '1', OPENCKEY_TELEMETRY_BURST: '1' } });
    try {
      assert.equal((await postBatch(s.base, validBatch())).status, 204);
      assert.equal((await postBatch(s.base, validBatch())).status, 429);
      t = Date.UTC(2026, 10, 6, 0, 0, 1);
      assert.equal((await postBatch(s.base, validBatch())).status, 204);
    } finally {
      await s.stop();
    }
  });
});

describe('telemetry retention', () => {
  test('aggregates older than 400 days are deleted at startup', async () => {
    let t = NOW;
    const s = await startServer({ now: () => t });
    try {
      const ins = s.app.db.prepare(
        "INSERT INTO telemetry_daily (day, sdk, sdk_version, platform, batches) VALUES (?, 'c', '0.1.0', 'linux', 1)",
      );
      ins.run('2025-10-01'); // exactly 400 days before 2026-11-05: kept
      ins.run('2025-09-30'); // 401 days: removed
      ins.run('2026-11-01');
      const days = () => s.app.db.prepare('SELECT day FROM telemetry_daily ORDER BY day').all().map((r) => r.day);
      s.app.store.purge(Math.floor(NOW / 86_400_000));
      assert.deepEqual(days(), ['2025-10-01', '2026-11-01']);
      // The daily maintenance job runs the purge again when the UTC day changes.
      t += 86_400_000;
      s.app.runMaintenance();
      assert.deepEqual(days(), ['2026-11-01']);
    } finally {
      await s.stop();
    }
  });
});
