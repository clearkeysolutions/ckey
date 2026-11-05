# Adoption and telemetry

This document is normative for every CKey SDK and for the openckey.org ingestion service. It implements specification §9.

## Modes

| Value | Name | Counts locally | Sends to openckey.org |
|---|---|---|---|
| `0` | `CKEY_TELEMETRY_OFF` | no | no |
| `1` | `CKEY_TELEMETRY_ON` | yes | yes, aggregate batches (**default**) |
| `2` | `CKEY_TELEMETRY_LOCAL` | yes | never |

Every SDK uses these numbers. Each language may also offer named constants. Setting any other value is a configuration error: `CKEY_INVALID_ARGUMENT` in C, and the language's own error elsewhere. The mode is never silently reset to 1.

Opting out takes one line, for example `CKey.telemetry = 0` (Swift, Kotlin, JS) or `ckey_telemetry_set_mode(0)` (C).

### Switching modes

| From → to | Effect |
|---|---|
| any → `0` | Stops counting. **Purges** all counters, including unsent ones. Nothing more is sent. |
| `1` → `2` | Keeps local totals. **Drops** the unsent delta, so it can never be sent later. If a batch is in flight, its result is ignored. |
| `2` → `1` | Starts a new unsent delta at zero. Counts made while in mode 2 are never sent. |
| `0` → `1` or `2` | Starts counting from zero. |

Persisting counters across app restarts is a separate host option (`ckey_telemetry_export_state` / `ckey_telemetry_import_state`). It is off unless the host stores the exported state. Importing never re-enables sending, and the mode is not part of the state.

## What a scan attempt is

In continuous camera mode, the unit is a **scan session**, not a frame:

- **Starts:** when the host starts scanning (`ckey_telemetry_session_begin`), for example when the camera view appears.
- **Succeeds:** at the first valid decode (`ckey_telemetry_session_success`). Later frames in the same session are not counted.
- **Fails:** when the host stops scanning without a decode (`ckey_telemetry_session_end`), or when 30 seconds pass with no decode. On timeout, the next frame implicitly starts a new session.
- **Frames:** processed frames are counted separately (`ckey_telemetry_frame`), together with their processing time.

## Collected fields

Only counts and coarse durations are collected:

- session counts (started, succeeded, failed);
- the number of frames processed;
- the number of decodes by profile;
- histograms of time to first decode and of per-frame processing time;
- the SDK name and version;
- a coarse platform name;
- the UTC day the batch was cut.

The SDK **never** collects:

- payloads, URLs or any decoded content;
- user, device or installation IDs;
- advertising IDs or fingerprints;
- location;
- IP addresses (beyond what the network itself exposes);
- per-scan event histories.

Telemetry never runs on the decode path. Recording is a few atomic counter updates. Sending is asynchronous, batched, and never blocks or delays scanning. A failed send keeps the delta for the next attempt, and an offline device simply accumulates counts.

### Send cadence

At most one batch per hour per app process, and only when there is something to send. The SDK also tries a send when the app moves to the background. One request carries one batch, so there is never one request per scan.

## Batch schema, version 1

`POST https://openckey.org/api/telemetry/v1/batch` with `Content-Type: application/json` and a body of at most 4,096 bytes:

```json
{
  "schema": 1,
  "sdk": "swift",
  "sdk_version": "0.1.0-build.61",
  "platform": "ios",
  "day": "2026-11-05",
  "sessions": { "started": 12, "succeeded": 10, "failed": 2 },
  "frames": 3120,
  "decoded": { "p0": 7, "p0_1": 1, "p1": 1, "p2": 1, "p3": 0 },
  "time_to_decode_ms": { "count": 10, "sum": 4210, "buckets": [0, 1, 3, 4, 1, 1, 0, 0, 0, 0] },
  "frame_ms": { "count": 3120, "sum": 40110, "buckets": [10, 400, 2000, 600, 100, 10, 0, 0, 0, 0] }
}
```

Field rules (the service rejects anything else with `400`):

| Field | Rule |
|---|---|
| `schema` | exactly `1` |
| `sdk` | one of `c`, `swift`, `android`, `web`, `dotnet` |
| `sdk_version` | matches `^0\.[0-9]+\.[0-9]+(-(build\|dev)\.[0-9]{1,6})?$` |
| `platform` | one of `ios`, `macos`, `android`, `web`, `windows`, `linux`, `other` |
| `day` | `YYYY-MM-DD`, at most 2 days in the future and at most 35 days in the past (server clock) |
| `sessions` | object with exactly `started`, `succeeded` and `failed` |
| `frames` | integer |
| `decoded` | object with exactly `p0`, `p0_1`, `p1`, `p2` and `p3` |
| `time_to_decode_ms` and `frame_ms` | object with exactly `count`, `sum` and `buckets` (10 integers) |

- Every count is an integer from 0 to 10,000,000. A `sum` is an integer from 0 to 10<sup>12</sup>.
- Bucket counts must add up to `count`.
- Unknown fields, at any depth, are rejected.

Histogram bucket upper edges, in ms (the last bucket is everything above):

| Histogram | Edges |
|---|---|
| `time_to_decode_ms` | 25, 50, 100, 200, 400, 800, 1600, 3200, 6400, ∞ |
| `frame_ms` | 2, 5, 10, 20, 40, 80, 160, 320, 640, ∞ |

The service replies `204` when it accepts the batch, `400` for an invalid one, `413` if the body is too large, and `429` when rate limited. SDKs drop a batch after a `400` or `413`, and keep it after a `429`, `5xx` or network error.

## Service obligations

- **Storage:** aggregates only, summed per (day, sdk, sdk_version, platform). Raw request bodies are not stored.
- **IP addresses:**
  - not written to application logs or the database;
  - rate limiting uses an in-memory key derived from the IP with a random salt that rotates daily;
  - nothing derived from the IP is persisted.
- **Retention:** daily aggregates are kept for 400 days.
- **Abuse limits:** per-key rate limits, a per-day cap on accepted batches, and size limits. Counts can still be inflated by a determined sender; see below.
- **Reporting:** the public summary (`GET /api/telemetry/v1/summary`) labels its figures *reported by SDKs that left telemetry on*. These are not total usage, unique users or worldwide scans.

## Integrity caveat

Anonymous counters from open-source SDKs are unauthenticated. They can be spoofed, replayed or inflated, and without identifiers they cannot be deduplicated. Treat the figures as a coarse signal, and publish the methodology alongside them.

## Other adoption evidence

These are reported separately and never merged into one "users" number:

- package downloads (not the same as active installs);
- GitHub activity;
- voluntarily registered apps and businesses;
- opt-in redirect counts from openckey.org short links.

## Privacy disclosures

Integrators must disclose telemetry according to their own app-store and legal requirements. For example, Apple's privacy manifest lists "Product Interaction → Analytics, not linked to the user, not used for tracking" while mode 1 is on. Builds for OS vendors can leave the telemetry module out entirely: the scanner library does not depend on it.
