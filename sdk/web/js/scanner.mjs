// Continuous-frame scanner helper. Pure JavaScript over injected functions,
// so its telemetry session logic is unit-tested without WebAssembly.
//
// Session semantics (docs/adoption-and-telemetry.md):
//   first processed frame -> session_begin
//   every processed frame -> frame(now, processing ms)
//   first valid decode    -> session_success(now, profile); later frames are
//                            skipped until reset() ("scan again")
//   stop()                -> session_end (a session without a decode fails),
//                            then a background send attempt

/**
 * deps: {
 *   decode(frame) -> null | result with a numeric `profileCode` (0..4),
 *   telemetry: { begin(now), frame(now, ms), success(now, profileCode), end(now) },
 *   now() -> monotonic ms,
 *   sender: { arm(), trySendSoon() } | null
 * }
 */
export function makeScanner(deps, { onResult, onError } = {}) {
  if (onResult !== undefined && typeof onResult !== 'function') throw new TypeError('onResult must be a function');
  const { decode, telemetry, now, sender } = deps;
  let active = false; // a session is open (or done, awaiting reset/stop)
  let done = false; // the current session has decoded
  let last = null;

  const safe = (fn) => {
    try {
      fn();
    } catch {
      /* telemetry never breaks scanning */
    }
  };

  return {
    /** Decodes one frame (ImageData or { width, height, data } RGBA). Returns the result or null. */
    processFrame(frame) {
      if (done) return null;
      if (!active) {
        active = true;
        safe(() => telemetry.begin(now()));
        if (sender) safe(() => sender.arm());
      }
      const t0 = now();
      let result = null;
      try {
        result = decode(frame);
      } catch (err) {
        const t1 = now();
        safe(() => telemetry.frame(t1, Math.max(0, t1 - t0)));
        if (onError) onError(err);
        else throw err;
        return null;
      }
      const t1 = now();
      safe(() => telemetry.frame(t1, Math.max(0, t1 - t0)));
      if (!result) return null;
      done = true;
      last = result;
      safe(() => telemetry.success(t1, result.profileCode));
      if (onResult) onResult(publicResult(result));
      return publicResult(result);
    },
    /** Scan again after a decode: begins a new session at the next frame. */
    reset() {
      if (done) {
        // The core moves DONE -> a new OPEN session at the next begin.
        done = false;
        active = false;
        last = null;
      }
    },
    /** The host stopped scanning (camera closed). Ends the session. */
    stop() {
      if (active) safe(() => telemetry.end(now()));
      active = false;
      done = false;
      last = null;
      if (sender) safe(() => sender.trySendSoon());
    },
    get decoded() {
      return done ? publicResult(last) : null;
    },
    get scanning() {
      return active && !done;
    },
  };
}

function publicResult(r) {
  if (!r) return null;
  const { profileCode, ...rest } = r; // eslint-disable-line no-unused-vars
  return rest;
}
