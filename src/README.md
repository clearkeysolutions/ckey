# Core source layout

The core is C++20 behind a C ABI (`include/ckey/ckey.h`, `include/ckey/telemetry.h`). It is built as separate libraries, so an app links only what it uses (specification §2.1, §6 and §9.2).

| Library | Translation unit | Contents | Depends on |
|---|---|---|---|
| `ckey_codec` | `ckey_codec.cpp` | encode and decode codewords for every profile (framing, CRC-32C, Reed–Solomon), capacities, version queries | nothing |
| `ckey_scan` | `ckey_scan.cpp` | grayscale image decoding, calibration scores, full-frame localization | `ckey_codec` |
| `ckey_render` | `ckey_render.cpp` | styles and their checks, canonical geometry, SVG, direct raster, PNG, PDF, print | nothing |
| `ckey_telemetry` | `telemetry.cpp` | optional aggregate counters and batches; no networking | nothing |
| `ckey_core` | (interface) | `ckey_codec` + `ckey_scan` + `ckey_render` | |

Each library's translation unit is the shared internals plus its own public functions:

- `core_internal.inc`: internal definitions shared by all three. It covers Reed–Solomon, CRC, layouts, sampling, the style checker and localization helpers, all with internal linkage.
- `api_codec.inc`, `api_scan.inc`, `api_render.inc`: the `extern "C"` functions of each library.
- `scalable_render.inc`: the Profile 0 geometry engine and its native backends. `api_render.inc` includes it.
- `ckey.cpp`: the amalgamation, which includes all the parts once. The Xcode projects compile it as a single file, and the white-box tests include it.
- `svg_raster.h`: the reference SVG rasterizer. It is used only by tests and tools; no library includes it.

A scan-only build has no renderer or telemetry code, and CI proves it: `ckey_optical_image_decode` links `ckey_scan` alone. Builds for OS vendors can configure with `-DCKEY_BUILD_TELEMETRY=OFF`.
