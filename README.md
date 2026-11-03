# CKey

CKey is an independent, monochrome optical symbol under development. Its target geometry combines a square Cartesian field with a calibrated polar field. The repository is the implementation and interoperability record; this is not a declaration that camera robustness or a capacity target has already been achieved.

## Current milestone

The locator core defines a fixed 29 × 29 profile, byte framing, CRC-32C, shortened Reed–Solomon coding and correction, deterministic Cartesian/polar placement, SVG rendering, and grayscale image decoding. Read [the current draft format](spec/CKey-Format-v0.8.0.md) before treating any bytes or geometry as interoperable. It is a draft and may change before 1.0.

The authoritative working state is identified by [`VERSION`](VERSION). Read [`CONTEXT.md`](CONTEXT.md) and [`CONTRIBUTING_VERSIONING.md`](CONTRIBUTING_VERSIONING.md) before changing the project. Every committed change must bump the software version and update the context record.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/ckey encode --hex 00112233445566778899aabbccddeeff --out locator.svg
```

`ckey encode` accepts exactly 16 bytes for the locator profile. The C ABI `ckey_decode_gray8` decodes a one-channel 8-bit grayscale crop when the caller supplies the symbol quadrilateral. It rectifies projective perspective, checks the Grid/Ray/Ring calibration keys and both monochrome polarities, samples the matrix, and validates the payload through Reed–Solomon and CRC-32C. The caller must locate the symbol and provide its four corners; full-frame localization, lens-distortion correction, known-erasure recovery, and continuous camera scanning are not implemented. The committed optical image fixture decodes a browser-rendered capture at 670 × 670 pixels, and also checks rotated corner order and inverted polarity. This is evidence of the image sampling path, not a real-camera robustness claim. RS(77,26) is genuine Reed–Solomon error correction over GF(256); it is not masking.

## iPhone reader

An experimental SwiftUI camera reader is in [`ios/CKeyReader.xcodeproj`](ios/CKeyReader.xcodeproj). It compiles the same `src/ckey.cpp` core into the app, uses Vision to locate a square candidate, and passes grayscale camera frames and candidate corners to the C ABI. Open the project in Xcode and follow [`ios/README.md`](ios/README.md) to run it on an iPhone. The app reports Vision localization time separately from CKey decode time. Camera scanning is an early test build; the supported capture envelope and device performance have not been established.

## Milestones

1. **Format and deterministic core (current):** frozen-enough draft framing/geometry, reference encoder, SVG, independent test vectors, C ABI.
2. **Decoder and image normalization:** RS error/erasure correction, structured diagnostics, grayscale image acquisition, geometric localization and perspective normalization, real-camera corpus.
3. **Interop and SDKs:** Swift, Kotlin, JS/TS, WASM; wrappers call the same core rather than reimplementing the format.
4. **Laboratory and hardening:** browser test lab, perturbation suite, fuzzing, license audit, reproducible release and measured device benchmarks.

Every release milestone must report tests run, real-capture conditions, latency distributions, and limits. No performance target is considered achieved without published benchmark data.

## License and research provenance

The repository already used MIT at the start of this work; it is retained to preserve existing repository intent. Apache-2.0 is also permissive and adds an explicit patent grant, which may be preferable if the project later needs stronger patent clarity. Changing the repository license requires an owner decision and contributor-rights review; no research implementation is copied into this milestone.

Research informing the independent design is summarized in [the research and license audit](spec/RESEARCH.md). Cited ideas are not copied source code. Each dependency must have its license recorded before adoption.
