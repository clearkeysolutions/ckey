import { randomBytes } from 'node:crypto';

const ALPHABET = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
export const ID_LENGTH = 7;
export const ID_RE = /^[0-9A-Za-z]{7}$/;

/** Uniform random base62 id (62^7 ≈ 3.5e12), using rejection sampling. */
export function randomId(len = ID_LENGTH) {
  let out = '';
  while (out.length < len) {
    for (const b of randomBytes(len * 2)) {
      if (b >= 248) continue; // 248 = 4 * 62; avoids modulo bias
      out += ALPHABET[b % 62];
      if (out.length === len) break;
    }
  }
  return out;
}
