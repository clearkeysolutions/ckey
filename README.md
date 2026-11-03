# CKey

CKey is an independent, monochrome optical symbol under development. Its target geometry combines a square Cartesian field with a calibrated polar field. The repository is the implementation and interoperability record; this is not a declaration that camera robustness or a capacity target has already been achieved.

## Current milestone

The first implementation milestone defines a fixed 29 × 29 locator profile, a byte framing format, CRC-32C, shortened Reed–Solomon coding and correction, deterministic Cartesian/polar placement, SVG rendering, and a command-line encoder. Read [the current draft format](spec/CKey-Format-v0.5.0.md) before treating any bytes or geometry as interoperable. It is a draft and may change before 1.0.

The authoritative working state is identified by [`VERSION`](VERSION). Read [`CONTEXT.md`](CONTEXT.md) and [`CONTRIBUTING_VERSIONING.md`](CONTRIBUTING_VERSIONING.md) before changing the project. Every committed change must bump the software version and update the context record.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/ckey encode --hex 00112233445566778899aabbccddeeff --out locator.svg
```

`ckey encode` accepts exactly 16 bytes for the locator profile. The reference SVG now follows the supplied dark-field, light-mark, rounded-corner target direction. SVG styling does not alter payload bits. The C ABI provides byte-codeword decode with up to 25 unknown erroneous byte corrections, CRC-32C verification, and structured status. RS(77,26) is genuine Reed–Solomon error correction over GF(256); it is not masking. Erasure correction, image detection, perspective correction, native platform packages, and browser scanning are later milestones and are not yet claimed as implemented.

## Milestones

1. **Format and deterministic core (current):** frozen-enough draft framing/geometry, reference encoder, SVG, independent test vectors, C ABI.
2. **Decoder and image normalization:** RS error/erasure correction, structured diagnostics, grayscale image acquisition, geometric localization and perspective normalization, real-camera corpus.
3. **Interop and SDKs:** Swift, Kotlin, JS/TS, WASM; wrappers call the same core rather than reimplementing the format.
4. **Laboratory and hardening:** browser test lab, perturbation suite, fuzzing, license audit, reproducible release and measured device benchmarks.

Every release milestone must report tests run, real-capture conditions, latency distributions, and limits. No performance target is considered achieved without published benchmark data.

## License and research provenance

The repository already used MIT at the start of this work; it is retained to preserve existing repository intent. Apache-2.0 is also permissive and adds an explicit patent grant, which may be preferable if the project later needs stronger patent clarity. Changing the repository license requires an owner decision and contributor-rights review; no research implementation is copied into this milestone.

Research informing the independent design is summarized in [the research and license audit](spec/RESEARCH.md). Cited ideas are not copied source code. Each dependency must have its license recorded before adoption.
