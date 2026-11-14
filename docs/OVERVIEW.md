# CKey technical overview

A tour of the formats, the libraries and the tools, with the commands that exercise each one. For installing a package, start with the [README](../README.md).

## Current milestone

The locator core defines a fixed 29 × 29 profile, byte framing, CRC-32C, shortened Reed–Solomon coding and correction, deterministic Cartesian/polar placement, SVG rendering, and grayscale image decoding. Read the current draft formats — [Profile 0 Format 0.27.0](../spec/CKey-Format-v0.27.0.md), [Profile 0.1 Format 0.28.0](../spec/CKey-Format-v0.28.0.md), [Profile 1 Format 0.14.0](../spec/CKey-Format-v0.14.0.md), [Profile 2 Format 0.25.0](../spec/CKey-Format-v0.25.0.md), and [Profile 3 Format 0.26.0](../spec/CKey-Format-v0.26.0.md) — before treating any bytes or geometry as interoperable. Byte-grouped placement for Profile 0 ([Format 0.29.0](../spec/CKey-Format-v0.29.0.md), [0.30.0](../spec/CKey-Format-v0.30.0.md)) and correction levels U and X are opt-in from build 86. The Arc layout ([Format 0.32.0](../spec/CKey-Format-v0.32.0.md), byte-grouped [0.33.0](../spec/CKey-Format-v0.33.0.md)) is read from build 110, and from build 114 the SDKs and CLI encode Format 0.33 by default from version 8 (see [layouts](#layouts)). It is a draft and may change before 1.0.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # checks are always on, including Release
./build/ckey encode --hex 00112233445566778899aabbccddeeff --out locator.svg
```

`ckey encode` accepts exactly 16 bytes for the locator profile. The C ABI `ckey_decode_gray8` decodes a one-channel 8-bit grayscale crop when the caller supplies the symbol quadrilateral. It rectifies projective perspective, checks the Grid/Ray/Ring calibration keys and both monochrome polarities, samples the matrix, and validates the payload through Reed–Solomon and CRC-32C. The caller supplies the symbol's four corners, or finds them with the core's full-frame localization: `ckey_find_borders_gray8` returns border candidates from a whole frame, and `ckey_refine_quad_gray8` fits each to a profile's field. The pipeline is tested on simulated camera frames, not yet on a labelled real-camera corpus. From build 86, Profile 0 decoding estimates radial lens distortion from the frame and uses erasure and soft-decision Reed–Solomon decoding; Profiles 1 to 3 have neither. `ckey_scan_frame_gray8` runs the whole search, refine and decode loop in one call. The committed optical image fixture decodes a browser-rendered capture at 670 × 670 pixels, and also checks rotated corner order and inverted polarity. This is evidence of the image sampling path, not a real-camera robustness claim. RS(77,26) is genuine Reed–Solomon error correction over GF(256); it is not masking.

The experimental text profile accepts up to 200 UTF-8 bytes or arbitrary bytes and is intentionally separate from the compact locator:

```sh
./build/ckey encode-text --utf8 "a message up to 200 UTF-8 bytes" --out text.svg
./build/ckey encode-text --hex 001122334455 --out binary.svg
```

It implements a real RS(255,210) frame with 45 parity bytes, a dense Cartesian/polar renderer, connected multi-unit ring arcs, and the canonical one-arc-per-corner finder geometry. See [Format 0.25.0](../spec/CKey-Format-v0.25.0.md). Short frames use deterministic visual fill, protected by the same RS block; every optical site carries a whitened value, so no region of the symbol is left empty, and payload modules keep a declared clearance from every structural mark. The projective grayscale decoder reads clean renders from about 10 px per unit (round-trip tested in CI); physical camera scans still need validation.

The experimental extended profile accepts 1–2,048 bytes. Seven capacity tiers (256, 384, 512, 768, 1,024, 1,536, and 2,048 bytes) trade payload room for Reed–Solomon parity while keeping the full 2,550-byte symbol populated. It uses ten byte-interleaved tiered RS(255,k) blocks, 34 polar rings, and an eight-state ray alphabet (four Gray-coded lengths × two calibrated widths):

```sh
./build/ckey encode-extended --utf8 "large payload" --out extended.svg
./build/ckey encode-extended --hex <up-to-4096-hex-chars> --out extended.svg
./build/ckey encode-extended --tier 512 --utf8 "payload with a 512-byte ceiling" --out extended-512.svg
```

See [Profile 2 Format 0.25.0](../spec/CKey-Format-v0.25.0.md) and [Profile 3 Format 0.26.0](../spec/CKey-Format-v0.26.0.md). The encoder automatically chooses the smallest Profile 3 tier that fits, or callers can select a tier explicitly through the C ABI and CLI. The projective decoder reads clean renders of the 2,048-byte tier from about 11 px per unit (round-trip tested in CI); physical camera decode is not yet validated.

## Libraries, SDKs and services

The core is C++20 behind a C ABI ([`include/ckey/ckey.h`](../include/ckey/ckey.h), ABI 4). It is split so an app links only what it needs ([`src/README.md`](../src/README.md)):

| Library | Purpose |
|---|---|
| `ckey_codec` | encode and decode codewords; the `.ckey` file format |
| `ckey_scan` | full-frame localization and decoding; needs only `ckey_codec` |
| `ckey_render` | canonical geometry; native RGBA/gray raster, PNG, PDF, SVG, print |
| `ckey_certify` | style certification through a modelled camera scan |
| `ckey_telemetry` | optional aggregate scan counters ([details](adoption-and-telemetry.md)) |

`cmake --install` provides a CMake package (`find_package(CKey)`, targets `CKey::core`, `CKey::scan` and so on); see [`examples/c`](../examples/c).

| SDK | Package | Location | Verified |
|---|---|---|---|
| Swift (iOS, macOS) | SwiftPM `CKey` (this repository's URL) | [`sdk/swift`](../sdk/swift) | in CI on macOS |
| Web (WASM) | npm `openckey` | [`sdk/web`](../sdk/web) | JS here; wasm in CI |
| Android | Maven `org.openckey:ckey` | [`sdk/android`](../sdk/android) | JNI here; Gradle in CI |
| .NET | NuGet `CKey` | [`sdk/dotnet`](../sdk/dotnet) | native library here; C# in CI |
| CLI | Homebrew `ckey` (formula generated per release) | [`packaging/homebrew`](../packaging/homebrew) | |

Install commands for every package are in the [README](../README.md#install).

Every SDK wraps the same core. Each gives you:

- headless rendering to native images, plus a UI view;
- a scanner;
- payload actions per the [conventions](../spec/CKey-Payload-Conventions.md);
- telemetry mode `0`/`1`/`2`, default `1`. Opting out takes one line: `CKey.telemetry = 0`.

[openckey.org](https://openckey.org) is the project's hosted site. It runs separately from this repository and is not open source. It provides:

- the browser renderer;
- downloads of the packages above (MIT, built from this repository);
- `/text/{id}` and `/sq/{id}` destinations;
- the telemetry endpoint the SDKs report to. It keeps aggregates only and stores no IPs, as `docs/adoption-and-telemetry.md` requires.

### Rendering, certification and verification

```sh
./build/ckey encode-scalable --utf8 "https://openckey.org" --raster code.png --size-px 1024             # native pixels
./build/ckey encode-scalable --utf8 "https://openckey.org" --raster code.png --transparent yes          # alpha 0 where unpainted
./build/ckey encode-scalable --utf8 "https://openckey.org" --pdf code.pdf --size-in 2                   # vector PDF
```

- **Default look:** black on white.
- **Style policies** (the `policy` field of `ckey_style`):
  - `strict` (default): refuses styles that could fail to scan;
  - `adaptive`: repairs them and reports the changes;
  - `override`: renders anything, but never certifies it.
- **Certification:** `ckey_certify_scalable_style` certifies a style by decoding it through a modelled camera, not by contrast ratio. A transparent symbol needs the host's backdrop to be certified ([design system](../spec/CKey-Design-System.md#policies-and-certification)).
- **Conformance:** vectors in [`tests/vectors/conformance-v1.json`](../tests/vectors/conformance-v1.json) ([how](../tools/conformance/README.md)).
- **Benchmarks:** synthetic, with QR and Data Matrix comparison in CI ([`docs/benchmarks`](benchmarks)).
- **Status:** the full requirement-by-requirement map is in [`docs/IMPLEMENTATION-STATUS.md`](IMPLEMENTATION-STATUS.md).

## Profile 0: the scalable family

Profile 0 ([Format 0.27.0](../spec/CKey-Format-v0.27.0.md)) is one design at 31 sizes. Every version keeps the same frame, finders, rings and dial, and only the module pitch changes. It runs from Profile 1-sized modules (v1, 29 across, 27 bytes at level M) to Profile 3 density (v31, 232 across, 4,619 bytes in the Standard layout), about 7% finer per step. It removes the appearance jumps between Profiles 1, 2 and 3: a 33-character URL becomes v2, which still looks like a locator.

The Grid, Ring and Ray Keys are drawn at each version's own pitch, so the reader identifies the version from them. Four correction levels (L/M/Q/H) trade capacity for robustness. Profiles 1–3 remain available and unchanged.

### Layouts

Profile 0 has several layouts. They share the frame, finders, version table and framing; the reader tells them apart from the keys and reports the one it found.

| Layout | Format | C (`ckey_scalable_layout`) | Web / CLI | Swift | Android | Use |
|---|---|---|---|---|---|---|
| Standard | [0.27](../spec/CKey-Format-v0.27.0.md) | `CKEY_LAYOUT_STANDARD` 0 | `standard` | `.standard` | `STANDARD` | Screens and ordinary prints; the default for versions 1–7 |
| Print (P0.1) | [0.28](../spec/CKey-Format-v0.28.0.md) | `CKEY_LAYOUT_PRINT` 1 | `print` | `.print` | `PRINT` | Small prints |
| Standard grouped | [0.29](../spec/CKey-Format-v0.29.0.md) | `CKEY_LAYOUT_STANDARD_GROUPED` 2 | `standard-grouped` | `.standardGrouped` | `STANDARD_GROUPED` | Standard, byte-grouped |
| Print grouped | [0.30](../spec/CKey-Format-v0.30.0.md) | `CKEY_LAYOUT_PRINT_GROUPED` 3 | `print-grouped` | `.printGrouped` | `PRINT_GROUPED` | Print, byte-grouped |
| Pixel (experimental) | 0.31 | `CKEY_LAYOUT_PIXEL` 4 | `pixel` | `.pixel` | `PIXEL` | Pixel-perfect screens at one or two pixels per module |
| Arc | [0.32](../spec/CKey-Format-v0.32.0.md) | `CKEY_LAYOUT_STANDARD_ARC` 5 | `arc` | `.standardArc` | `STANDARD_ARC` | Standard with equal-arc rings |
| Arc grouped | [0.33](../spec/CKey-Format-v0.33.0.md) | `CKEY_LAYOUT_STANDARD_ARC_GROUPED` 6 | `arc-grouped` | `.standardArcGrouped` | `STANDARD_ARC_GROUPED` | Arc, byte-grouped; the default from version 8 |
| Auto (encoding only) | — | `CKEY_LAYOUT_AUTO` 255 | `auto` | `nil` | `AUTO` | Standard for v1–7, Arc grouped for v8–31 |

**Arc** (build 110). Standard gives every ring the dial's slot count D at equal angles, so inner-ring marks sit up to twice as close as the outermost ring's (v21: 0.074 units apart on the innermost ring against 0.149 on the outermost; module pitch 0.25). Arc gives ring k 8·⌊D·r_k/r_outer/8⌋ slots and keeps D on the outermost ring, and its Ring Key starts at 6:00 instead of 3:00 so the reader tells the two families apart. Capacity at level M is about 10–12% lower for large versions (v21: 959 against 1,085 bytes; v31: 4,100 against 4,619) and more for small ones (v1: 21 against 27). To the eye it looks like Standard, except that the inner rings have fewer, longer marks.

**Auto** (build 114) is the default in Swift (`encode(layout: nil)`), the web (`layout: 'auto'` or omitted), Android (`Layout.AUTO`) and the CLI (`--layout auto`). With version 0 it picks the smallest version that fits under that rule. In the C API it is opt-in: callers name a layout, and `CKEY_LAYOUT_AUTO` is one of the choices. A symbol always carries the layout actually chosen, and naming a layout gives exactly that layout. The .NET SDK does not have Arc, Pixel or Auto yet; its default is Standard. Codes made by default from v8 up need readers from build 110.

**Why.** Opt-in scan reports from the owner's iPhone showed Standard codes' ring errors falling steadily with radius, from about 21% inside r = 4.3 to about 1% on the outermost rings, with the grid at 0.1–0.4%. Because each byte's 8 sites are scattered, 4% wrong sites spoiled 26–30% of bytes, and the worst Reed–Solomon block had 77–86 bad bytes against 35 correctable. In a side-by-side field run (same payload, reader build 113), Arc v22 at 392–438 px and Standard v21 at 442–465 px both read in a median of about 120 ms; in the frames that read, Arc had about a third of Standard's wrong sites (median 43 against 96) and a fifth of its inner-ring errors (0.3–1.9% against 3–9%), at a smaller size. On the screen model (lattice at 6 cells per module, blur σ 1 px, 3% noise) the smallest width that reads with the full reader is 246 px for Standard and 210 for Arc at 300 bytes, 400 and 329 at 900 bytes, and 590 and 507 at 2,000 bytes. Small versions keep Standard because Arc costs them the most capacity.

```sh
./build/ckey encode-scalable --utf8 "https://citeory.com/tour?ref=ckey" --out p0.svg
./build/ckey encode-scalable --utf8 "a longer message" --version 12 --level Q --out p0-v12.svg
./build/ckey encode-scalable --utf8 "a longer message" --version 12 --layout standard --out p0-v12-standard.svg   # not Arc
```

## Design system

Profile 0 symbols can be branded without breaking scanning; see the [CKey Design System](../spec/CKey-Design-System.md):

- **Mark shapes:** modules rounded, square, circle or diamond; rings joined or dotted; rays round or flat.
- **Paint:** metallic sheen, vignette, accent colours, bevel, depth and grain.
- **Logo:** in the centre.
- **Fixed:** the frame, finders and keys.

A built-in check enforces contrast and effect limits and the logo's error-correction budget.

```sh
./build/ckey encode-scalable --utf8 "https://citeory.com/tour" --preset concept --modules circle --logo logo.png --out branded.svg
```

## Printing

Small prints need dark marks on paper, a device-grid bitmap printed at actual size, and, for small or dense symbols, the print-robust **Profile 0.1** layout ([Format 0.28.0](../spec/CKey-Format-v0.28.0.md)). See the [printing guide](../spec/CKey-Printing.md).

```sh
./build/ckey encode-scalable --utf8 "https://citeory.com/tour" --layout print --png p01.png --size-in 0.25 --dpi 600
./build/ckey print-sheet --out sheet.png   # calibration ladder for your printer and paper
```

## Payload conventions

Payloads are bytes. [CKey Payload Conventions](../spec/CKey-Payload-Conventions.md) (informative) defines how readers interpret text payloads:

- Standard URI forms: `https:`, `mailto:`, `tel:`, `sms:`, `geo:`, and vCard.
- One CKey convention, `address:`, which carries a free-text street address for the map app.
- Safety rules: nothing happens until the user taps, and only allowlisted forms get an action.

## iPhone reader

An experimental SwiftUI camera reader is in [`ios/CKeyReader.xcodeproj`](../ios/CKeyReader.xcodeproj). It compiles the same `src/ckey.cpp` core into the app, locks capture to the physical wide rear camera, ranks thresholded border components, and refines each quadrilateral against the printed Grid, Ring, Ray, and finder keys before passing grayscale samples to the C ABI. Its small generated ranker only prioritizes candidates; key agreement plus Reed–Solomon and CRC remain the acceptance gate. Open the project in Xcode and follow [`ios/README.md`](../ios/README.md) to run it on an iPhone. Camera scanning is an early test build; the supported capture envelope and device performance have not been established.

The companion Mac laboratory is [`macos/CKeyRenderer.xcodeproj`](../macos/CKeyRenderer.xcodeproj). It invokes the same C++ encoders and SVG renderer, then applies non-normative color, blur, damage, noise, rotation, and resolution reduction. The preview runs each rendered raster through the same C++ image decoder and reports pass/fail; this controlled crop test does not replace full-frame camera localization or held-out phone captures.

## Autonomous synthetic-camera training

The candidate ranker can be rebuilt without holding a phone up to a display:

```sh
python3 tools/train_synthetic_locator.py --samples 1500 --keep-images
```

The command compiles the current C++ encoder, makes actual Profile 1/2/3 SVG artifacts with varied payload sizes, rasterizes them, applies deterministic perspective, lighting, blur, noise, damage, polarity-adjacent contrast, scale, and clutter variation, records the expected payload/profile/corners, derives candidate measurements from those captures, and emits `ios/CKeyReader/LocatorRanker.swift`. It requires Inkscape for exact SVG rasterization. The result ranks border candidates only; mathematical key fitting plus RS/CRC remains the only payload-acceptance authority.

## Milestones

1. **Format and deterministic core (current):** frozen-enough draft framing/geometry, reference encoder, SVG, independent test vectors, C ABI.
2. **Decoder and image normalization:** RS error/erasure correction, structured diagnostics, grayscale image acquisition, geometric localization and perspective normalization, real-camera corpus.
3. **Interop and SDKs:** Swift, Kotlin, JS/TS, WASM; wrappers call the same core rather than reimplementing the format.
4. **Laboratory and hardening:** browser test lab, perturbation suite, fuzzing, license audit, reproducible release and measured device benchmarks.

Every release milestone must report tests run, real-capture conditions, latency distributions, and limits. No performance target is considered achieved without published benchmark data.

## License and research provenance

The repository already used MIT at the start of this work; it is retained to preserve existing repository intent. Apache-2.0 is also permissive and adds an explicit patent grant, which may be preferable if the project later needs stronger patent clarity. Changing the repository license requires an owner decision and contributor-rights review; no research implementation is copied into this milestone.

Research informing the independent design is summarized in [the research and license audit](../spec/RESEARCH.md). Cited ideas are not copied source code. Each dependency must have its license recorded before adoption.
