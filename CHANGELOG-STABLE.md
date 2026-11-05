# Stable-Release changelog

This log lists the releases tagged on the `Stable-Release` branch, newest first. Each release is one immutable tag. The per-build history is in [CHANGELOG.md](CHANGELOG.md), and the [release and versioning map](docs/RELEASE-AND-VERSIONING-MAP.md) explains how to cut and deprecate a release.

**Releases so far: none.** The first release will be promoted from `scratch`.

## Entry template

Copy this block to the top of the list for each release, and fill it in.

```markdown
### CKey 0.1.0-build.N — YYYY-MM-DD

* **Tag:** `v0.1.0-build.N` (commit `abcdef0`)
* **Status:** supported  <!-- or: Deprecated YYYY-MM-DD, replaced by CKey 0.1.0-build.M, because … -->
* **Builds included:** build.A to build.N (see CHANGELOG.md)
* **Formats emitted:** P0 0.27.0, P0.1 0.28.0, P1 0.14.0, P2 0.25.0, P3 0.26.0
* **Formats read:** the emitted formats, plus the legacy formats listed in the map
* **C ABI:** 3
* **Highlights:** …
* **Known limits:** …
```

When a release is deprecated:

1. Change its GitHub Release title to `Deprecated - {release name}`.
2. Update its **Status** line here. Never delete the entry.

## Releases

_None yet._
