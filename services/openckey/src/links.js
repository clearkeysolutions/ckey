// Short links: /r/:id shows an interstitial with the destination host first.

import { randomId } from './ids.js';
import { utcDay } from './days.js';
import { parseStrictJSON, decodeUtf8, JSONSyntaxError } from './strict-json.js';
import { HttpError, readBody, requireJSONContentType, sendBody, sendEmpty, sendJSON } from './http.js';
import { PAGE_CSP, interstitialPage } from './pages.js';

export const MAX_URL_LENGTH = 2048;
const LINK_BODY_LIMIT = 16 * 1024;

/**
 * Validate and normalise a destination URL. Only http/https with a host,
 * no embedded credentials, no whitespace or control characters, and at most
 * MAX_URL_LENGTH characters (before and after normalisation).
 */
export function validateDestination(url) {
  if (typeof url !== 'string' || url.length === 0) throw new HttpError(400, 'url must be a non-empty string');
  if (url.length > MAX_URL_LENGTH) throw new HttpError(400, `url must be at most ${MAX_URL_LENGTH} characters`);
  // eslint-disable-next-line no-control-regex
  if (/[\u0000- \u007f-\u009f]/.test(url)) throw new HttpError(400, 'url must not contain whitespace or control characters');
  let u;
  try {
    u = new URL(url);
  } catch {
    throw new HttpError(400, 'url is not a valid absolute URL');
  }
  if (u.protocol !== 'https:' && u.protocol !== 'http:') throw new HttpError(400, 'url scheme must be http or https');
  if (!u.hostname) throw new HttpError(400, 'url must have a host');
  if (u.username || u.password) throw new HttpError(400, 'url must not contain credentials');
  if (u.href.length > MAX_URL_LENGTH) throw new HttpError(400, `url must be at most ${MAX_URL_LENGTH} characters`);
  return u.href;
}

export async function readJSONObject(req, limit, allowedKeys) {
  requireJSONContentType(req);
  const buf = await readBody(req, limit);
  let body;
  try {
    body = parseStrictJSON(decodeUtf8(buf), { maxDepth: 2 });
  } catch (err) {
    if (err instanceof JSONSyntaxError) throw new HttpError(400, `invalid JSON: ${err.message}`);
    throw err;
  }
  if (typeof body !== 'object' || body === null || Array.isArray(body)) throw new HttpError(400, 'body must be a JSON object');
  for (const k of Object.keys(body)) {
    if (!allowedKeys.includes(k)) throw new HttpError(400, `unknown field ${k}`);
  }
  return body;
}

export function makeLinkHandlers({ db, config, requireAdmin, now }) {
  const insert = db.prepare('INSERT INTO links (id, url, count_enabled, created_at) VALUES (?, ?, ?, ?)');
  const get = db.prepare('SELECT id, url, count_enabled, created_at, disabled_at FROM links WHERE id = ?');
  const disable = db.prepare('UPDATE links SET disabled_at = ? WHERE id = ? AND disabled_at IS NULL');
  const hit = db.prepare(
    `INSERT INTO link_daily (id, day, hits) VALUES (?, ?, 1)
     ON CONFLICT (id, day) DO UPDATE SET hits = hits + 1`,
  );
  const stats = db.prepare('SELECT day, hits FROM link_daily WHERE id = ? ORDER BY day');

  function findOr404(id) {
    const row = get.get(id);
    if (!row) throw new HttpError(404, 'link not found');
    return row;
  }

  async function create(req, res) {
    requireAdmin(req);
    const body = await readJSONObject(req, LINK_BODY_LIMIT, ['url', 'count']);
    if (!Object.hasOwn(body, 'url')) throw new HttpError(400, 'missing field url');
    const url = validateDestination(body.url);
    const count = body.count ?? false;
    if (typeof count !== 'boolean') throw new HttpError(400, 'count must be true or false');
    const createdAt = new Date(now()).toISOString();
    for (let attempt = 0; ; attempt++) {
      const id = randomId();
      try {
        insert.run(id, url, count ? 1 : 0, createdAt);
        sendJSON(res, 201, { id, short_url: `${config.publicBase}/r/${id}` });
        return;
      } catch (err) {
        if (attempt >= 5 || !/UNIQUE|PRIMARY KEY/i.test(String(err.message))) throw err;
      }
    }
  }

  function visit(req, res, { params, url, method }) {
    const row = findOr404(params.id);
    if (row.disabled_at) throw new HttpError(410, 'link disabled');
    // HEAD (link previews, health checks) is never counted.
    if (row.count_enabled && method === 'GET') hit.run(row.id, utcDay(now()));
    const headers = { 'Cache-Control': 'no-store', 'X-Robots-Tag': 'noindex, nofollow' };
    if (url.searchParams.get('go') === '1') {
      sendEmpty(res, 302, { ...headers, Location: row.url });
      return;
    }
    sendBody(res, 200, 'text/html; charset=utf-8', interstitialPage(row.url), {
      ...headers,
      'Content-Security-Policy': PAGE_CSP,
    });
  }

  function getStats(req, res, { params }) {
    requireAdmin(req);
    const row = findOr404(params.id);
    const days = stats.all(row.id).map((r) => ({ day: r.day, hits: Number(r.hits) }));
    sendJSON(res, 200, {
      id: row.id,
      url: row.url,
      count: Boolean(row.count_enabled),
      created_at: row.created_at,
      disabled: Boolean(row.disabled_at),
      total: days.reduce((a, d) => a + d.hits, 0),
      days,
    });
  }

  function remove(req, res, { params }) {
    requireAdmin(req);
    findOr404(params.id);
    disable.run(new Date(now()).toISOString(), params.id);
    sendEmpty(res, 204);
  }

  return { create, visit, getStats, remove };
}
