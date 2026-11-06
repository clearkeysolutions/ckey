# Stable-Release changelog

This log lists the releases tagged on the `Stable-Release` branch, newest first. Each release is one immutable tag. The per-build history is in [CHANGELOG.md](CHANGELOG.md), and the [release and versioning map](docs/RELEASE-AND-VERSIONING-MAP.md) explains how to cut and deprecate a release.

**Releases so far: 1** (`v0.1.0-build.78`; `v0.1.0-build.76` was tagged but never released).

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

### CKey 0.1.0-build.78 — 2026-11-06

* **Tag:** `v0.1.0-build.78`
* **Status:** supported
* **Builds included:** dev.1 to build.78 (see CHANGELOG.md)
* **Formats emitted:** P0 0.27.0, P0.1 0.28.0, P1 0.14.0, P2 0.25.0, P3 0.26.0
* **Formats read:** the emitted formats, plus the legacy formats listed in the map
* **C ABI:** 4
* **Highlights:**
  * Profile 0, the scalable family: 31 versions, L/M/Q/H correction, version identified from the symbol; Profile 0.1 print layout.
  * Native rendering to RGBA and gray pixels, PNG, SVG and vector PDF; styles with strict, adaptive and override policies; certification through a modelled camera sweep.
  * Full-frame scanning separate from the renderer, so a scanner can ship without it.
  * SDKs: Swift package (`CKey`, scan-only `CKeyScanner`), npm `ckey` (WebAssembly), Maven `org.openckey:ckey` (+ `ckey-compose`, `ckey-camerax`), NuGet `CKey` (+ `CKey.Wpf`), CLI with Homebrew formula, CMake package.
  * Optional aggregate telemetry (modes 0/1/2), never content or identifiers.
* **Fixes since the build.76 tag:** the .NET native library builds with MSVC (no `__int128` in the telemetry core) and exports only the C API on Linux; Debug and sanitizer builds link every test; the Windows struct-layout check ignores line endings.
* **Known limits:** experimental. Formats are drafts and may change before 1.0. Decoding is verified on rendered and simulated captures only: real-camera robustness, latency and print compensation are not yet measured. No independent security review yet.


### CKey 0.1.0-build.76 — 2026-11-06

* **Tag:** `v0.1.0-build.76`
* **Status:** not released. Its release build failed (MSVC could not compile the telemetry core; the Linux .NET library exported C++ runtime symbols; a Debug test did not link), so nothing was built or published from it. The tag stays, as tags are never moved or deleted. Use `v0.1.0-build.78`.
