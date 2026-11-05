import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { loadConfig } from '../src/config.js';
import { createApp } from '../src/app.js';

export const TOKEN = 'test-admin-token-0123456789abcdef';
// Fixed server clock: 2026-11-05T12:00:00Z.
export const NOW = Date.UTC(2026, 10, 5, 12, 0, 0);

/** Start a server on port 0 with a temp sqlite file. */
export async function startServer({ env = {}, now = () => NOW, sdkDir } = {}) {
  const dir = mkdtempSync(join(tmpdir(), 'openckey-test-'));
  const config = loadConfig({
    OPENCKEY_DB: join(dir, 'test.sqlite'),
    OPENCKEY_ADMIN_TOKENS: `other-token-xyz, ${TOKEN}`,
    OPENCKEY_PUBLIC_BASE: 'https://openckey.example',
    OPENCKEY_SDK_DIR: sdkDir ?? join(dir, 'no-sdk-here'),
    OPENCKEY_LOG: '0',
    ...env,
  });
  const lines = [];
  const app = createApp({ config, now, logger: (l) => lines.push(l) });
  await new Promise((r) => app.server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${app.server.address().port}`;
  return {
    app,
    base,
    lines,
    dir,
    async stop() {
      await app.close({ graceMs: 100 });
      rmSync(dir, { recursive: true, force: true });
    },
  };
}

export function validBatch(overrides = {}) {
  return {
    schema: 1,
    sdk: 'swift',
    sdk_version: '0.1.0-build.61',
    platform: 'ios',
    day: '2026-11-05',
    sessions: { started: 12, succeeded: 10, failed: 2 },
    frames: 3120,
    decoded: { p0: 7, p0_1: 1, p1: 1, p2: 1, p3: 0 },
    time_to_decode_ms: { count: 10, sum: 4210, buckets: [0, 1, 3, 4, 1, 1, 0, 0, 0, 0] },
    frame_ms: { count: 3120, sum: 40110, buckets: [10, 400, 2000, 600, 100, 10, 0, 0, 0, 0] },
    ...overrides,
  };
}

export function postBatch(base, body, { contentType = 'application/json', headers = {} } = {}) {
  return fetch(`${base}/api/telemetry/v1/batch`, {
    method: 'POST',
    headers: { 'Content-Type': contentType, ...headers },
    body: typeof body === 'string' ? body : JSON.stringify(body),
  });
}

export function admin(base, path, { method = 'POST', body, token = TOKEN, headers = {} } = {}) {
  const h = { ...headers };
  if (token !== null) h.Authorization = `Bearer ${token}`;
  if (body !== undefined) h['Content-Type'] = 'application/json';
  return fetch(`${base}${path}`, {
    method,
    headers: h,
    body: body === undefined ? undefined : typeof body === 'string' ? body : JSON.stringify(body),
    redirect: 'manual',
  });
}
