# CKey

CKey is an independent, monochrome optical symbol under development. Its target geometry combines a square Cartesian field with a calibrated polar field. The repository is the implementation and interoperability record; this is not a declaration that camera robustness or a capacity target has already been achieved.

## Current milestone

The locator core defines a fixed 29 × 29 profile, byte framing, CRC-32C, shortened Reed–Solomon coding and correction, deterministic Cartesian/polar placement, SVG rendering, and grayscale image decoding. Read the current draft formats — [Profile 0 Format 0.27.0](spec/CKey-Format-v0.27.0.md), [Profile 0.1 Format 0.28.0](spec/CKey-Format-v0.28.0.md), [Profile 1 Format 0.14.0](spec/CKey-Format-v0.14.0.md), [Profile 2 Format 0.25.0](spec/CKey-Format-v0.25.0.md), and [Profile 3 Format 0.26.0](spec/CKey-Format-v0.26.0.md) — before treating any bytes or geometry as interoperable. It is a draft and may change before 1.0.

The authoritative working state is identified by [`VERSION`](VERSION). Read [`CONTEXT.md`](CONTEXT.md) and [`CONTRIBUTING_VERSIONING.md`](CONTRIBUTING_VERSIONING.md) before changing the project. Every committed change must bump the software version and update the context record.

## Repository, branches and releases

The project lives in one repository, [`clearkeysolutions/ckey`](https://github.com/clearkeysolutions/ckey). It has three long-lived branches:

| Branch | Who can push | Use it for |
|---|---|---|
| `scratch` | Maintainer only | Development. Every change is written and tested here first, one numbered build per commit. |
| `Stable-Release` | Maintainer only; anyone may fork | The supported source. Every release is a tag on this branch. Fork from here. |
| `community` | Anyone | Experimental contributions. **Unstable:** do not depend on it in production. |

Changes flow `scratch` → `Stable-Release` → forks and `community`. Community work reaches a release only when the maintainer brings it into `scratch`. Releases are immutable tags such as `v0.1.0-dev.60`. A deprecated release keeps its tag and is retitled `Deprecated - {release name}`. No release has been tagged yet. See [branches and releases](docs/BRANCHES-AND-RELEASES.md).

### Versions

CKey has four version numbers:

- **Software version:** `0.1.0-dev.N`, bumped on every commit. It lives in [`VERSION`](VERSION).
- **Format version:** `0.N.0`, written into every symbol as format byte `N`.
- **Profile byte:** `00`–`03`.
- **C ABI version:** currently 3.

The [release and versioning map](docs/RELEASE-AND-VERSIONING-MAP.md) has the full picture:

- which build introduced each format and ABI;
- which legacy formats are still readable;
- a build-to-commit table;
- the procedure for cutting and deprecating a release.

### Changelogs

| File | Covers |
|---|---|
| [`CHANGELOG.md`](CHANGELOG.md) | Every development build on `scratch`, newest first |
| [`CHANGELOG-STABLE.md`](CHANGELOG-STABLE.md) | Tagged releases on `Stable-Release` (none yet) |
| [`CHANGELOG-COMMUNITY.md`](CHANGELOG-COMMUNITY.md) | Changes merged into `community`, and the rules for contributing there |

### Documentation

- [`spec/`](spec): the wire-format specifications, one file per format version, plus [payload conventions](spec/CKey-Payload-Conventions.md), the [design system](spec/CKey-Design-System.md), the [printing guide](spec/CKey-Printing.md) and the [research and license audit](spec/RESEARCH.md).
- [`docs/SPECIFICATION-rendering-sdk-distribution-telemetry.md`](docs/SPECIFICATION-rendering-sdk-distribution-telemetry.md): the rendering, SDK, distribution, adoption and telemetry specification.
- [`docs/IMPLEMENTATION-STATUS.md`](docs/IMPLEMENTATION-STATUS.md): how far the code meets that specification.
- [`CONTEXT.md`](CONTEXT.md): the living engineering record.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # checks are always on, including Release
./build/ckey encode --hex 00112233445566778899aabbccddeeff --out locator.svg
```

`ckey encode` accepts exactly 16 bytes for the locator profile. The C ABI `ckey_decode_gray8` decodes a one-channel 8-bit grayscale crop when the caller supplies the symbol quadrilateral. It rectifies projective perspective, checks the Grid/Ray/Ring calibration keys and both monochrome polarities, samples the matrix, and validates the payload through Reed–Solomon and CRC-32C. The caller supplies the symbol's four corners, or finds them with the core's full-frame localization: `ckey_find_borders_gray8` returns border candidates from a whole frame, and `ckey_refine_quad_gray8` fits each to a profile's field. The pipeline is tested on simulated camera frames, not yet on a labelled real-camera corpus. Lens-distortion correction and known-erasure recovery are not implemented. The committed optical image fixture decodes a browser-rendered capture at 670 × 670 pixels, and also checks rotated corner order and inverted polarity. This is evidence of the image sampling path, not a real-camera robustness claim. RS(77,26) is genuine Reed–Solomon error correction over GF(256); it is not masking.

The experimental text profile accepts up to 200 UTF-8 bytes or arbitrary bytes and is intentionally separate from the compact locator:

```sh
./build/ckey encode-text --utf8 "a message up to 200 UTF-8 bytes" --out text.svg
./build/ckey encode-text --hex 001122334455 --out binary.svg
```

It implements a real RS(255,210) frame with 45 parity bytes, a dense Cartesian/polar renderer, connected multi-unit ring arcs, and the canonical one-arc-per-corner finder geometry. See [Format 0.25.0](spec/CKey-Format-v0.25.0.md). Short frames use deterministic visual fill, protected by the same RS block; every optical site carries a whitened value, so no region of the symbol is left empty, and payload modules keep a declared clearance from every structural mark. The projective grayscale decoder reads clean renders from about 10 px per unit (round-trip tested in CI); physical camera scans still need validation.

The experimental extended profile accepts 1–2,048 bytes. Seven capacity tiers (256, 384, 512, 768, 1,024, 1,536, and 2,048 bytes) trade payload room for Reed–Solomon parity while keeping the full 2,550-byte symbol populated. It uses ten byte-interleaved tiered RS(255,k) blocks, 34 polar rings, and an eight-state ray alphabet (four Gray-coded lengths × two calibrated widths):

```sh
./build/ckey encode-extended --utf8 "large payload" --out extended.svg
./build/ckey encode-extended --hex <up-to-4096-hex-chars> --out extended.svg
./build/ckey encode-extended --tier 512 --utf8 "payload with a 512-byte ceiling" --out extended-512.svg
```

See [Profile 2 Format 0.25.0](spec/CKey-Format-v0.25.0.md) and [Profile 3 Format 0.26.0](spec/CKey-Format-v0.26.0.md). The encoder automatically chooses the smallest Profile 3 tier that fits, or callers can select a tier explicitly through the C ABI and CLI. The projective decoder reads clean renders of the 2,048-byte tier from about 11 px per unit (round-trip tested in CI); physical camera decode is not yet validated.

## Profile 0: the scalable family

Profile 0 ([Format 0.27.0](spec/CKey-Format-v0.27.0.md)) is one design at 31 sizes. Every version keeps the same frame, finders, rings and dial, and only the module pitch changes. It runs from Profile 1-sized modules (v1, 29 across, 27 bytes at level M) to Profile 3 density (v31, 232 across, 4,619 bytes), about 7% finer per step. It removes the appearance jumps between Profiles 1, 2 and 3: a 33-character URL becomes v2, which still looks like a locator.

The Grid, Ring and Ray Keys are drawn at each version's own pitch, so the reader identifies the version from them. Four correction levels (L/M/Q/H) trade capacity for robustness. Profiles 1–3 remain available and unchanged.

```sh
./build/ckey encode-scalable --utf8 "https://citeory.com/tour?ref=ckey" --out p0.svg
./build/ckey encode-scalable --utf8 "a longer message" --version 12 --level Q --out p0-v12.svg
```

## Design system

Profile 0 symbols can be branded without breaking scanning; see the [CKey Design System](spec/CKey-Design-System.md):

- **Mark shapes:** modules rounded, square, circle or diamond; rings joined or dotted; rays round or flat.
- **Paint:** metallic sheen, vignette, accent colours, bevel, depth and grain.
- **Logo:** in the centre.
- **Fixed:** the frame, finders and keys.

A built-in check enforces contrast and effect limits and the logo's error-correction budget.

```sh
./build/ckey encode-scalable --utf8 "https://citeory.com/tour" --preset concept --modules circle --logo logo.png --out branded.svg
```

## Printing

Small prints need dark marks on paper, a device-grid bitmap printed at actual size, and, for small or dense symbols, the print-robust **Profile 0.1** layout ([Format 0.28.0](spec/CKey-Format-v0.28.0.md)). See the [printing guide](spec/CKey-Printing.md).

```sh
./build/ckey encode-scalable --utf8 "https://citeory.com/tour" --layout print --png p01.png --size-in 0.25 --dpi 600
./build/ckey print-sheet --out sheet.png   # calibration ladder for your printer and paper
```

## Payload conventions

Payloads are bytes. [CKey Payload Conventions](spec/CKey-Payload-Conventions.md) (informative) defines how readers interpret text payloads:

- Standard URI forms: `https:`, `mailto:`, `tel:`, `sms:`, `geo:`, and vCard.
- One CKey convention, `address:`, which carries a free-text street address for the map app.
- Safety rules: nothing happens until the user taps, and only allowlisted forms get an action.

## iPhone reader

An experimental SwiftUI camera reader is in [`ios/CKeyReader.xcodeproj`](ios/CKeyReader.xcodeproj). It compiles the same `src/ckey.cpp` core into the app, locks capture to the physical wide rear camera, ranks thresholded border components, and refines each quadrilateral against the printed Grid, Ring, Ray, and finder keys before passing grayscale samples to the C ABI. Its small generated ranker only prioritizes candidates; key agreement plus Reed–Solomon and CRC remain the acceptance gate. Open the project in Xcode and follow [`ios/README.md`](ios/README.md) to run it on an iPhone. Camera scanning is an early test build; the supported capture envelope and device performance have not been established.

The companion Mac laboratory is [`macos/CKeyRenderer.xcodeproj`](macos/CKeyRenderer.xcodeproj). It invokes the same C++ encoders and SVG renderer, then applies non-normative color, blur, damage, noise, rotation, and resolution reduction. The preview runs each rendered raster through the same C++ image decoder and reports pass/fail; this controlled crop test does not replace full-frame camera localization or held-out phone captures.

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

Research informing the independent design is summarized in [the research and license audit](spec/RESEARCH.md). Cited ideas are not copied source code. Each dependency must have its license recorded before adoption.
