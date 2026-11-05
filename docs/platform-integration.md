# Integrating the CKey scanner into a camera platform

This guide is for teams that own a camera pipeline: OS vendors, camera apps and scanning frameworks. It covers specification §6 and the benchmark plan that §12 leaves open. Adoption remains each platform's decision, and nothing here implies one has been made.

## What to take

| Component | Library | Header | Notes |
|---|---|---|---|
| Codeword framing, RS, CRC | `ckey_codec` | `ckey/ckey.h` | no dependencies |
| Detection, refinement, decoding | `ckey_scan` | `ckey/ckey.h` | needs only `ckey_codec`; no renderer, no telemetry, no network, no file system, no threads |
| Payload interpretation | your code | [`spec/CKey-Payload-Conventions.md`](../spec/CKey-Payload-Conventions.md) | allowlisted schemes only; never act without a tap |

Do not link `ckey_render`, `ckey_certify` or `ckey_telemetry` in a scanner. Configure with `-DCKEY_BUILD_TELEMETRY=OFF` to leave telemetry out of the build entirely.

## Pipeline for each frame

The reference loop is `ios/CKeyReader/CameraScanner.swift`, and every SDK repeats it.

1. **Luma plane:** the Y plane of a 420 buffer is enough. No colour conversion is needed.
2. **Find borders:** call `ckey_find_borders_gray8(gray, w, h, stride, quads, max)` with up to 6 candidates. These are oriented quadrilaterals of the continuous frame stroke.
3. **Refine and decode Profile 0:** for each candidate, call `ckey_refine_quad_gray8(..., CKEY_FIELD_SCALABLE, quad)`. If it scores above 0, call `ckey_decode_scalable_gray8(...)` with an output buffer of 65,535 bytes. The decoder identifies the version and layout from the keys.
4. **Legacy profiles (optional):** repeat with `CKEY_FIELD_LOCATOR`, `CKEY_FIELD_TEXT` and `CKEY_FIELD_EXTENDED` and their decoders.
5. **Emit the first valid decode.** Every result has passed Reed–Solomon and CRC-32C. The candidate ranker only orders the work; it accepts nothing.

**Threading:** every call is reentrant and keeps no global mutable state. Layout tables are built once, behind `std::call_once`. Run one frame per worker.

**Memory:** the caller owns all buffers. The decoder allocates a few working vectors per call, proportional to the symbol's size.

## Security review surface

- **Bounded inputs:**
  - Image calls validate dimensions, strides and quads before reading.
  - The decoders never write past `out_capacity` and report the required size instead.
  - The `.ckey` reader, which a scanner does not need, is bounded and checks its CRC. It is fuzzed in CI with 20,000 mutations under ASan/UBSan.
- **No parsing of untrusted text in the scanner.** Payload bytes come back verbatim, and interpreting them is your code's job, under the conventions' safety rules.
- **CI hardening:** a Debug ASan/UBSan matrix runs on every push (`.github/workflows/ci.yml`).
- **Determinism:** fixed tables and seeded hashes. `tests/vectors/conformance-v1.json` pins codewords and geometry.

## Conformance

Run `ckey-conformance --check tests/vectors/conformance-v1.json` against your build, then decode the vectors' rasters through your own image path. Any mismatch in a codeword or the geometry hash is a conformance failure ([details](../tools/conformance/README.md)).

## Benchmark test plan (proposed)

This defines a latency target as specification §6 requires. Every target is stated per device, per image size, per profile/version and per percentile. Nothing has been measured on devices yet.

| Dimension | Values |
|---|---|
| Devices | the vendor's current base model and its oldest supported model (on iOS, also the Neural Engine-free path) |
| Frame | 1920×1080 and 3840×2160 luma, at the camera's native rate |
| Symbols | P0 v1, v6, v16, v31 at level M, standard layout, plus P0.1 v6 |
| Printed sizes | 0.5 in, 1 in and 2 in, on matte paper and on a phone screen |
| Distances | as far as the camera can go with the symbol still covering 2–4 px per module |
| Lighting | 50, 300 and 1,000 lux; backlit; with specular glare |
| Motion | handheld, and on a 20°/s pan |
| Clutter | the symbol among QR codes, text and patterned backgrounds |

The plan collects 300 captures per cell, labelled with the payload. From each cell it reports:

- frame latency at p50, p90 and p99;
- time from first frame to first decode, at p50, p90 and p99;
- the rate of decoding within 1 second;
- false positives per 10,000 symbol-free frames;
- results for QR at the same printed area, for comparison.

The proposed acceptance target is first decode at p90 under 200 ms, on the base device at 1080p, for v1–v16 at 1 in and 30 cm. This target must be confirmed with measured data before anyone quotes it.

`ckey-bench` (in [`docs/benchmarks`](benchmarks)) runs the same metrics on synthetic frames, so a regression can be caught before device runs.
