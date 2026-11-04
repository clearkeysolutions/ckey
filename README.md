# CKey

CKey is an independent, monochrome optical symbol under development. Its target geometry combines a square Cartesian field with a calibrated polar field. The repository is the implementation and interoperability record; this is not a declaration that camera robustness or a capacity target has already been achieved.

## Current milestone

The locator core defines a fixed 29 × 29 profile, byte framing, CRC-32C, shortened Reed–Solomon coding and correction, deterministic Cartesian/polar placement, SVG rendering, and grayscale image decoding. Read the current draft formats — [Profile 1 Format 0.14.0](spec/CKey-Format-v0.14.0.md), [Profile 2 Format 0.25.0](spec/CKey-Format-v0.25.0.md), and [Profile 3 Format 0.26.0](spec/CKey-Format-v0.26.0.md) — before treating any bytes or geometry as interoperable. It is a draft and may change before 1.0.

The authoritative working state is identified by [`VERSION`](VERSION). Read [`CONTEXT.md`](CONTEXT.md) and [`CONTRIBUTING_VERSIONING.md`](CONTRIBUTING_VERSIONING.md) before changing the project. Every committed change must bump the software version and update the context record.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # checks are always on, including Release
./build/ckey encode --hex 00112233445566778899aabbccddeeff --out locator.svg
```

`ckey encode` accepts exactly 16 bytes for the locator profile. The C ABI `ckey_decode_gray8` decodes a one-channel 8-bit grayscale crop when the caller supplies the symbol quadrilateral. It rectifies projective perspective, checks the Grid/Ray/Ring calibration keys and both monochrome polarities, samples the matrix, and validates the payload through Reed–Solomon and CRC-32C. The caller must locate the symbol and provide its four corners; full-frame localization, lens-distortion correction, known-erasure recovery, and continuous camera scanning are not implemented. The committed optical image fixture decodes a browser-rendered capture at 670 × 670 pixels, and also checks rotated corner order and inverted polarity. This is evidence of the image sampling path, not a real-camera robustness claim. RS(77,26) is genuine Reed–Solomon error correction over GF(256); it is not masking.

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
