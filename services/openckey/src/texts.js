// Hosted text canvas: /t/:id renders escaped text, /t/:id.txt serves it raw.

import { randomId } from './ids.js';
import { HttpError, sendBody, sendEmpty, sendJSON } from './http.js';
import { readJSONObject } from './links.js';
import { PAGE_CSP, textPage } from './pages.js';

export const MAX_TEXT_BYTES = 64 * 1024;
export const MAX_TITLE_CHARS = 200;
// JSON escaping can expand text up to ~6x; the decoded text is checked separately.
const TEXT_BODY_LIMIT = 512 * 1024;
// eslint-disable-next-line no-control-regex
const BAD_CONTROLS = /[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/;

export function makeTextHandlers({ db, config, requireAdmin, now }) {
  const insert = db.prepare('INSERT INTO texts (id, title, body, created_at) VALUES (?, ?, ?, ?)');
  const get = db.prepare('SELECT id, title, body, disabled_at FROM texts WHERE id = ?');
  // Deleting purges the content but keeps the id reserved (410, never reused).
  const disable = db.prepare("UPDATE texts SET disabled_at = ?, body = '', title = NULL WHERE id = ? AND disabled_at IS NULL");

  function findLive(id) {
    const row = get.get(id);
    if (!row) throw new HttpError(404, 'text not found');
    if (row.disabled_at) throw new HttpError(410, 'text deleted');
    return row;
  }

  async function create(req, res) {
    requireAdmin(req);
    const body = await readJSONObject(req, TEXT_BODY_LIMIT, ['text', 'title']);
    const { text } = body;
    if (typeof text !== 'string' || text.length === 0) throw new HttpError(400, 'text must be a non-empty string');
    if (!text.isWellFormed()) throw new HttpError(400, 'text must be valid Unicode');
    if (Buffer.byteLength(text, 'utf8') > MAX_TEXT_BYTES) throw new HttpError(400, `text must be at most ${MAX_TEXT_BYTES} bytes of UTF-8`);
    if (BAD_CONTROLS.test(text)) throw new HttpError(400, 'text must not contain control characters other than tab and line breaks');
    let title = null;
    if (body.title !== undefined && body.title !== null) {
      if (typeof body.title !== 'string') throw new HttpError(400, 'title must be a string');
      title = body.title.trim();
      if (!title.isWellFormed() || /[\u0000-\u001f\u007f]/.test(title)) throw new HttpError(400, 'title must be a single line of text');
      if (title.length > MAX_TITLE_CHARS) throw new HttpError(400, `title must be at most ${MAX_TITLE_CHARS} characters`);
      if (title === '') title = null;
    }
    const createdAt = new Date(now()).toISOString();
    for (let attempt = 0; ; attempt++) {
      const id = randomId();
      try {
        insert.run(id, title, text, createdAt);
        sendJSON(res, 201, { id, url: `${config.publicBase}/t/${id}`, text_url: `${config.publicBase}/t/${id}.txt` });
        return;
      } catch (err) {
        if (attempt >= 5 || !/UNIQUE|PRIMARY KEY/i.test(String(err.message))) throw err;
      }
    }
  }

  function show(req, res, { params }) {
    const row = findLive(params.id);
    const headers = { 'Cache-Control': 'no-cache', 'X-Robots-Tag': 'noindex, nofollow', 'Content-Security-Policy': PAGE_CSP };
    if (params.ext === '.txt') {
      sendBody(res, 200, 'text/plain; charset=utf-8', row.body, {
        ...headers,
        'Content-Security-Policy': "default-src 'none'; frame-ancestors 'none'",
        'Content-Disposition': 'inline',
      });
      return;
    }
    sendBody(res, 200, 'text/html; charset=utf-8', textPage(row.id, row.title, row.body), headers);
  }

  function remove(req, res, { params }) {
    requireAdmin(req);
    if (!get.get(params.id)) throw new HttpError(404, 'text not found');
    disable.run(new Date(now()).toISOString(), params.id);
    sendEmpty(res, 204);
  }

  return { create, show, remove };
}
