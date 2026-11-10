#ifndef CKEY_TELEMETRY_H
#define CKEY_TELEMETRY_H

/*
 * CKey telemetry core (specification §9, docs/adoption-and-telemetry.md).
 *
 * This module is optional and self-contained. It is built as its own library
 * (ckey_telemetry); the scanner library does not depend on it and it does not
 * call any other ckey_ function, so builds for OS vendors can leave it out.
 * This header includes ckey/ckey.h only for CKEY_API and ckey_status.
 *
 * WHAT IT DOES
 *   It keeps aggregate counters in process memory and serializes them into the
 *   version-1 batch JSON. The core NEVER does networking: SDK wrappers take a
 *   batch, POST it to CKEY_TELEMETRY_ENDPOINT asynchronously, off the decode
 *   path, and report the HTTP result back with ckey_telemetry_batch_result().
 *   The core never reads a clock either; every time is a caller-supplied
 *   monotonic millisecond value (the UTC day for a batch is also supplied).
 *
 * WHAT IS COLLECTED
 *   Session counts (started, succeeded, failed), the number of processed
 *   frames, decodes per profile, a histogram of time to first decode, a
 *   histogram of per-frame processing time, the SDK name and version, a coarse
 *   platform name and the UTC day the batch was cut.
 *
 * WHAT IS NEVER COLLECTED
 *   Payloads, URLs or any decoded content; user, device or installation IDs;
 *   advertising IDs or fingerprints; location; IP addresses; per-scan event
 *   histories. No API in this header accepts any of these.
 *
 * MODES (canonical numeric values shared by every SDK)
 *   0 CKEY_TELEMETRY_OFF    no counting, nothing sent.
 *   1 CKEY_TELEMETRY_ON     local counting and aggregate batches (DEFAULT).
 *   2 CKEY_TELEMETRY_LOCAL  local counting, never sent.
 *   Switching (ckey_telemetry_set_mode):
 *     any -> 0   purges every counter (totals, unsent delta, in-flight batch)
 *                and the open session. Recording calls become no-ops.
 *     1 -> 2     keeps local totals; drops the unsent delta; the in-flight
 *                batch (if any) is discarded and its result is ignored.
 *     2 -> 1     starts a new unsent delta at zero; counts made in mode 2 are
 *                never sent.
 *     0 -> 1|2   starts counting from zero.
 *     same mode  no effect.
 *   Any other value is rejected with CKEY_INVALID_ARGUMENT; the mode is left
 *   unchanged and is never silently reset to 1.
 *
 * THREADING
 *   All state is process-global and guarded by one internal mutex; every
 *   function may be called from any thread. Recording calls take the mutex and
 *   do a few saturating integer updates; they never allocate, block on I/O or
 *   serialize. Serialization happens only in take_batch/export_state, which
 *   wrappers call off the decode path.
 *
 * COUNTERS
 *   Counters saturate at UINT64_MAX instead of wrapping. A batch never carries
 *   a count above CKEY_TELEMETRY_MAX_COUNT or a sum above CKEY_TELEMETRY_MAX_SUM;
 *   any excess stays in the unsent delta and goes in a later batch.
 */

#include "ckey/ckey.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CKEY_TELEMETRY_ENDPOINT "https://openckey.org/api/telemetry/v1/batch"
#define CKEY_TELEMETRY_SCHEMA 1

/* Default sdk_version reported until ckey_telemetry_set_client() is called:
 * the core's own version (ckey.h), which matches the schema's pattern. */
#ifndef CKEY_TELEMETRY_DEFAULT_VERSION
#define CKEY_TELEMETRY_DEFAULT_VERSION CKEY_SOFTWARE_VERSION
#endif

/* Schema limits for one batch. */
#define CKEY_TELEMETRY_MAX_COUNT 10000000ull
#define CKEY_TELEMETRY_MAX_SUM 1000000000000ull
#define CKEY_TELEMETRY_MAX_BODY_BYTES 4096u

/* A session with no decode fails once now - start >= this many ms. */
#define CKEY_TELEMETRY_SESSION_TIMEOUT_MS 30000u
/* Minimum ms between the last reported batch result and the next batch. */
#define CKEY_TELEMETRY_SEND_INTERVAL_MS 3600000u
/* An unsent delta must be this old before the first batch is cut from it. */
#define CKEY_TELEMETRY_MIN_DELTA_AGE_MS 60000u

#define CKEY_TELEMETRY_BUCKETS 10u
#define CKEY_TELEMETRY_PROFILES 5u

typedef enum ckey_telemetry_mode {
    CKEY_TELEMETRY_OFF = 0,
    CKEY_TELEMETRY_ON = 1,
    CKEY_TELEMETRY_LOCAL = 2
} ckey_telemetry_mode;

/* profile_code values for ckey_telemetry_session_success(); they index
 * ckey_telemetry_counters.decoded and map to the batch keys p0..p3. */
typedef enum ckey_telemetry_profile {
    CKEY_TELEMETRY_PROFILE_P0 = 0,   /* "p0"   */
    CKEY_TELEMETRY_PROFILE_P0_1 = 1, /* "p0_1" */
    CKEY_TELEMETRY_PROFILE_P1 = 2,   /* "p1"   */
    CKEY_TELEMETRY_PROFILE_P2 = 3,   /* "p2"   */
    CKEY_TELEMETRY_PROFILE_P3 = 4    /* "p3"   */
} ckey_telemetry_profile;

/* Histogram buckets use INCLUSIVE upper edges: a value v goes in the first
 * bucket i with v <= edge[i]; the last bucket holds everything above the
 * ninth edge. So 25 ms is in time_to_decode bucket 0 and 26 ms in bucket 1.
 *   time_to_decode_ms: 25, 50, 100, 200, 400, 800, 1600, 3200, 6400, inf
 *   frame_ms:          2, 5, 10, 20, 40, 80, 160, 320, 640, inf
 * Invariant: the buckets add up to count. */
typedef struct ckey_telemetry_histogram {
    uint64_t count;
    uint64_t sum;
    uint64_t buckets[CKEY_TELEMETRY_BUCKETS];
} ckey_telemetry_histogram;

typedef struct ckey_telemetry_counters {
    uint64_t sessions_started;
    uint64_t sessions_succeeded;
    uint64_t sessions_failed;
    uint64_t frames;
    uint64_t decoded[CKEY_TELEMETRY_PROFILES]; /* indexed by ckey_telemetry_profile */
    ckey_telemetry_histogram time_to_decode_ms;
    ckey_telemetry_histogram frame_ms;
} ckey_telemetry_counters;

/* ---- Configuration --------------------------------------------------- */

/* Sets the mode (0, 1 or 2) and applies the switching table above.
 * Returns CKEY_INVALID_ARGUMENT, mode unchanged, for any other value. */
CKEY_API ckey_status ckey_telemetry_set_mode(int mode);
CKEY_API int ckey_telemetry_get_mode(void);

/* Sets the client identity reported in batches. All three are required and
 * validated together; on any error nothing changes.
 *   sdk:         "c", "swift", "android", "web" or "dotnet"   (default "c")
 *   sdk_version: ^0\.[0-9]+\.[0-9]+(-(build|dev)\.[0-9]{1,6})?$, at most 63
 *                bytes                    (default CKEY_TELEMETRY_DEFAULT_VERSION)
 *   platform:    "ios", "macos", "android", "web", "windows", "linux" or
 *                "other"                                      (default "other")
 * The identity is configuration, not a counter: mode changes keep it. */
CKEY_API ckey_status ckey_telemetry_set_client(const char *sdk, const char *sdk_version,
                                               const char *platform);

/* ---- Recording (no-ops in mode 0) -------------------------------------
 *
 * now_ms is any monotonic millisecond clock of the caller's choice, used
 * consistently. A value earlier than the session start counts as 0 ms elapsed.
 *
 * Session state machine:
 *   IDLE --begin/frame/success--> OPEN --success--> DONE
 *   OPEN --end, begin, or (now - start >= 30 s at the next call)--> failed
 *   DONE --end--> IDLE (no further count); DONE --begin--> new OPEN session
 *
 * - session_begin: the host started scanning. An OPEN session is first ended
 *   as failed (the host restarted without a decode). started += 1.
 * - frame: counts one processed frame and its processing time. If the OPEN
 *   session has timed out it is ended as failed. If no session is open (IDLE,
 *   or just timed out) a new session implicitly begins at now_ms. In DONE the
 *   frame is counted as a frame only: per the normative doc, a session
 *   succeeds at its FIRST valid decode and later frames/decodes of the same
 *   session are not counted, so a new session begins only with the next
 *   session_begin (or after session_end, at the next frame).
 * - session_success: the first valid decode. Records time to decode
 *   (now_ms - session start), succeeded += 1, decoded[profile] += 1, and moves
 *   the session to DONE. A timed-out OPEN session is first ended as failed;
 *   with no OPEN session, one implicitly begins at now_ms (time to decode 0).
 *   In DONE it is ignored. Returns CKEY_INVALID_ARGUMENT, recording nothing,
 *   for a profile_code outside 0..4.
 * - session_end: the host stopped scanning. An OPEN session fails; a DONE
 *   session just closes.
 * Timeouts are detected lazily, at the next recording call.
 * The outcome of a session is counted in the delta of the mode current when
 * the outcome happens. */
CKEY_API void ckey_telemetry_session_begin(uint64_t now_ms);
CKEY_API void ckey_telemetry_frame(uint64_t now_ms, uint32_t processing_ms);
CKEY_API ckey_status ckey_telemetry_session_success(uint64_t now_ms, uint32_t profile_code);
CKEY_API void ckey_telemetry_session_end(uint64_t now_ms);

/* Local totals since counting started (modes 1 and 2; all zero in mode 0).
 * CKEY_INVALID_ARGUMENT if out is NULL. */
CKEY_API ckey_status ckey_telemetry_snapshot(ckey_telemetry_counters *out);

/* ---- Batching (mode 1 only; the wrapper does the HTTP) ----------------
 *
 * Wrapper loop, e.g. on a timer and when the app moves to the background:
 *   if (ckey_telemetry_should_send(now)) {
 *       ckey_telemetry_take_batch(utc_day, buf, sizeof buf, &len, &id);
 *       ... POST buf (Content-Type: application/json) asynchronously ...
 *       ckey_telemetry_batch_result(id, http_status_or_0, now_after);
 *   }
 * Every taken batch must be reported exactly once; until then no new batch
 * can be taken. */

/* Non-zero only when: mode is 1, the unsent delta is non-empty, no batch is
 * in flight, at least CKEY_TELEMETRY_SEND_INTERVAL_MS have passed since the
 * now_ms of the last ckey_telemetry_batch_result (whatever its outcome, so a
 * failed send is retried an hour later and there is never more than one batch
 * per hour; the first batch of a process has no such wait), and the delta has
 * existed for
 * at least CKEY_TELEMETRY_MIN_DELTA_AGE_MS (so the first batch of a process
 * is not cut after a single scan; deltas restored by import or by a failed
 * send are not subject to this age). */
CKEY_API int ckey_telemetry_should_send(uint64_t now_ms);

/* Serializes the unsent delta as the compact version-1 batch JSON (keys in
 * the documented order, NUL-terminated) and moves it to "in flight".
 *   utc_day:      "YYYY-MM-DD", a valid calendar date (supplied by the caller).
 *   out_json/cap: output buffer; out_json may be NULL only when cap is 0.
 *   out_len:      receives strlen of the JSON (excluding the NUL).
 *   out_batch_id: receives a non-zero id for ckey_telemetry_batch_result.
 * Returns CKEY_INSUFFICIENT_BUFFER with *out_len = required length (cap must
 * be at least *out_len + 1) and nothing moved. Returns CKEY_OK with
 * *out_len = 0 and *out_batch_id = 0 when there is nothing to send (mode is
 * not 1, delta empty, or a batch already in flight). The cadence is NOT
 * checked here; call ckey_telemetry_should_send first. Values above the schema
 * maxima are clamped and the excess stays in the unsent delta. The body is
 * always well under CKEY_TELEMETRY_MAX_BODY_BYTES. */
CKEY_API ckey_status ckey_telemetry_take_batch(const char *utc_day, char *out_json, size_t cap,
                                               size_t *out_len, uint64_t *out_batch_id);

/* Reports the outcome of a taken batch.
 *   http_status 200..299: accepted; the batch is dropped.
 *   429, 0 (network error, timeout, no connectivity), 1xx, 3xx, 5xx: kept;
 *       the batch is merged back into the unsent delta for a later attempt.
 *   400, 413 and every other 4xx: rejected; the batch is dropped.
 * Returns CKEY_INVALID_ARGUMENT, changing nothing, when batch_id is not the
 * batch currently in flight (for example after a mode switch made it stale:
 * such a batch is never merged back or resent) or http_status is outside
 * 0..999. now_ms (same clock as the recording calls) starts the hourly
 * cadence interval for every accepted report, success or failure. */
CKEY_API ckey_status ckey_telemetry_batch_result(uint64_t batch_id, int http_status, uint64_t now_ms);

/* ---- Optional persistence (host option, independent of the mode) -------
 *
 * The state holds the local totals and the unsent delta. It never holds the
 * mode, the client identity, the in-flight batch or the open session. Format
 * (compact JSON, keys in this exact order):
 *   {"state":1,"totals":C,"unsent":C}
 * where C is {"sessions":{...},"frames":N,"decoded":{...},
 * "time_to_decode_ms":{...},"frame_ms":{...}} with the batch field layout.
 *
 * export: NUL-terminated into out/cap; *len = strlen. CKEY_INSUFFICIENT_BUFFER
 * with *len = required length when cap < *len + 1. In mode 0 it exports an
 * all-zero state, so a host that stores it also forgets purged counts.
 *
 * import: ADDS the stored counts to the current ones (saturating); call it
 * once, at startup. In mode 2 only the totals are imported (mode-2 counts are
 * never sent). Rejected with CKEY_INVALID_ARGUMENT in mode 0, for NULL, and
 * for anything that is not exactly the format above: unknown, missing or
 * reordered keys, non-decimal or out-of-range numbers, buckets that do not add
 * up to count, trailing data, or input over 16 KiB. Whitespace between tokens
 * is allowed. On rejection the state is unchanged. Import never changes the
 * mode and never re-enables sending. */
CKEY_API ckey_status ckey_telemetry_export_state(char *out, size_t cap, size_t *len);
CKEY_API ckey_status ckey_telemetry_import_state(const char *json);

/* TEST ONLY. Restores defaults: mode 1, default client, all counters, the
 * delta, the in-flight batch, the session and the cadence cleared. Not for
 * production use. */
CKEY_API void ckey_telemetry_reset_for_testing(void);

#ifdef __cplusplus
}
#endif

#endif
