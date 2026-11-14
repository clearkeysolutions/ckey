# Stable-Release changelog

This log lists the releases tagged on the `Stable-Release` branch, newest first. Each release is one immutable tag. The per-build history is in [CHANGELOG.md](CHANGELOG.md), and the [release and versioning map](docs/RELEASE-AND-VERSIONING-MAP.md) explains how to cut and deprecate a release.

**Releases so far: 3** (`v0.1.0-build.120`, `v0.1.0-build.85`, `v0.1.0-build.83`; `v0.1.0-build.76`, `v0.1.0-build.81` and `v0.1.0-build.82` were tagged but superseded before they were published).

## Entry template

Copy this block to the top of the list for each release, and fill it in.

```markdown
### CKey 0.1.0-build.N — YYYY-MM-DD

* **Tag:** `v0.1.0-build.N` (commit `abcdef0`)
* **Status:** supported  <!-- or: Deprecated YYYY-MM-DD, replaced by CKey 0.1.0-build.M, because … -->
* **Builds included:** build.A to build.N (see CHANGELOG.md)
* **Formats emitted:** P0 0.27.0, P0.1 0.28.0, P1 0.14.0, P2 0.25.0, P3 0.26.0
* **Formats read:** the emitted formats, plus the legacy formats listed in the map
* **C ABI:** 4
* **Highlights:** …
* **Known limits:** …
```

When a release is deprecated:

1. Change its GitHub Release title to `Deprecated - {release name}`.
2. Update its **Status** line here. Never delete the entry.

## Releases

### CKey 0.1.0-build.120 — 2026-11-14

* **Tag:** `v0.1.0-build.120`
* **Status:** supported
* **Builds included:** dev.1 to build.120. New since 0.1.0-build.85: build.86 to build.120 (see CHANGELOG.md).
* **Formats emitted:**
  * **Default:** P0 0.27.0 for versions 1–7 and P0 0.33.0 (Arc, byte-grouped) for versions 8–31. This is the SDK and CLI default (Auto layout).
  * **Opt-in:** P0.1 0.28.0, 0.29.0, 0.30.0 and 0.32.0, plus 0.31.0 (Pixel, experimental, no specification yet).
  * **Other profiles:** P1 0.14.0, P2 0.25.0, P3 0.26.0.
* **Formats read:** the emitted formats, plus the legacy formats listed in the map.
* **C ABI:** 4. Everything since build.85 is additive.
* **Highlights:**
  * **Reader repair pass (build.86):** a new border search, a frame-edge corner fit with a lens term, erasure and soft-decision decoding, and a work budget. Also levels U and X, and byte-grouped placement (Formats 0.29/0.30).
  * **Arc (Formats 0.32/0.33, build.110):** Standard geometry with each ring's slot count scaled to its radius. Auto layout (`CKEY_LAYOUT_AUTO`, build.114) makes it the default from v8. Arc and grouped readers use per-group version keys (build.111), and the fast scan always compares three start sizes (build.115).
  * **Per-site scan heatmaps and opt-in detailed scan reports** in the SDKs.
  * **ES256 Safe Action issuer keys (build.118–119):** ECDSA P-256 with SHA-256, raw `r ‖ s`, low-S, and 33- or 65-byte SEC 1 keys. They are verified in the core and in every SDK. Signing goes through a key service that keeps the key (`ckey_safe_action_signing_input`, `ckey_es256_signature_from_der`, `ckey_safe_action_assemble`, and `ckey action signing-input` / `assemble` in the CLI). Ed25519 is unchanged.
  * **CI and releases on the self-hosted runner** (build.119).
* **Known limits:**
  * Experimental: formats are drafts and may change before 1.0.
  * Format 0.31 (Pixel) has no specification yet.
  * The release builds on one self-hosted Apple-silicon Mac. This release's archives are macos-arm64 only, and the NuGet package carries the osx-arm64 and osx-x64 natives only. Linux and Windows natives return when runners for them exist.
  * No independent security review yet, including of the P-256 verifier. It is cross-checked against Node's crypto and OpenSSL.

### CKey 0.1.0-build.85 — 2026-11-07

* **Tag:** `v0.1.0-build.85` (commit `ede38f5`)
* **Status:** superseded by CKey 0.1.0-build.120
* **Builds included:** dev.1 to build.85 (see CHANGELOG.md)
* **Formats emitted:** P0 0.27.0, P0.1 0.28.0, P1 0.14.0, P2 0.25.0, P3 0.26.0
* **Formats read:** the emitted formats, plus the legacy formats listed in the map
* **C ABI:** 4
* **Highlights:**
  * Safe Actions: Ed25519 envelope verification in the core and CLI, the specification, 22 independent vectors, and SDK-owned policy in every SDK. Adds `CKEY_EXPIRED`.
  * The field-test suite (build.84).
* **Known limits:** as 0.1.0-build.83.

### CKey 0.1.0-build.83 — 2026-11-06

* **Tag:** `v0.1.0-build.83`
* **Status:** supported
* **Builds included:** dev.1 to build.83 (see CHANGELOG.md)
* **Formats emitted:** P0 0.27.0, P0.1 0.28.0, P1 0.14.0, P2 0.25.0, P3 0.26.0
* **Formats read:** the emitted formats, plus the legacy formats listed in the map
* **C ABI:** 4
* **Highlights:**
  * Profile 0, the scalable family: 31 versions, L/M/Q/H correction, version identified from the symbol; Profile 0.1 print layout.
  * Native rendering to RGBA and gray pixels, PNG, SVG and vector PDF; styles with strict, adaptive and override policies; certification through a modelled camera sweep.
  * Full-frame scanning separate from the renderer, so a scanner can ship without it.
  * SDKs: Swift package (`CKey`, scan-only `CKeyScanner`), npm `openckey` (WebAssembly), Maven `org.openckey:ckey` (+ `ckey-compose`, `ckey-camerax`), NuGet `CKey` (+ `CKey.Wpf`), CLI with Homebrew formula, CMake package.
  * Optional aggregate telemetry (modes 0/1/2), never content or identifiers.
* **Fixes since the build.76 tag:** the .NET native library builds with MSVC (no `__int128` in the telemetry core) and exports only the C API on Linux; Debug and sanitizer builds link every test; the Windows struct-layout check ignores line endings; macOS jobs run on macos-15; the .NET SDK's C# compiles (it had not since build 67) and its 65 tests pass; the .NET native library is `ckeynative` (`ckeynative.dll`, `libckeynative.so`, `libckeynative.dylib`), so it no longer collides with the managed `CKey.dll` on Windows; style validation reads enum fields as raw integers (no undefined behaviour on out-of-range values); the Android host check tolerates missing optional jars; the npm package is `openckey` (the name `ckey` belongs to an unrelated package).
* **Known limits:** experimental. Formats are drafts and may change before 1.0. Decoding is verified on rendered and simulated captures only: real-camera robustness, latency and print compensation are not yet measured. No independent security review yet.


### CKey 0.1.0-build.82 — 2026-11-06

* **Tag:** `v0.1.0-build.82`
* **Status:** superseded by CKey 0.1.0-build.83. Its release built and verified, but npm refused the package name `ckey` (owned by an unrelated package), so nothing was published from it. Use `v0.1.0-build.83`.


### CKey 0.1.0-build.81 — 2026-11-06

* **Tag:** `v0.1.0-build.81`
* **Status:** superseded by CKey 0.1.0-build.82. Two CI checks (a sanitizer finding in style validation and a script error in the Android host check) stopped its release workflow, so no draft release was made from it. The .NET packages were built from this commit by hand; everything else should use `v0.1.0-build.83`.


### CKey 0.1.0-build.76 — 2026-11-06

* **Tag:** `v0.1.0-build.76`
* **Status:** not released. Its release build failed (MSVC could not compile the telemetry core; the Linux .NET library exported C++ runtime symbols; a Debug test did not link), so nothing was built or published from it. The tag stays, as tags are never moved or deleted. Use `v0.1.0-build.83`.
