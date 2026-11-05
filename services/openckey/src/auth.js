import { createHash, timingSafeEqual } from 'node:crypto';
import { HttpError } from './http.js';

const digest = (s) => createHash('sha256').update(s, 'utf8').digest();

/**
 * Returns a function that throws 401 unless the request carries
 * `Authorization: Bearer <token>` for one of `tokens`.
 * Tokens are compared as SHA-256 digests with timingSafeEqual, and every
 * configured token is checked, so timing reveals neither length nor which one
 * matched. With no tokens configured, admin routes are always refused.
 */
export function makeAdminAuth(tokens) {
  const digests = tokens.map(digest);
  return function requireAdmin(req) {
    const h = req.headers.authorization;
    const m = typeof h === 'string' ? /^Bearer +(\S+)\s*$/i.exec(h) : null;
    let ok = false;
    if (m) {
      const d = digest(m[1]);
      for (const t of digests) ok = timingSafeEqual(d, t) || ok;
    }
    if (!ok) {
      throw new HttpError(401, 'unauthorized', { headers: { 'WWW-Authenticate': 'Bearer realm="openckey"' } });
    }
  };
}
