// Telemetry ingestion, batch schema version 1 (docs/adoption-and-telemetry.md).

import { HIST_BUCKETS, TELEMETRY_COLUMNS, transaction } from './db.js';
import { dayFromNumber, dayNumber, dayNumberOfMs, utcDay } from './days.js';
import { parseStrictJSON, decodeUtf8, JSONSyntaxError } from './strict-json.js';
import {
  HttpError,
  checkDeclaredLength,
  clientIp,
  readBody,
  requireJSONContentType,
  sendEmpty,
  sendJSON,
} from './http.js';

export const MAX_BATCH_BYTES = 4096;
export const MAX_COUNT = 10_000_000;
export const MAX_SUM = 1_000_000_000_000;
export const MAX_FUTURE_DAYS = 2;
export const MAX_PAST_DAYS = 35;
export const RETENTION_DAYS = 400;
export const SUMMARY_LABEL =
  'Reported by SDKs with telemetry on; not total usage, unique users, or worldwide scans.';

const SDKS = new Set(['c', 'swift', 'android', 'web', 'dotnet']);
const PLATFORMS = new Set(['ios', 'macos', 'android', 'web', 'windows', 'linux', 'other']);
const SDK_VERSION_RE = /^0\.[0-9]+\.[0-9]+(-(build|dev)\.[0-9]{1,6})?$/;

const TOP_KEYS = [
  'schema', 'sdk', 'sdk_version', 'platform', 'day',
  'sessions', 'frames', 'decoded', 'time_to_decode_ms', 'frame_ms',
];
const SESSION_KEYS = ['started', 'succeeded', 'failed'];
const DECODED_KEYS = ['p0', 'p0_1', 'p1', 'p2', 'p3'];
const HIST_KEYS = ['count', 'sum', 'buckets'];

export class ValidationError extends Error {}

const bad = (msg) => {
  throw new ValidationError(msg);
};

function isPlainObject(v) {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function exactKeys(obj, keys, path) {
  if (!isPlainObject(obj)) bad(`${path} must be an object`);
  const allowed = new Set(keys);
  for (const k of Object.keys(obj)) {
    if (!allowed.has(k)) bad(`unknown field ${path === '$' ? k : `${path}.${k}`}`);
  }
  for (const k of keys) {
    if (!Object.hasOwn(obj, k)) bad(`missing field ${path === '$' ? k : `${path}.${k}`}`);
  }
}

function intIn(v, max, path) {
  if (typeof v !== 'number' || !Number.isSafeInteger(v) || v < 0 || v > max) {
    bad(`${path} must be an integer from 0 to ${max}`);
  }
  return v === 0 ? 0 : v; // normalise -0
}

function histogram(h, path) {
  exactKeys(h, HIST_KEYS, path);
  const count = intIn(h.count, MAX_COUNT, `${path}.count`);
  const sum = intIn(h.sum, MAX_SUM, `${path}.sum`);
  if (!Array.isArray(h.buckets) || h.buckets.length !== HIST_BUCKETS) {
    bad(`${path}.buckets must be an array of ${HIST_BUCKETS} integers`);
  }
  const buckets = h.buckets.map((b, k) => intIn(b, MAX_COUNT, `${path}.buckets[${k}]`));
  if (buckets.reduce((a, b) => a + b, 0) !== count) bad(`${path}.buckets must add up to ${path}.count`);
  return { count, sum, buckets };
}

/**
 * Validate a parsed batch against schema 1. `todayNumber` is the server's
 * current UTC day number. Returns a normalised batch or throws ValidationError.
 */
export function validateBatch(b, todayNumber) {
  exactKeys(b, TOP_KEYS, '$');
  if (b.schema !== 1) bad('schema must be 1');
  if (typeof b.sdk !== 'string' || !SDKS.has(b.sdk)) bad('sdk is not a known SDK');
  if (typeof b.sdk_version !== 'string' || !SDK_VERSION_RE.test(b.sdk_version)) bad('sdk_version is malformed');
  if (typeof b.platform !== 'string' || !PLATFORMS.has(b.platform)) bad('platform is not a known platform');
  const dn = dayNumber(b.day);
  if (Number.isNaN(dn)) bad('day must be a calendar date YYYY-MM-DD');
  if (dn > todayNumber + MAX_FUTURE_DAYS || dn < todayNumber - MAX_PAST_DAYS) bad('day is outside the accepted window');

  exactKeys(b.sessions, SESSION_KEYS, 'sessions');
  const sessions = Object.fromEntries(SESSION_KEYS.map((k) => [k, intIn(b.sessions[k], MAX_COUNT, `sessions.${k}`)]));
  const frames = intIn(b.frames, MAX_COUNT, 'frames');
  exactKeys(b.decoded, DECODED_KEYS, 'decoded');
  const decoded = Object.fromEntries(DECODED_KEYS.map((k) => [k, intIn(b.decoded[k], MAX_COUNT, `decoded.${k}`)]));

  return {
    day: b.day,
    sdk: b.sdk,
    sdk_version: b.sdk_version,
    platform: b.platform,
    sessions,
    frames,
    decoded,
    time_to_decode_ms: histogram(b.time_to_decode_ms, 'time_to_decode_ms'),
    frame_ms: histogram(b.frame_ms, 'frame_ms'),
  };
}

function batchValues(v) {
  return [
    1,
    v.sessions.started, v.sessions.succeeded, v.sessions.failed,
    v.frames,
    v.decoded.p0, v.decoded.p0_1, v.decoded.p1, v.decoded.p2, v.decoded.p3,
    v.time_to_decode_ms.count, v.time_to_decode_ms.sum, ...v.time_to_decode_ms.buckets,
    v.frame_ms.count, v.frame_ms.sum, ...v.frame_ms.buckets,
  ];
}

export class TelemetryStore {
  constructor(db) {
    this.db = db;
    const cols = TELEMETRY_COLUMNS;
    this.upsert = db.prepare(
      `INSERT INTO telemetry_daily (day, sdk, sdk_version, platform, ${cols.join(', ')})
       VALUES (?, ?, ?, ?, ${cols.map(() => '?').join(', ')})
       ON CONFLICT (day, sdk, sdk_version, platform) DO UPDATE SET
       ${cols.map((c) => `${c} = ${c} + excluded.${c}`).join(', ')}`,
    );
    this.getAccepted = db.prepare('SELECT accepted FROM telemetry_ingest WHERE recv_day = ?');
    this.bumpAccepted = db.prepare(
      `INSERT INTO telemetry_ingest (recv_day, accepted) VALUES (?, 1)
       ON CONFLICT (recv_day) DO UPDATE SET accepted = accepted + 1`,
    );
    const sums = cols.map((c) => `COALESCE(SUM(${c}), 0) AS ${c}`).join(', ');
    this.totals = db.prepare(`SELECT ${sums} FROM telemetry_daily WHERE day >= ? AND day <= ?`);
    this.bySdk = db.prepare(
      `SELECT sdk, SUM(batches) AS batches, SUM(sessions_started) AS sessions_started,
              SUM(sessions_succeeded) AS sessions_succeeded
       FROM telemetry_daily WHERE day >= ? AND day <= ? GROUP BY sdk ORDER BY sdk`,
    );
    this.byPlatform = db.prepare(
      `SELECT platform, SUM(batches) AS batches, SUM(sessions_started) AS sessions_started,
              SUM(sessions_succeeded) AS sessions_succeeded
       FROM telemetry_daily WHERE day >= ? AND day <= ? GROUP BY platform ORDER BY platform`,
    );
    this.byDay = db.prepare(
      `SELECT day, SUM(batches) AS batches, SUM(sessions_started) AS sessions_started,
              SUM(sessions_succeeded) AS sessions_succeeded, SUM(sessions_failed) AS sessions_failed
       FROM telemetry_daily WHERE day >= ? AND day <= ? GROUP BY day ORDER BY day`,
    );
    this.purgeDaily = db.prepare('DELETE FROM telemetry_daily WHERE day < ?');
    this.purgeIngest = db.prepare('DELETE FROM telemetry_ingest WHERE recv_day < ?');
  }

  /** Add a validated batch, unless today's global cap is reached. Returns false when capped. */
  accept(v, recvDay, dailyCap) {
    return transaction(this.db, () => {
      const row = this.getAccepted.get(recvDay);
      if (row && row.accepted >= dailyCap) return false;
      this.upsert.run(v.day, v.sdk, v.sdk_version, v.platform, ...batchValues(v));
      this.bumpAccepted.run(recvDay);
      return true;
    });
  }

  summary(fromDay, toDay) {
    const t = this.totals.get(fromDay, toDay);
    const n = (x) => Number(x ?? 0);
    const hist = (prefix) => ({
      count: n(t[`${prefix}_count`]),
      sum: n(t[`${prefix}_sum`]),
      buckets: Array.from({ length: HIST_BUCKETS }, (_, k) => n(t[`${prefix}_b${k}`])),
    });
    const group = (rows, key) =>
      rows.map((r) => ({
        [key]: r[key],
        batches: n(r.batches),
        sessions_started: n(r.sessions_started),
        sessions_succeeded: n(r.sessions_succeeded),
      }));
    return {
      batches: n(t.batches),
      sessions: {
        started: n(t.sessions_started),
        succeeded: n(t.sessions_succeeded),
        failed: n(t.sessions_failed),
      },
      frames: n(t.frames),
      decoded: {
        p0: n(t.decoded_p0),
        p0_1: n(t.decoded_p0_1),
        p1: n(t.decoded_p1),
        p2: n(t.decoded_p2),
        p3: n(t.decoded_p3),
      },
      time_to_decode_ms: { ...hist('ttd'), bucket_upper_edges: [25, 50, 100, 200, 400, 800, 1600, 3200, 6400, null] },
      frame_ms: { ...hist('frame'), bucket_upper_edges: [2, 5, 10, 20, 40, 80, 160, 320, 640, null] },
      by_sdk: group(this.bySdk.all(fromDay, toDay), 'sdk'),
      by_platform: group(this.byPlatform.all(fromDay, toDay), 'platform'),
      by_day: this.byDay.all(fromDay, toDay).map((r) => ({
        day: r.day,
        batches: n(r.batches),
        sessions_started: n(r.sessions_started),
        sessions_succeeded: n(r.sessions_succeeded),
        sessions_failed: n(r.sessions_failed),
      })),
    };
  }

  /** Delete aggregates older than RETENTION_DAYS. Returns rows removed. */
  purge(todayNumber) {
    const cutoff = dayFromNumber(todayNumber - RETENTION_DAYS);
    const a = this.purgeDaily.run(cutoff).changes;
    const b = this.purgeIngest.run(cutoff).changes;
    return Number(a) + Number(b);
  }
}

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Cross-Origin-Resource-Policy': 'cross-origin',
};

export function makeTelemetryHandlers({ store, limiter, config, now }) {
  async function postBatch(req, res) {
    res.setHeader('Access-Control-Allow-Origin', CORS['Access-Control-Allow-Origin']);
    res.setHeader('Cross-Origin-Resource-Policy', CORS['Cross-Origin-Resource-Policy']);
    // Cheap checks first; an oversize declared body is refused unread.
    checkDeclaredLength(req, MAX_BATCH_BYTES);
    requireJSONContentType(req);
    const rl = limiter.take(clientIp(req, config.trustProxy));
    if (!rl.ok) throw new HttpError(429, 'rate limited', { headers: { 'Retry-After': String(rl.retryAfter) } });

    const buf = await readBody(req, MAX_BATCH_BYTES);
    const t = now();
    let batch;
    try {
      batch = validateBatch(parseStrictJSON(decodeUtf8(buf), { integersOnly: true, maxDepth: 4 }), dayNumberOfMs(t));
    } catch (err) {
      if (err instanceof ValidationError || err instanceof JSONSyntaxError) throw new HttpError(400, err.message);
      throw err;
    }
    if (!store.accept(batch, utcDay(t), config.telemetryDailyCap)) {
      throw new HttpError(429, 'daily capacity reached', { headers: { 'Retry-After': '3600' } });
    }
    sendEmpty(res, 204);
  }

  function preflight(req, res) {
    sendEmpty(res, 204, {
      ...CORS,
      'Access-Control-Allow-Methods': 'POST',
      'Access-Control-Allow-Headers': 'Content-Type',
      'Access-Control-Max-Age': '86400',
      'Cache-Control': 'public, max-age=86400',
    });
  }

  function getSummary(req, res, { url }) {
    const raw = url.searchParams.get('days');
    let days = 30;
    if (raw !== null) {
      if (!/^\d{1,3}$/.test(raw) || Number(raw) < 1 || Number(raw) > RETENTION_DAYS) {
        throw new HttpError(400, `days must be an integer from 1 to ${RETENTION_DAYS}`);
      }
      days = Number(raw);
    }
    const today = dayNumberOfMs(now());
    const from = dayFromNumber(today - days + 1);
    // Batches may be dated up to MAX_FUTURE_DAYS ahead of the server clock.
    const to = dayFromNumber(today + MAX_FUTURE_DAYS);
    sendJSON(
      res,
      200,
      {
        label: SUMMARY_LABEL,
        methodology:
          'Sums of anonymous aggregate batches (schema 1) sent by CKey SDKs in telemetry mode 1. ' +
          'Batches are unauthenticated and cannot be deduplicated; they can be spoofed, replayed or inflated. ' +
          'Treat these figures as a coarse signal. See docs/adoption-and-telemetry.md.',
        days,
        from,
        to,
        ...store.summary(from, to),
      },
      { ...CORS, 'Cache-Control': 'public, max-age=300' },
    );
  }

  return { postBatch, preflight, getSummary };
}
