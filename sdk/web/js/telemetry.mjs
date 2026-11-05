// Telemetry sender for the CKey Web SDK (docs/adoption-and-telemetry.md).
// Pure JavaScript; the counters, batching, cadence and mode rules all live in
// the C core (include/ckey/telemetry.h). This module only does what the core
// never does: read clocks and POST a batch, asynchronously, off the decode
// path, then report the HTTP result back.

export const CKEY_TELEMETRY_ENDPOINT = 'https://openckey.org/api/telemetry/v1/batch';

/** How often an armed sender asks the core whether a batch is due. */
export const CHECK_INTERVAL_MS = 60_000;
/** A send that has not finished after this long counts as a network error. */
export const SEND_TIMEOUT_MS = 30_000;

/** Monotonic milliseconds (performance.now()); Date.now() only as a last resort. */
export function monotonicNow() {
  const p = globalThis.performance;
  return p && typeof p.now === 'function' ? p.now() : Date.now();
}

/** The UTC day "YYYY-MM-DD" for a batch. */
export function utcDay(date = new Date()) {
  return date.toISOString().slice(0, 10);
}

/**
 * Drives one core's batch loop.
 *
 * core: {
 *   shouldSend(nowMs) -> boolean,
 *   takeBatch(utcDay) -> { json: string, id: number } | null,
 *   batchResult(id, httpStatus, nowMs) -> boolean     // false: stale batch, ignored
 * }
 * fetch: a fetch-compatible function, or null to never send (counting is
 * unaffected; set telemetry = 2 for local-only counting instead).
 */
export class TelemetrySender {
  constructor({
    core,
    fetch = globalThis.fetch ? globalThis.fetch.bind(globalThis) : null,
    endpoint = CKEY_TELEMETRY_ENDPOINT,
    now = monotonicNow,
    day = () => utcDay(),
    checkIntervalMs = CHECK_INTERVAL_MS,
    timeoutMs = SEND_TIMEOUT_MS,
    timers = globalThis,
  } = {}) {
    if (!core) throw new TypeError('TelemetrySender needs a core');
    this.core = core;
    this.fetch = fetch;
    this.endpoint = endpoint;
    this.now = now;
    this.day = day;
    this.checkIntervalMs = checkIntervalMs;
    this.timeoutMs = timeoutMs;
    this.timers = timers;
    this.timer = null;
    this.inFlight = null;
  }

  /** Starts periodic checks (idempotent). The timer never keeps Node alive. */
  arm() {
    if (this.timer !== null || !this.fetch) return;
    this.timer = this.timers.setInterval(() => void this.trySend(), this.checkIntervalMs);
    if (this.timer && typeof this.timer.unref === 'function') this.timer.unref();
  }

  disarm() {
    if (this.timer !== null) this.timers.clearInterval(this.timer);
    this.timer = null;
  }

  /** Schedules a send attempt on a later task, so callers never wait on it. */
  trySendSoon() {
    if (!this.fetch) return;
    const t = this.timers.setTimeout(() => void this.trySend(), 0);
    if (t && typeof t.unref === 'function') t.unref();
  }

  /**
   * Sends one batch if the core says one is due. Never throws or rejects.
   * Resolves to the HTTP status (0 for a network error), or null when
   * nothing was sent.
   */
  trySend() {
    if (this.inFlight) return this.inFlight;
    if (!this.fetch) return Promise.resolve(null);
    let batch;
    try {
      if (!this.core.shouldSend(this.now())) return Promise.resolve(null);
      batch = this.core.takeBatch(this.day());
    } catch {
      return Promise.resolve(null);
    }
    if (!batch || !batch.id) return Promise.resolve(null);
    this.inFlight = this.#post(batch).finally(() => {
      this.inFlight = null;
    });
    return this.inFlight;
  }

  async #post(batch) {
    let status = 0;
    try {
      const init = {
        method: 'POST',
        keepalive: true,
        headers: { 'content-type': 'application/json' },
        body: batch.json,
        // No cookies and no Referer: the page URL is not telemetry.
        credentials: 'omit',
        referrerPolicy: 'no-referrer',
      };
      if (typeof AbortSignal !== 'undefined' && typeof AbortSignal.timeout === 'function') {
        init.signal = AbortSignal.timeout(this.timeoutMs);
      }
      const res = await this.fetch(this.endpoint, init);
      status = res && Number.isInteger(res.status) && res.status >= 0 && res.status <= 999 ? res.status : 0;
    } catch {
      status = 0; // offline, blocked, timed out: the core keeps the batch for later
    }
    try {
      this.core.batchResult(batch.id, status, this.now());
    } catch {
      /* the result of a stale batch is ignored by design */
    }
    return status;
  }
}

/**
 * Tries a send when the page is hidden (tab switched, app backgrounded,
 * page being unloaded). keepalive lets the request outlive the page.
 * Returns a function that removes the listeners.
 */
export function installBackgroundSend(sender, doc = globalThis.document, win = globalThis.window) {
  const onVisibility = () => {
    if (doc.visibilityState === 'hidden') void sender.trySend();
  };
  const onPageHide = () => void sender.trySend();
  if (doc && typeof doc.addEventListener === 'function') doc.addEventListener('visibilitychange', onVisibility);
  if (win && typeof win.addEventListener === 'function') win.addEventListener('pagehide', onPageHide);
  return () => {
    if (doc && typeof doc.removeEventListener === 'function') doc.removeEventListener('visibilitychange', onVisibility);
    if (win && typeof win.removeEventListener === 'function') win.removeEventListener('pagehide', onPageHide);
  };
}
