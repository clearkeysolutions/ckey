// Response helpers, security headers and bounded body reading.

export class HttpError extends Error {
  constructor(status, message, { headers = {}, close = false } = {}) {
    super(message);
    this.status = status;
    this.headers = headers;
    this.close = close;
  }
}

/** Headers sent on every response. Routes may override CSP and CORP. */
export const SECURITY_HEADERS = {
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'X-Frame-Options': 'DENY',
  'Content-Security-Policy': "default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'",
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Resource-Policy': 'same-origin',
  'Permissions-Policy': 'camera=(), microphone=(), geolocation=(), interest-cohort=()',
  'Strict-Transport-Security': 'max-age=31536000; includeSubDomains',
};

export function applySecurityHeaders(res) {
  for (const [k, v] of Object.entries(SECURITY_HEADERS)) res.setHeader(k, v);
}

export function sendJSON(res, status, obj, headers = {}) {
  const body = Buffer.from(JSON.stringify(obj));
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store',
    ...headers,
  });
  res.end(body);
}

export function sendEmpty(res, status, headers = {}) {
  res.writeHead(status, { 'Cache-Control': 'no-store', ...headers });
  res.end();
}

export function sendBody(res, status, contentType, body, headers = {}) {
  const buf = typeof body === 'string' ? Buffer.from(body) : body;
  res.writeHead(status, { 'Content-Type': contentType, 'Content-Length': buf.length, ...headers });
  res.end(buf);
}

export function sendError(res, err) {
  if (res.headersSent) {
    res.destroy();
    return;
  }
  const headers = { ...err.headers };
  if (err.close) headers.Connection = 'close';
  sendJSON(res, err.status, { error: err.message }, headers);
}

/**
 * Require `Content-Type: application/json` (an optional `charset=utf-8` is
 * allowed; any other charset is refused).
 */
export function requireJSONContentType(req) {
  const raw = req.headers['content-type'];
  if (typeof raw !== 'string') throw new HttpError(415, 'Content-Type must be application/json');
  const [type, ...params] = raw.split(';').map((s) => s.trim().toLowerCase());
  if (type !== 'application/json') throw new HttpError(415, 'Content-Type must be application/json');
  for (const p of params) {
    if (p === '') continue;
    const [k, v] = p.split('=').map((s) => s.trim().replace(/^"|"$/g, ''));
    if (k === 'charset' && v !== 'utf-8' && v !== 'utf8') {
      throw new HttpError(415, 'Content-Type charset must be utf-8');
    }
  }
}

/**
 * Throw 413 straight away when the declared Content-Length exceeds `limit`,
 * so an oversize body is never read.
 */
export function checkDeclaredLength(req, limit) {
  const cl = req.headers['content-length'];
  if (cl === undefined) return;
  if (!/^\d+$/.test(cl)) throw new HttpError(400, 'invalid Content-Length');
  if (Number(cl) > limit) throw new HttpError(413, `body exceeds ${limit} bytes`, { close: true });
}

/**
 * Read the request body into a Buffer, never holding more than `limit` bytes.
 * On overflow, stop reading and throw 413 (the connection is closed after the
 * reply, so the unread remainder is discarded).
 */
export function readBody(req, limit) {
  checkDeclaredLength(req, limit);
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    let done = false;
    const finish = (fn, arg) => {
      if (done) return;
      done = true;
      req.off('data', onData);
      req.off('end', onEnd);
      req.off('error', onError);
      req.off('aborted', onAborted);
      fn(arg);
    };
    const onData = (chunk) => {
      size += chunk.length;
      if (size > limit) {
        req.pause();
        finish(reject, new HttpError(413, `body exceeds ${limit} bytes`, { close: true }));
        return;
      }
      chunks.push(chunk);
    };
    const onEnd = () => finish(resolve, Buffer.concat(chunks, size));
    const onError = () => finish(reject, new HttpError(400, 'request body error', { close: true }));
    const onAborted = () => finish(reject, new HttpError(400, 'request aborted', { close: true }));
    req.on('data', onData);
    req.on('end', onEnd);
    req.on('error', onError);
    req.on('aborted', onAborted);
  });
}

/** Client IP: socket address, or the first X-Forwarded-For hop when trusted. */
export function clientIp(req, trustProxy) {
  if (trustProxy) {
    const xff = req.headers['x-forwarded-for'];
    if (typeof xff === 'string') {
      const first = xff.split(',')[0].trim();
      if (first) return first;
    }
  }
  return req.socket.remoteAddress || 'unknown';
}

export function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}
