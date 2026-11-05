// In-memory token-bucket rate limiter keyed by HMAC-SHA256(salt, client IP).
//
// - The salt is 32 random bytes held only in memory and regenerated at each
//   UTC day boundary; all buckets are dropped at the same time, so keys from
//   different days cannot be linked.
// - Neither the IP nor the key is persisted or logged.

import { createHmac, randomBytes } from 'node:crypto';
import { utcDay } from './days.js';

export class RateLimiter {
  #salt;
  #day;
  #buckets = new Map();

  constructor({ ratePerHour, burst, maxKeys = 200_000, now = Date.now }) {
    this.ratePerMs = ratePerHour / 3_600_000;
    this.burst = burst;
    this.maxKeys = maxKeys;
    this.now = now;
    this.#rotate();
  }

  #rotate() {
    this.#salt = randomBytes(32);
    this.#day = utcDay(this.now());
    this.#buckets.clear();
  }

  /** Rotate the salt when the UTC day has changed. Called on every use. */
  maybeRotate() {
    if (utcDay(this.now()) !== this.#day) this.#rotate();
  }

  get size() {
    return this.#buckets.size;
  }

  #key(ip) {
    return createHmac('sha256', this.#salt).update(String(ip)).digest('base64url');
  }

  #refill(b, t) {
    b.tokens = Math.min(this.burst, b.tokens + (t - b.ts) * this.ratePerMs);
    b.ts = t;
  }

  /** Drop buckets that have refilled completely (they carry no state). */
  prune() {
    const t = this.now();
    for (const [k, b] of this.#buckets) {
      this.#refill(b, t);
      if (b.tokens >= this.burst) this.#buckets.delete(k);
    }
  }

  /** Take one token. Returns `{ ok: true }` or `{ ok: false, retryAfter: seconds }`. */
  take(ip) {
    this.maybeRotate();
    const t = this.now();
    const k = this.#key(ip);
    let b = this.#buckets.get(k);
    if (!b) {
      if (this.#buckets.size >= this.maxKeys) {
        this.prune();
        // Memory bound: fail open rather than grow without limit. The global
        // daily cap still bounds what is accepted.
        if (this.#buckets.size >= this.maxKeys) this.#buckets.clear();
      }
      b = { tokens: this.burst, ts: t };
      this.#buckets.set(k, b);
    } else {
      this.#refill(b, t);
    }
    if (b.tokens >= 1) {
      b.tokens -= 1;
      return { ok: true };
    }
    return { ok: false, retryAfter: Math.max(1, Math.ceil((1 - b.tokens) / this.ratePerMs / 1000)) };
  }
}
