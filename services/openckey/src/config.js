// Configuration from environment variables. Invalid values fail at startup.

import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

export const SERVICE_DIR = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function int(env, name, def, min, max) {
  const raw = env[name];
  if (raw === undefined || raw === '') return def;
  if (!/^\d+$/.test(raw)) throw new Error(`${name} must be a non-negative integer`);
  const n = Number(raw);
  if (n < min || n > max) throw new Error(`${name} must be between ${min} and ${max}`);
  return n;
}

export function loadConfig(env = process.env) {
  const publicBase = (env.OPENCKEY_PUBLIC_BASE || 'https://openckey.org').replace(/\/+$/, '');
  try {
    const u = new URL(publicBase);
    if (u.protocol !== 'https:' && u.protocol !== 'http:') throw new Error();
  } catch {
    throw new Error('OPENCKEY_PUBLIC_BASE must be an http(s) URL');
  }
  return {
    port: int(env, 'PORT', 8080, 0, 65535),
    host: env.OPENCKEY_HOST || undefined,
    dbPath: env.OPENCKEY_DB || './data/openckey.sqlite',
    publicBase,
    adminTokens: (env.OPENCKEY_ADMIN_TOKENS || '')
      .split(',')
      .map((t) => t.trim())
      .filter(Boolean),
    trustProxy: env.OPENCKEY_TRUST_PROXY === '1',
    sdkDir: resolve(env.OPENCKEY_SDK_DIR || resolve(SERVICE_DIR, '../../sdk/web/dist')),
    publicDir: resolve(SERVICE_DIR, 'public'),
    telemetryRatePerHour: int(env, 'OPENCKEY_TELEMETRY_RATE_PER_HOUR', 30, 1, 1_000_000),
    telemetryBurst: int(env, 'OPENCKEY_TELEMETRY_BURST', 30, 1, 1_000_000),
    telemetryDailyCap: int(env, 'OPENCKEY_TELEMETRY_DAILY_CAP', 200_000, 1, 1_000_000_000),
    rateLimitMaxKeys: int(env, 'OPENCKEY_RATE_LIMIT_MAX_KEYS', 200_000, 100, 100_000_000),
    log: env.OPENCKEY_LOG !== '0',
  };
}
