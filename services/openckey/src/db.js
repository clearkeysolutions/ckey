// SQLite storage (node:sqlite). Holds aggregates, links and texts only.
// Nothing derived from a client IP address is ever written here.

import { DatabaseSync } from 'node:sqlite';
import { mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';

export const HIST_BUCKETS = 10;

/** Additive aggregate columns of telemetry_daily, in a fixed order. */
export const TELEMETRY_COLUMNS = [
  'batches',
  'sessions_started',
  'sessions_succeeded',
  'sessions_failed',
  'frames',
  'decoded_p0',
  'decoded_p0_1',
  'decoded_p1',
  'decoded_p2',
  'decoded_p3',
  'ttd_count',
  'ttd_sum',
  ...Array.from({ length: HIST_BUCKETS }, (_, k) => `ttd_b${k}`),
  'frame_count',
  'frame_sum',
  ...Array.from({ length: HIST_BUCKETS }, (_, k) => `frame_b${k}`),
];

const SCHEMA = `
CREATE TABLE IF NOT EXISTS telemetry_daily (
  day TEXT NOT NULL,
  sdk TEXT NOT NULL,
  sdk_version TEXT NOT NULL,
  platform TEXT NOT NULL,
  ${TELEMETRY_COLUMNS.map((c) => `${c} INTEGER NOT NULL DEFAULT 0`).join(',\n  ')},
  PRIMARY KEY (day, sdk, sdk_version, platform)
) WITHOUT ROWID;

-- Accepted batches per server (receive) UTC day, for the global daily cap.
CREATE TABLE IF NOT EXISTS telemetry_ingest (
  recv_day TEXT PRIMARY KEY,
  accepted INTEGER NOT NULL
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS links (
  id TEXT PRIMARY KEY,
  url TEXT NOT NULL,
  count_enabled INTEGER NOT NULL,
  created_at TEXT NOT NULL,
  disabled_at TEXT
) WITHOUT ROWID;

-- Opt-in per-link per-day counters. No IP, user agent or referrer.
CREATE TABLE IF NOT EXISTS link_daily (
  id TEXT NOT NULL,
  day TEXT NOT NULL,
  hits INTEGER NOT NULL,
  PRIMARY KEY (id, day)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS texts (
  id TEXT PRIMARY KEY,
  title TEXT,
  body TEXT NOT NULL,
  created_at TEXT NOT NULL,
  disabled_at TEXT
) WITHOUT ROWID;
`;

export function openDb(path) {
  let target = path;
  if (path !== ':memory:') {
    target = resolve(path);
    mkdirSync(dirname(target), { recursive: true });
  }
  const db = new DatabaseSync(target);
  db.exec('PRAGMA busy_timeout = 5000;');
  if (target !== ':memory:') db.exec('PRAGMA journal_mode = WAL;');
  db.exec(SCHEMA);
  return db;
}

/** Run `fn` inside a transaction. */
export function transaction(db, fn) {
  db.exec('BEGIN IMMEDIATE');
  try {
    const out = fn();
    db.exec('COMMIT');
    return out;
  } catch (err) {
    db.exec('ROLLBACK');
    throw err;
  }
}
