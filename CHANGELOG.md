# Changelog

This is the development changelog for the `scratch` branch. It has one entry per build (`0.1.0-dev.N`), newest first. Every commit that changes the project adds an entry here, and `tools/check_version.py` checks that the top entry matches `VERSION`.

Related logs:

* [CHANGELOG-STABLE.md](CHANGELOG-STABLE.md): releases tagged on `Stable-Release`.
* [CHANGELOG-COMMUNITY.md](CHANGELOG-COMMUNITY.md): changes merged into `community`.
* [Release and versioning map](docs/RELEASE-AND-VERSIONING-MAP.md): which build introduced each format and ABI.

Builds dev.1 to dev.10 predate the repository history, which starts at dev.11 (`fe01f26`). Their entries are kept as written. Some later builds (for example dev.33 to dev.36 and dev.44) were recorded here but committed together with a neighbouring build.

## 0.1.0-build.108 — 2026-11-13

* **The site heatmap carries the frame it was read from** (Swift SDK). `CKeySiteHeatmap.readFrame` is the last kept frame (the frame that read, when the fast scan read it): the code's 8-bit gray crop and the field corners in it. Open CKey Scanner uploads it with detailed scan reports when the person opts in.
* The heatmap keeps at most 30 frames (was 40), to bound memory now that it can run in release builds.
* Swift not compiled here (parsed only).

## 0.1.0-build.107 — 2026-11-13

* **Per-site heatmap of a scan** (diagnostics). The owner wants to see which parts of a code read wrong while the scanner tries, so the weak areas can be fixed.
  * Core: `ckey_read_sites_gray8` re-reads every site of a known symbol at given corners and reports, per site, whether it read as printed (1), wrong (0) or not at all (255), with the reader's confidence. It tries all 8 corner orders and both the plain and the local-levels-plus-ring-sequence readings, and keeps the one that agrees best with the truth (`CKEY_HAS_READ_SITES`). A clean rendered frame re-reads with 0 of 3,719 sites wrong; the same frame smeared over 14 px has 1,446 wrong. Tested in `ml_tests`.
  * Swift SDK: `CKeyFrameProcessor.onSiteHeatmap` / `CKeyCameraScanner.onSiteHeatmap`. While it is set, the processor keeps the code's crop of up to 40 frames since the first candidate. After a read, it re-encodes the decoded payload at each level, keeps the level that best matches the last frame, and re-reads every kept frame against that truth (`CKeySiteHeatmap`: site positions and kinds from `ckey_ml_site_map`, the printed values, and per-frame correctness). The work runs on a utility queue. Leave it nil in production.
  * On the first capture the core re-read, the 13 wrong sites in the frame that read were all ring marks (0 of 7,027 grid modules, 0 of 285 dial rays, 13 of 4,829 ring marks).
* **A tracked box under 96 px is ignored** (`CKeyFrameProcessor.minimumTrackedSide`). In a field capture the tracker locked onto a round Dock icon (a 68 × 70 px crop) and the scanner kept reading only that crop.
* Swift not compiled here (parsed only).

## 0.1.0-build.106 — 2026-11-13

From a field capture on the owner's iPhone. Built optimized, fast scans took 114 to 143 ms, against 545 to 1,014 ms in the Debug build, and the frame that read took 51 ms. The capture showed three delays.
* **A stale tracked box cut the code off.** The tracker holds its last corners through missed frames. The processor scanned around those corners while the code moved, so two sharp frames were cropped through the code; the reader cannot read those crops even classically and without a time limit. A held track's box now gets a 60% margin, and a freshly measured one 25% as before (`trackedRegion(_:width:height:margin:)`).
* **The full reader no longer blocks the camera loop.** One full-ladder attempt on a blurred frame took 1.14 s, during which no frame was scanned.
  * The analysis queue now runs the fast scan on every frame.
  * The full reader runs beside it on its own queue, on a copy of the frame, one at a time, for a tracked code the fast scan has missed twice in a row. The multi-frame accumulator belongs to that queue.
* **The exposure cap is 1/250 s.** At 1/120 s, frames at 2× zoom were still smeared by hand movement.
* Not compiled here.

## 0.1.0-build.105 — 2026-11-13

* **Per-frame diagnostic reports** (Swift SDK), for charting a scan from the first candidate to the read.
  * `CKeyFrameProcessor.onFrameReport` reports every analysed frame (`CKeyFrameReport`):
    * time, fast or full scan, the area scanned and the scan time;
    * located, tracked and decoded, the candidate score and the decoded version;
    * the advice's code size, sharpness, contrast, clipping and zoom;
    * a 32-bin luma histogram of the code's box.
  * `CKeyCameraScanner.onFrameReport` adds the camera's exposure, ISO, lens position and zoom, on the main queue.
  * Nil by default, and meant for debug builds only.
  * Not compiled here.

## 0.1.0-build.104 — 2026-11-13

* **Diagnostic frame capture** (Swift SDK), so the reader can be measured on real camera frames instead of simulated ones.
  * `CKeyFrameProcessor.captureDiagnostics(count:completion:)` keeps the next analysed frames. Each is the 8-bit luma cropped to the area that scan read, at full resolution (whole frames centred and capped at 2,048 pixels on a side), with:
    * the candidate's corners and the tracked corners, in crop pixels;
    * whether it decoded, whether it was a fast scan, and its time.
  * `CKeyCameraScanner.captureDiagnostics` adds the camera's exposure, ISO, lens position and zoom.
  * A decode ends the capture early.
  * Nothing leaves the device unless the host shares it.
  * Not compiled here.

## 0.1.0-build.103 — 2026-11-13

* **Smooth pixel-perfect drawing:** new `ckey_render_scalable_aligned_raster` (`CKEY_HAS_ALIGNED_RASTER`).
  * The full renderer draws on the lattice's pixel grid. Every grid module is an exact whole-pixel dot, so nothing is resampled. Rings, rays, the frame and the finders are antialiased, and colours, materials and the logo are drawn.
  * Snapping every mark to whole cells made dense codes look jagged (owner report, with screenshots).
  * The Web SDK's `renderPixelPerfect`, `renderToCanvasPixelPerfect` and `renderPNGPixelPerfect` and the Swift SDK's `pixelPerfectPixels`, `pixelPerfectImage` and `pixelPerfectPNG` use it. `renderLattice` and the lattice SVG are unchanged.
  * Tests: the aligned raster has the lattice's side, decodes, and has antialiased pixels (Standard v8 and v26, Print v20).
* **Live scanning uses less power** (owner report: the phone heats up).
  * `CKeyFrameProcessor` analyses at most 8 frames a second until a code is tracked (`searchFramesPerSecond`), and up to `maxFramesPerSecond` while tracked.
  * The full reader ladder now runs only on a tracked code that two fast scans in a row failed to read, on one of every three frames. With nothing tracked it never runs.
  * Not compiled here.

## 0.1.0-build.102 — 2026-11-13

**Live scanning reads where the code is, not just inside the reticle.** From a screen recording: the scanner locked its brackets on sharp, well-lit codes (a 122-byte Standard code and the 2,022-byte Pixel one) but did not read them for several seconds.
* **Cause.** Both codes were held a little larger than the reticle. Two of every three analysed frames read only the reticle region plus an 18% margin, which cut the code off. Before build 101, each such doomed scan also ran the full reader ladder. Only every third frame was read whole.
* **Swift SDK.**
  * While the tracker holds a code, `CKeyFrameProcessor` reads the tracked corners' bounding box plus a quarter of its size, on every frame (`trackedRegion`).
  * The reticle margin (`CKeyDecoder.regionMargin`) is 35%, up from 18%, so a code larger than the reticle reads before the track locks.
  * Not compiled here.

## 0.1.0-build.101 — 2026-11-13

**Live scanning: frames that will not read give up fast, and frames come out sharper.**
* **Why live scanning was slow when a photo reads at once.**
  * The camera loop analyses one frame at a time. Every frame that failed (motion blur, refocus) ran the whole reader ladder at 4K: the ML locator, the corner-refiner retry, the site model, multi-frame averaging, 6 candidates and 8 version hypotheses each.
  * Measured here, a 4K frame smeared by 9 to 12 pixels took **1.6 to 1.8 s** to fail, against 45 to 90 ms for a frame that reads. While the hand moved, the scanner looked at about one frame a second.
* **`CKEY_SCAN_OPTION_FAST`** (8): the classical reader only for that call, with no locator, refiner retry, site-model reading or averaging, two version hypotheses and two reading strategies. Acceptance is unchanged.
  * On 48 live-like 4K frames (versions 4 to 27, 2.5 to 7.5 pixels per module, 0 to 10 pixels of motion blur), with 2 candidates against 6:
    * the full ladder decoded 23/48, at 630 ms per frame on average and 1,125 ms per failing frame;
    * the fast scan decoded 18/48, at 197 ms on average and 293 ms per failing frame.
  * A smeared frame in the core test fails in 225 ms fast against 1,244 ms full.
* **Swift SDK.**
  * `CKeyFrameProcessor` scans three of every four analysed frames fast and runs the full ladder on the fourth (`fullLadderEvery`).
  * `CKeyCameraControl` caps auto-exposure at 1/120 s (`maxExposureTimescale`) when it controls exposure. The camera raises ISO rather than smearing a handheld code.
  * `CKeyCameraScanner` turns video stabilization off on its output, since stabilization delays and crops frames.
  * Not compiled here.

## 0.1.0-build.100 — 2026-11-13

**Pixel-perfect drawing uses the pixels it has for detail.** In build 96 to 99 the renderers drew the lattice at its minimum size (6 cells per module for Standard, 3 for Print) and repeated each cell over several screen pixels. On a typical preview that made blocky rays and blob-like ring marks.
* **Core:**
  * `ckey_render_scalable_lattice` accepts 1 to 32 cells per module.
  * Module dots are about 0.68 of the module at every size: `c − 2·round(0.16c)` cells, and 2 of 3 and 3 of 4 for Print and Pixel.
  * From 12 cells per module, marks are snapped at half coverage, which draws them at their true width.
  * Pixel's centreline rays apply only at 1 and 2 cells per module.
  * Clean decodes at 3 to 32 cells per module, versions 2, 8, 14 and 21: 44/44 for Print, 44/44 for Pixel, and 41/44 for Standard (all 3 misses at 3 cells, below its minimum of 6).
* **Web SDK:**
  * New `pixelPerfectFit(sym, maxDevicePx)` picks the most cells per module that fit at one pixel per cell, then whole pixels per cell beyond 32.
  * `renderToCanvasPixelPerfect` with `cssPx` uses it.
* **Swift SDK:** new `pixelPerfectFit(pixels:)`, which `CKeyView` uses.

## 0.1.0-build.99 — 2026-11-13

* **Pixel layout: the lattice defaults to 2 cells per module.** At 1 cell per module the data rays are a full module wide. A version-28 symbol drawn that way did not decode even shown at 2 to 4 screen pixels per cell. At 2 cells per module it decodes at 1 and 2 pixels per cell, as every tested version did in build 98.
  * Checked in a browser: the 2,022-byte example in the Pixel layout is 402 device pixels wide on a ratio-1 display and decodes. The same text needs 604 pixels in Print and 1,052 in Standard; a QR code at 2 pixels per module is 354.

## 0.1.0-build.98 — 2026-11-13

**Experimental Pixel layout** (`CKEY_LAYOUT_PIXEL` = 4, Format 0.31, format byte `1F`, opt-in): a Profile 0 geometry family designed to be drawn pixel-perfect at QR-like density.

* **Geometry.** The Print family's frame, finders, grid pitch, dial and keys, with three changes:
  * the ring zone's data sits on grid modules filling the annulus out to the dial;
  * a single ring at the ring zone's inner radius carries only the Ring Key;
  * rays are two modules apart, with short rays 0.6 module, so a short ray ends 1.1 modules before the ray sample.

  At one cell per module every data site is then a distinct cell. Capacity is within 5% of Print's at small versions and under 1% below it at large ones: level M, version 26, 1,659 bytes against 1,677.
* **Lattice.** `ckey_render_scalable_lattice` defaults to 1 cell per module for Pixel and draws its data rays along their centrelines. For every layout, a grid module's block now belongs to its data alone: traced marks that spill into it are cleared.
* **Measured on clean renders** (8 versions from 1 to 31, one payload each, at 1, 2 and 3 pixels per cell):
  * the antialiased raster decodes 8/8;
  * the lattice at **2 cells per module decodes 24/24**, comparable to a QR code at 2 pixels per module;
  * at 3 cells per module it decodes 24/24;
  * at **1 cell per module it decodes 17/24**.
  * At one cell per module the drawing is exact (every data site's cell matches its bit). The failures are in the reader:
    * corner registration lands 1 to 9 pixels off;
    * version 31's keys are not found at 1 to 2 pixels per module.

  Reader work for one cell per module continues.
* **The reader** tries the Pixel family in its version search. It has no byte-grouped placement, so the grouped retry is skipped. Calls that indexed layouts by family number now go through `scalable_family_layout`.
* **SDKs:**
  * Web: `layout: 'pixel'`; a decode reports profile `'0'` and layout `'pixel'`.
  * Swift: `CKeyLayout.pixel`.
  * Telemetry counts Pixel as Profile 0.
* **Readers before build 98 cannot read Pixel symbols.**

## 0.1.0-build.97 — 2026-11-12

* **Pixel-perfect lattice, Print family:** grid modules are drawn as a dot one cell smaller than the module (2 of 3, 3 of 4 cells across) instead of Standard's `c − 2`.
  * Under a sub-pixel display model (RGB stripes and black matrix), the build-96 one-cell dot at the default 3 cells per module read 6/9 at version 14 and 5/9 at version 28; it now reads 9/9 and 8/9. A lone one-cell dot is mostly stripe and black matrix to a camera.
  * Solid blocks were also tried. They fail clean decodes of versions 26–31 at 3 pixels per cell, where large solid areas merge.
  * Clean decodes at 3 cells per module: 278/279; at 4: 279/279. The ideal-display sweep is unchanged or better (Print, 3 cells: 120/120).
  * Standard is unchanged.

## 0.1.0-build.96 — 2026-11-12

**Pixel-perfect rendering** in the core, the Web SDK and the Swift SDK.

* **`ckey_render_scalable_lattice`** (`CKEY_HAS_LATTICE`) draws a Profile 0 symbol on whole cells.
  * Every grid module is exactly `cells_per_module` × `cells_per_module` cells, drawn as a centred dot from the data: 1, 2, 3 or 4 cells across at 3, 4, 5 or 6 cells per module, or the whole block at 1 and 2. The reader's mark model needs gaps between dots.
  * Every other mark (frame, finders, rings, rays, Ring Key) is snapped to the cells it covers by at least 10%.
  * `cells_per_module` 0 selects the measured default: **3 for the Print family, 6 for Standard**, whose ring dots are 0.2 of a module against Print's 0.3.
  * Clean decodes over 3 payloads at each of 31 versions and 3 screen scales: Print 2 cells 272/279 (version 11 fails), **Print 3 cells 278/279**, Standard 5 cells 276/279, **Standard 6 cells 279/279**. No wrong payloads.
  * This replaces the experimental build-95 tracing, which did not decode.
* **Web SDK:** `renderLattice`, `renderPixelPerfect`, `pixelPerfectScale`, `renderToCanvasPixelPerfect`, `renderPNGPixelPerfect` and `renderSVGPixelPerfect`.
  * `renderToCanvasPixelPerfect` picks whole device pixels per cell from `cssPx` and `devicePixelRatio` and sets the canvas's CSS size, so one canvas pixel is one device pixel.
  * Bridge `ckw_render_lattice`. Native, fake-module and WebAssembly tests: the output is exactly two colours and decodes.
* **Swift SDK:**
  * `CKeyLattice`, `lattice(cellsPerModule:)`, `pixelPerfectPixels`, `pixelPerfectImage`, `pixelPerfectPNG` and `pixelPerfectSVG`.
  * **`CKeyView`, `CKeyUIView` and `CKeyNSView` now draw pixel-perfect** instead of the antialiased raster at the view's size. `policy` now applies only to `cgImage` and `pngData`.
  * Not compiled here (no Apple toolchain).
* `rasterize_scene` takes an optional canonical origin and scale.

## 0.1.0-build.95 — 2026-11-12

* **Experimental `ckey_render_scalable_lattice`** (`CKEY_HAS_LATTICE`): draws a Profile 0 symbol on a grid of whole cells, `cells_per_module` (1–4) cells per grid module, with every mark snapped to cells, for pixel-perfect display and export. A prototype toward one pixel-perfect renderer for the app, the site and the SDKs; not used by any of them yet.
  * Measured on a 2,022-byte version-26 symbol (164 modules), zero wrong payloads throughout: with grid modules drawn as centred dots from the site map, 3×3 cells per module (526 px) decodes from about 2 camera pixels per screen pixel, and 4×4 (702 px) at every distance tried. Solid 1×1 and 2×2 cells (176 and 352 px, QR-like density) do not decode even from true corners: the reader models round dots with gaps and ring dashes, and the Ring Key's tick lengths need a 10% coverage rule to survive snapping.
* `rasterize_scene` takes an optional canonical origin and scale (default unchanged).

## 0.1.0-build.94 — 2026-11-12

* **Swift SDK compile fix** (`CKeyCameraControl.swift`): `guard snaps < 2, change > (…)` was parsed by Swift as a generic specialization `snaps<2, change>`, which failed the iOS build ("Cannot specialize non-generic type 'Int'"). It is now two separate conditions. Behaviour is unchanged.

## 0.1.0-build.93 — 2026-11-12

**The ML locator is built in**, and the Swift SDK uses all three models by default, with a switch to turn them off.

* **Built-in locator `locator-v1`** (`src/ml_builtin_locator.inc`; key-test `ml/locator-models/locator-v1.*`): 444,648 bytes, sha256 `f2a78390…`. It proposes symbol corners and a version hint when the border search finds nothing. Measured on 1,200 fresh-seed cases (never used in training), identical frames, 4-core container:

| | Classical | ML proposals first | ML after classical |
|---|---|---|---|
| Valid symbols decoded | 56.4% (609/1080) | 59.1% (638/1080) | **59.1%** (638/1080) |
| Readable from true corners, decoded | 83.1% | 86.6% | **86.6%** |
| Dense (v25–31), readable, decoded | 103/133 | 114/133 | **114/133** |
| Wrong payloads | 0 | 0 | 0 |
| Frames that decoded, median | 46 ms | 77 ms | **48 ms** |
| Frames that did not decode, median / p95 | 173 / 774 ms | 317 / 1067 ms | 311 / 1076 ms |

  * +29 reads, none lost. After the classical search it gains the same reads as running first, and frames the classical reader reads pay about 2 ms. Frames that read nothing take longer (they try the proposals too).
* **Swift SDK:** `CKeyDecoder` scans (camera frames and photos) set `CKEY_SCAN_OPTION_ML_AFTER`. New `CKeyDecoder.setMachineLearning(enabled:)` (process-wide; off makes scanning exactly classical, through `ckey_ml_clear_models`) and `CKeyDecoder.machineLearningEnabled`. The Swift changes are not compiled here (no Swift toolchain in this container).
* **Site model:** `sites-v2` (doubt channel) read exactly as many as `sites-v1` (57.3% on the same 1,200 cases), so `sites-v1` stays built in.

## 0.1.0-build.92 — 2026-11-12

**The corner refiner is built in**, and the locator can run after the classical search instead of before it.

* **Built-in corner refiner `refiner-v1`** (`src/ml_builtin_refiner.inc`; key-test `ml/locator-models/refiner-v1.*`, commit `294de49`): width 3, 450k weights, 473,276 bytes, sha256 `9fd91f51…`. It was trained on the owner's Mac with 60,000 fresh-seed cases. It runs only when a Profile 0 decode fails from the given corners: the symbol is rectified through them, the model corrects each corner (two rounds), and the decode is retried once.
  * **Measured** against the classical reader on identical frames, fresh seeds never used in training:

| | Classical | With refiner |
|---|---|---|
| 2,000 cases (owner's Mac): full scan | 59.5% | **63.3%** (+69, lost 0) |
| 1,200 other cases (4-core container): full scan | 56.4% | **61.4%** (+54, lost 0) |
| 1,200 cases: from corners 0.6 module off | 3.9% | **66.2%** |
| Wrong payloads | 0 | 0 |

  * The gains are largest under motion and defocus, ring stress and colour.
  * **Cost:** only frames whose first decode fails run it. The full-scan 95th percentile rose from 250 to 470 ms on the owner's Mac, and from 598 to 1,111 ms on the busy container; medians rose 6–24 ms. Device timing on the CPU path is not measured yet.
* **`CKEY_SCAN_OPTION_ML_AFTER`:** the locator's proposals are tried only after the border search found nothing that decodes, so frames the classical reader reads never pay for the locator. With `CKEY_SCAN_OPTION_ML` the proposals still come first. Test: a frame the border search reads never uses a proposal.

## 0.1.0-build.91 — 2026-11-11

**The Swift SDK now has everything the iPhone app's scanner had**, so the app can be a thin client of it (Open CKey Scanner on openckey.org `main` now is). Reading other barcode symbologies stays in the app by design.

**Swift SDK** (`CKeyScanKit`; **not compiled here**, there is no Swift toolchain in this environment)

* **Multi-frame reading on by default:** `CKeyCameraScanner.multiFrame` / `CKeyFrameProcessor.multiFrame` (default `true`) scan through `ckey_scan_frame_accumulate_gray8` with one accumulator per processor, reset by `scanAgain()` and `reset()`. `CKeyScanOutcome.accumulated` marks a read from the average. Measured in the key-test harness (1,080 six-frame sequences with shake, motion and focus and exposure drift): 63.5% of codes read on some single frame, 66.3% with the accumulator; 67.4% with the built-in site model as well; 0 wrong payloads.
* **Rotation-aware preview:** `CKeyCameraPreview(session:geometry:interestRect:onRegionOfInterest:)` and `CKeyPreviewView.attach(_:)` keep the image upright in every orientation and posture with `AVCaptureDevice.RotationCoordinator` (iOS 17 and later), and report the reticle in sensor coordinates for `scanRegion`. The existing initialiser still works.
* **Camera access:** `CKeyCameraScanner.cameraAuthorization` and `requestCameraAccess()`.
* **Torch:** `isTorchAvailable`, `setTorch(_:)`.
* **Photos and screenshots:** `CKeyDecoder.decode(imageData:)`, with the orientation applied, at 4,096 then 2,048 px.

**Core**

* The frame accumulator now works on region-of-interest scans too: the SDK's default, two of every three frames. The captured geometry is taken in the region and continuity is checked in full-frame coordinates; corners are reported in full-frame pixels. New test case in `ckey_ml_locator`.
* `ckey.h`: the accumulator's description now matches what it does (averaging rectified fields).

## 0.1.0-build.90 — 2026-11-11

Three more ML assists and a way to turn ML off. No new built-in model; the corner refiner and the two-channel site model are being trained (key-test `ml/run_next.sh`).

**Reader (core)**

* **Version hint:** bits 8–15 of `ckey_scan_options.flags` (`CKEY_SCAN_VERSION_HINT(v)`) give a Profile 0 version. Versions within one of it pass their key fit at a lower gate (0.6 instead of 0.85) and are tried first. With `CKEY_SCAN_OPTION_ML` and no hint, the locator's density estimate is the hint, within two versions. Only the order and the candidates change; Reed–Solomon and CRC-32C decide. **Measured** with the true version as the hint (its ceiling), on 1,200 fresh-seed cases: full scans 56.4% → **57.6%** (+13, lost 0, wrong 0), dense symbols 113 → 117 of 244, time unchanged.
* **Corner refiner** (model kind 3: one output cell, 8 channels): when a Profile 0 decode fails from the given corners, the symbol is rectified over canonical [−3, 32]² at the model's side through those corners, and the model's outputs correct each corner in canonical units, two rounds. The decode is then retried once from the refined corners, and the message says "(ML corners)". `ckey_ml_refiner_model_id`; `ckey_ml_rectify_region_gray8` for training data.
* **Site model doubt channel:** a site model may have a second output channel, the logit that its own reading of a site is wrong (trained on its mistakes: occluders, glare, damage). It scales the site's confidence, so the soft decoder erases what the model distrusts first. One-channel models work as before.
* **`ckey_ml_clear_models`:** unloads every model, built-ins included, until the next `ckey_ml_load_model` (NULL restores the built-ins). Scanning is then exactly classical. The key-test comparisons use it for their classical arms. Without it, the built-in site model of build 89 would have run in both arms.
* Tests: `ckey_ml_locator` covers the refiner (recognised; a working decode is untouched; from far-off corners only the true payload or nothing), the version hint (any hint, right or wrong, still reads the true payload) and clearing.

## 0.1.0-build.89 — 2026-11-11

**The first built-in ML model:** the site model, read only on frames the plain reading fails.

* **Model:** `sites-v1` from `clearkeysolutions/key-test` (`ml/locator-models/sites-v1.*`, commit `1d93dc4`): width 2.5, 90k weights, 100,616 bytes, sha256 `4576bcd5…`. It was trained on the owner's Mac with 40,000 fresh-seed cases, oversampling the fields the classical reader fails, against ckey `b0299d1`. Embedded as `src/ml_builtin_sites.inc` by the new `tools/embed_ml_model.py`; the new `ckey_ml_builtin_models` test checks the embedded bytes against the recorded SHA-256. Define `CKEY_ML_NO_BUILTIN` to build without it; `ckey_ml_load_model` replaces it at run time.
* **Measured** (key-test harness, fresh seeds never used in training, classical reading against the site model on identical frames):

| | Classical | With site model |
|---|---|---|
| 2,000 cases (owner's Mac): decoded from the true corners | 65.2% | **66.8%** (+29, lost 0) |
| 2,000 cases: full scan | 56.9% | **58.1%** (+20, lost 0) |
| 1,200 other cases (4-core container): from the true corners | 66.1% | **67.6%** (+16, lost 0) |
| 1,200 cases: full scan | 56.4% | **57.3%** (+10, lost 0) |
| Wrong payloads, both sets | 0 | 0 |

  The gains are in resolution, print spread, screens and moiré, motion, rotation and dense symbols (v25–31: 225 → 232 of 408). Uneven light and finder loss are unchanged; there the symbol is lost before reading, which is the locator's job.
* **Cost:** only frames whose plain reading fails run the model. Median scan time is unchanged. On those frames the 95th-percentile full-scan time rose from 321 to 466 ms on the owner's Mac, and from 807 to 1,146 ms on a busy 4-core container. Device timing on the CPU path is not measured yet; the planned Core ML path runs the same weights on the iPhone Neural Engine.
* Built-in locator: none yet (training).

## 0.1.0-build.88 — 2026-11-11

The ML site model runtime and multi-frame reading. As with build 87, **no trained model is built in yet**: with no model loaded, single-frame scanning is unchanged. The accumulator is new behaviour behind its own call.

**Reader (core)**

* **ML site reading** (`src/ml_sites.inc`): with a site model loaded, the Profile 0 decoder adds one reading strategy, right after the plain one. The candidate's field is rectified at two samples per module through the decoder's own homography, so grid modules, ring dots and dial rays are all covered. A small fully convolutional network turns it into an ink logit per module cell, and each site's logit gives its value and its confidence, which becomes the per-byte reliability of the soft decoder (build 86). Reed–Solomon and CRC-32C decide, as for every other strategy. On identical inputs the C++ logits match PyTorch to within 2e-4. The site model shipped for training is about 17k weights and 1,100 multiply-adds per rectified sample (235M for a version 31 field); the model file is about 22 KB.
* **Model kinds:** CKML files now carry either a locator (stride 4, 24 channels) or a site model (stride 1 or 2, one channel), recognised from the header. Shapes are worked out per call, so a site model runs on any field size. `ckey_ml_load_model` replaces only the model of its own kind; `ckey_ml_site_model_id` identifies the site model.
* **Multi-frame reading:** `ckey_scan_frame_accumulate_gray8` with a `ckey_frame_accumulator` (create, reset, destroy, frames). When a frame does not decode, the field of its most plausible reading is rectified into canonical coordinates, standardised, matched to the running average in each of the four quarter-turn orientations, and added. The average is then decoded as a clean frame by the normal decoder, which identifies the version itself. That is why the sum does not depend on any single noisy frame's version guess. Continuity: the centre must be within half the symbol's size and the size within 25%, and a frame must correlate with the average. A stray frame is skipped; three in a row restart the average. `CKEY_SCAN_FLAG_ACCUMULATED` marks a read from the average. In `ckey_ml_locator`, a symbol under noise that no single frame decodes reads after two frames, and two different symbols alternating in one place yield only their own payloads.
* **Tooling for training data:** `ckey_ml_rectify_gray8` (the field through given corners, at a chosen sampling) and `ckey_ml_site_map` (every site's canonical position, kind and printed value for a symbol).
* Additive: the C ABI stays 4.

**Known limits**

* Under extreme noise the average registers each frame through that frame's own corners, and those corners are noisy too. Pixel-aligned averaging of a code that does not move still does better there (it read at a noise level where the accumulator did not). Aligning each frame to the average directly is the next step.

## 0.1.0-build.87 — 2026-11-11

The ML locator runtime: the first part of on-device ML assistance for scanning. It proposes where symbols are; the deterministic reader still decides what they say. **No trained model is built in yet**, so with this build alone scanning is unchanged. The model is trained with the key-test pipeline (`ml/run_locator.sh` in `clearkeysolutions/key-test`) and will be embedded once its evaluation on identical frames shows it helps.

**Reader (core)**

* **ML locator** (`src/ml_locator.inc`): a portable, dependency-free inference engine for small convolutional networks (convolution, depthwise convolution, ReLU, add, nearest upsample-add), with a detection decoder. From a whole frame shrunk to 256 × 256 it proposes each symbol's four field corners in symbol corner order, a presence score, the probability that the symbol reads from those corners, the version and the layout. Its output matches PyTorch to within 1e-6 on the same input. One frame takes about 35 ms on a busy 4-core x86 container (0.41M weights, about 195M multiply-adds); on iPhone the same weights are meant to run through Core ML.
* **Model files** (CKML version 2): int8 weights with a per-channel scale, and a graph description, so a larger network needs no new runtime code. A model can arrive as a data asset, so `ckey_ml_load_model` validates it completely before use: every size, buffer index, channel count and shape, the output head, and no trailing bytes. Every truncation of a valid model, header bit flips and 2,000 random files are tested, under AddressSanitizer and UBSan too.
* **Scan integration:** `ckey_scan_options.flags` (formerly `reserved`, same size) takes `CKEY_SCAN_OPTION_ML`, which tries the locator's Profile 0 proposals (refined, then decoded) before the border search, and `CKEY_SCAN_OPTION_ML_GATE`, which skips the border search when a loaded model sees no symbol. That is the expensive part on frames that do not decode. `CKEY_SCAN_FLAG_ML` marks a read that came from an ML proposal. Proposals are only candidates: a payload is accepted only after Reed–Solomon and CRC-32C, exactly as before.
* **New calls:** `ckey_ml_locate_gray8`, `ckey_ml_load_model`, `ckey_ml_model_id`, struct `ckey_ml_detection`. Additive: the C ABI stays 4.
* New test suite `ckey_ml_locator`: parser rejection; a model that proposes garbage everywhere never yields a wrong payload and the border search still reads; with no model the ML option changes nothing; the gate.

## 0.1.0-build.86 — 2026-11-10

The repair pass for the CKTEST-RH-001 hardening campaign. It implements `Pretest-observations-for-repair.md` (now deleted; what remains open is listed in `CONTEXT.md`). The source tree is back on `scratch`, restored from `Stable-Release` at `ede38f5`.

**Measured.** key-test harness (`clearkeysolutions/key-test` at `9ff83e3`), the first 300 structured cases of each of the 20 strata (6,000 cases; the plan was regenerated locally and its shard hashes match the campaign's evidence), build 85 against this build on identical frames:

| | Build 85 | Build 86 |
|---|---|---|
| Valid symbols decoded (5,401) | 50.0% | **59.1%** |
| Physically recoverable symbols decoded (3,267) | 64.4% | **76.0%** |
| Recoverable, standard layout (Format 0.27) | 60.8% | 70.5% |
| Recoverable, print layout (Format 0.28) | 68.2% | 81.5% |
| Wrong payloads (6,000 cases, 599 of them hard negatives) | 0 | 0 |
| Reader time per frame, median / 95th percentile | 41 / 187 ms | 88 / 536 ms |
| Reader time, frames that decoded, median / 95th percentile | 32 / 62 ms | 49 / 120 ms |
| Reader time, frames that did not decode, median | 77 ms | 261 ms |

Recoverable symbols decoded, by stratum (build 85 → build 86): S01 clean 99.5 → 99.5; S02 96.3 → 97.9; S03 resolution 69.7 → 77.8; S04 perspective and bends 40.6 → 69.4; S05 rotation 92.8 → 97.0; S06 defocus 31.7 → 42.2; S07 motion 33.8 → 41.0; S08 low light 88.5 → 94.2; S09 contrast and shadows 27.4 → 60.0; S10 glare 39.0 → 63.4; S11 occlusion 72.4 → 62.1 (29 cases); S12 print 58.0 → 68.2; S13 screens 61.5 → 73.7; S14 compression 80.7 → 87.4; S15 colour 54.4 → 86.0; S16 clutter and crops 87.6 → 93.8; S17 75.6 → 77.7; S18 46.9 → 60.3; S19 lens and finder loss 72.0 → 79.3; S20 88.6 → 94.4. 562 cases were gained and 71 that build 85 read are now missed (net +491). Times are host times on a 4-core container with four workers running, so they compare the two builds but are not device figures.

**After the ring work** (commit `35f9c1b`: physical ring model, lens estimated from the rings, per-ring kernel learning), same 6,000 cases: 59.6% of valid symbols and **76.5%** of recoverable ones decoded (standard layout 71.5%, print layout 81.6%); 0 wrong payloads; 74 cases that build 85 read are missed; reader time per frame median 105 ms and 95th percentile 517 ms (decoded frames 62 ms median, undecoded 269 ms). That is 0.5 points over the table above for about 20% more time. In individual frames the ring detector takes ring errors from over 20% to almost none, but most remaining failures have other causes (blur of 0.4 module or more, bent surfaces, glare, low resolution), so the aggregate moves little.

**After the bent-surface work** (commits `9bb5b1b`, `5a5455b`), same 6,000 cases against the ring-work result: 60.2% of valid symbols and **77.2%** of recoverable ones decoded (standard layout 72.3%, print layout 82.2%); 25 more recoverable symbols read and none lost; 0 wrong payloads. The gain is where it was aimed: the perspective and bends stratum (S04) went from 70.0% to 81.1%, and of the 45 recoverable symbols on a bent surface 20 now decode, where none did. Time, on 120 frames of each kind on an idle machine: frames that decode are unchanged (median 54 ms); frames that do not decode have a median of 258 ms (283 before, from a cheaper point sample) and a 95th percentile of about 1.0 s (0.6 s before).

No wrong payload in either run. This is a simulated, host-side comparison on a subset, not the campaign's full regression or holdout, and it does not qualify the reader. The gap to 100% and its causes are in `CONTEXT.md`.

**Reader (core)**

* **Border search** (`ckey_find_borders_gray8`): works on an area-averaged reduction of the frame instead of reading one pixel per block (the cause of screen moiré bands), and adds two local-threshold passes to the whole-frame one, so a symbol separates whatever the rest of the frame looks like (bright screen in a dark room, low contrast, colour). Outlines are fitted as perspective quadrilaterals at any tilt. A strong candidate from the first pass skips the others.
* **Corner refinement** (`ckey_refine_quad_gray8`): the frame stroke's edges are located directly (sub-pixel, as a paired stroke so a nearby lone edge is not taken for the frame) and fitted as four lines; three starting sizes are tried, because the border search sometimes returns the stroke's inner rim. Radial lens distortion is estimated from the bow of the sides and used for the rest of the call. Profile 0 then registers on the module lattice itself, each corner held near the frame fit. The unbounded key hill-climb is gone.
* **Decoding** (`ckey_decode_scalable_gray8`): sites outside the image are unknown instead of failing the frame; levels are taken from the symbol's own modules; local levels and washed-out regions (glare, shadow, occlusion) are found per neighbourhood and washed-out sites become erasures; blur leak between neighbouring marks is measured by least squares and cancelled; ring dots have a sequence detector; dial rays are read at the radius that splits best (ink spread). Four reading strategies are tried on one set of samples, and Reed–Solomon and the CRC decide.
* **Bent surfaces:** when no flat reading decodes and the frame's sides are visibly curved, the decoder fits a cylinder (a label on a bottle, a curled page). The version comes from the period of the module lattice, because a bend misplaces the keys; the corners from curved frame sides; the cylinder's axis, curvature and the camera's focal length from the modules themselves (module centres vary, the points where four modules meet stay clear); and the camera's pose from the four bent corners, which makes the model exact for a cylinder. Corners and surface are then refined together against the measured frame edge. Bends up to a half-angle of about 0.4 radian are read; see `CONTEXT.md` for what is not.
* **Erasure and soft-decision decoding:** `ckey_decode_scalable_soft` takes a reliability per codeword byte. After plain correction fails, the least reliable bytes are erased in capped steps (2 × errors + erasures ≤ parity, with two spare syndromes kept as a check). `erasures_used` is reported.
* **Work budget:** every image sample draws on a per-call allowance; when it runs out the call returns "no read". Candidate counts, refinement passes and decode attempts are capped.
* **New calls:** `ckey_scan_frame_gray8` (one call for search, refine and decode, with a zero-copy region of interest and an optional caller-supplied lens `k1`), `ckey_track_gray8` / `ckey_track_seed` / `ckey_match_corner_order` (per-frame corner tracking with the continuity, lock and corner-order rules that worked in the iOS app), and `ckey_camera_advise_gray8` (exposure bias, zoom, metering point and user guidance, with the 0 / −1 / −2 EV cycle). Additive: the C ABI stays 4.
* The acceptance path is unchanged: a payload is accepted only after Reed–Solomon and CRC-32C.

**Formats and codec**

* **Formats 0.29 and 0.30** ([spec](spec/CKey-Format-v0.29.0.md), [print](spec/CKey-Format-v0.30.0.md)): byte-grouped placement for both layouts (`CKEY_LAYOUT_STANDARD_GROUPED`, `CKEY_LAYOUT_PRINT_GROUPED`). Each byte's eight bits sit on neighbouring sites, and a keyed shuffle spreads each Reed–Solomon block over the field. Geometry, capacity and appearance do not change. The reader samples once and tries both placements. In `tests/hardening_tests.cpp` a 5% blot at level M reads 6 of 6 times grouped and 0 of 6 scattered. Opt-in: older readers cannot read them, and the default layout is still Format 0.27.
* **Levels U (80% parity) and X (95%)** in the encoder, renderer and reader, for all four formats. Capacities match the table in the repair notes (v31: 1,285 and 303 bytes). Older readers reject them.

**SDKs and tools**

* **Swift:** the scanner now controls the camera by default (`CKeyCameraScanner.controlsCamera`, with `focusOnCode`, `exposureControl`, `realDetailZoom`, `regionOfInterest`, `tracking`, `keepsScreenAwake`); it never picks the ultrawide. Tracking is published through `onTrack`; `CKeyBracketOverlay` is the optional built-in overlay (One Euro filter, latency projection, critically damped spring, drawn by a display link); `CKeyCameraPreview` converts points for custom overlays; `CKeyFrameProcessor` and `CKeyCameraControl` serve hosts that own their camera. **Not compiled here** (no Swift toolchain).
* **Android, web, .NET, CLI, macOS Renderer:** levels U and X and the grouped layouts. Android host checks, web tests, the native bridge test and the conformance vectors pass here; .NET, wasm and the Swift apps were not built here.
* New test suite `ckey_reader_hardening`. All 16 CTest suites pass in Release.

**Known costs and gaps**

* The reader is slower than build 85: about 1.5 times on frames that decode and more than 3 times on frames that do not (table above). A strong first-pass candidate skips the extra search passes, and non-symbol rectangles are dropped before refinement, but several reading strategies and both placements are still tried on a frame that fails. Region-of-interest scanning in the SDK reduces the per-frame cost on a phone; the latency gate has not been measured on a device.
* 71 of 6,000 cases that build 85 read are now missed, and the occlusion stratum is lower on its 29 recoverable cases. These were not investigated.
* A second ring observation per slot (half a slot along) and averaging across the ring stroke were tried and removed: on the 141 hard standard-layout frames neither changed how many came within error-correction capacity (36, against 34 to 38 for the variants). See `docs/research/reading-through-blur.md`.
* The standard layout's rings still fail first under blur. Their dots have a radius of 0.2 module and sit 0.3 module apart on the inner ring, so one unprinted slot between two printed ones is a gap of about 0.2 module. See `CONTEXT.md` and `docs/research/reading-through-blur.md`.

## 0.1.0-build.85 — 2026-11-07

* **Safe Actions** ([spec/CKey-Safe-Actions.md](spec/CKey-Safe-Actions.md)). A Safe Action is a code a reader may act on without asking first: `ckey-action:1:<base64url envelope>`, signed with Ed25519 by a site (the issuer, named by its domain) for one app and one action that app registered, with a single-use request ID and a short lifetime. Anything that does not verify runs nothing. Same-device actions run at once; cross-device actions (sign in a browser elsewhere, approve a payment) run only after a confirmation the SDK owns, which shows the issuer's account of who is asking (device, place, time) fetched from the issuer's own domain. The issuer's scope and the app's registration both count, and the stricter wins. This is the one exception to rule 1 of the [payload conventions](spec/CKey-Payload-Conventions.md) (now draft 0.2).
* **Core:** SHA-512 and Ed25519 (RFC 8032, canonical-S check) in `src/ed25519.inc`; `ckey_is_safe_action`, `ckey_safe_action_parse`, `ckey_safe_action_verify`, `ckey_safe_action_sign`, `ckey_ed25519_public_key`, struct `ckey_safe_action` and status `CKEY_EXPIRED` (7). Additive: the C ABI stays 4.
* **Conformance vectors:** `tests/vectors/safe-actions-v1.json`, 22 envelopes from an implementation independent of the core (`tools/safe_actions_vectors.mjs`, Node crypto), each with its required verdict. The core's `ckey_safe_actions` test, the web, Android and .NET tests check every one; CI checks the file is current.
* **CLI:** `ckey action keygen | public-key | sign | verify`.
* **SDKs:** a policy class in each SDK (`SafeActions` for the web, `CKeySafeActions` for Swift, Android and .NET) that pins issuer keys, registers actions, and returns not-a-safe-action / rejected / performed / needs-confirmation. Approval is internal to the SDK: only its own prompt (a `<dialog>`, SwiftUI `CKeySafeActionPrompt`, Android `CKeySafeActionPrompt` over AlertDialog, WPF `CKeySafeActionPrompt` or `CKeySafeActionConsolePrompt`) can run a cross-device action. Web bridge `ckw_safe_action`; Android JNI `nativeSafeAction`; .NET P/Invoke over `ckey_safe_action`.
* **Verified here:** core tests in release and sanitizer builds; web tests; the Android host check (JNI signatures, exports, Safe Action vectors through JNI, Kotlin API checks, 28 JUnit tests); 76 of 76 .NET tests against `libckeynative.so`. Not compiled here: the Swift sources (CI macOS) and `CKey.Wpf` (needs the Windows desktop SDK; CI Windows).

## 0.1.0-build.84 — 2026-11-07

* **Field tests.** `field-tests/suite.json` defines eleven real-world tests: smallest printed size, reading distance, viewing angle, lighting, screens, damage, styles, density, the print layout on small labels, time to read, and payload safety. Each states the claim it checks and why, the set-up and steps, the test codes (payload, version, level, layout, style and print size), what to record, and a pass / partial / fail rubric. `tools/field_tests.py` validates the suite and generates `field-tests/README.md`, one page per test in `field-tests/tests/`, and the GitHub issue form `.github/ISSUE_TEMPLATE/field-test-report.yml`; CI runs it with `--check` and encodes every test code with the CLI. openckey.org reads the same `suite.json` from the release it pins, prints the test sheets with the web SDK, and publishes every result, from the site or from GitHub issues, at openckey.org/tests.
* No change to the core, formats or packages.

## 0.1.0-build.83 — 2026-11-06

The release that goes to every registry, `v0.1.0-build.83`.

* **npm package renamed `openckey`.** The name `ckey` on npm belongs to an unrelated package published in 2018, so `v0.1.0-build.82` could not be published there. Install with `npm install openckey` and import from `'openckey'`; the API is unchanged. The release tarball is `openckey-<version>.tgz`.
* No change to the core, formats or other packages.

## 0.1.0-build.82 — 2026-11-06

Tagged `v0.1.0-build.82`; superseded by build.83 (the npm name `ckey` was taken).

* **Style validation without undefined behaviour.** The structural check read `policy`, `modules`, `rings` and `rays` as their enum types before range-checking them, so a caller passing an out-of-range value (the certification test passes 9) was undefined behaviour, and the sanitizer CI job stopped on it. The check now reads each field's raw integer and refuses unknown values as before.
* **Android host check:** `jni-host-test/run.sh` looked up optional jars in the Gradle distribution with `ls`, and under `set -euo pipefail` a missing jar ended the script with exit code 2 before the Kotlin step. Missing optional jars are now ignored.
* **Verified here:** the full suite under AddressSanitizer and UBSan with `-fno-sanitize-recover=all` (13 of 13), and the Android host check (JNI signatures, exports, 60 harness checks, Kotlin API checks, 17 JUnit tests).

## 0.1.0-build.81 — 2026-11-06

Tagged `v0.1.0-build.81`; superseded by build.82 (two CI checks stopped its release workflow).

* **.NET native library renamed to `ckeynative`** (`ckeynative.dll`, `libckeynative.so`, `libckeynative.dylib`). Windows file names ignore case, so the native `ckey.dll` and the managed `CKey.dll` were the same file in an application's output folder; one overwrote the other and P/Invoke found no `ckey_get_abi_version` (33 of 65 tests failed on Windows). Every Windows consumer of the package would have hit this. The CMake output name, `NativeLibraryName.Name`, the package's `runtimes/<rid>/native/` assets, the .NET Framework copy target and the CI checks all use the new name.
* **Verified here:** 65 of 65 .NET tests pass against `libckeynative.so`; a local pack places every native asset and both target frameworks where the CI layout check expects them.

## 0.1.0-build.80 — 2026-11-06


* **.NET SDK compiles again.** `CKeyPayload.cs` held a literal LINE SEPARATOR and PARAGRAPH SEPARATOR (U+2028, U+2029) inside a string. C# treats both as line breaks, so the string ended mid-line and the SDK had not compiled since build 67. They are written as `\u2028` and `\u2029`; no other source file holds either character.
* **.NET certification test:** it still expected every modelled trial to pass. Since build 72 the photo sweep includes sizes too small to read, so a certified style passes some trials, not necessarily all; the test now checks that.
* **Verified here:** `dotnet build` of `CKey` and `CKey.Wpf` (net8.0 and netstandard2.0) with no errors or warnings, and `dotnet test`: 65 of 65 pass against the Linux native library.

## 0.1.0-build.79 — 2026-11-06


* **macOS CI:** GitHub retired the `macos-14` runners ("The configuration 'macos-14-arm64-us-default' is not supported"), so every macOS job (the .NET natives for `osx-arm64` and `osx-x64`, the .NET tests, the Swift package and the release CLI build) was cancelled before it started. They run on `macos-15`.

## 0.1.0-build.78 — 2026-11-06


* **Windows .NET build:** the struct-layout check compared the probe's output (carriage returns stripped) with an expected file that the Windows runner had checked out with CRLF endings, so every line differed although every offset and size matched. The check now uses `diff --strip-trailing-cr`, and `.gitattributes` keeps LF endings for the expected layout and the test vectors on every platform.

## 0.1.0-build.77 — 2026-11-06

Fixes for the release build, which failed at the `v0.1.0-build.76` tag.

* **Windows (.NET native library):** MSVC has no `__int128`, which the telemetry core used for exact 128-bit multiply-divides. A `mul_div` helper keeps `__int128` where the compiler has it and otherwise uses a portable 64×64→128 multiply and long division (checked against `__int128` on 3.5 million random and edge-case inputs). The bound check on a histogram's sum no longer needs 128 bits.
* **Linux (.NET native library):** `libckey.so` exported C++ standard-library template instantiations under GCC 11. A linker version script (`sdk/dotnet/native/exports.map`) now exports `ckey_*` only (57 symbols); there is still no libstdc++ dependency.
* **Debug and sanitizer builds:** `field_score`, which calls the scan library's calibration scores, moved from the shared internals to `api_scan.inc`. Unoptimized codec and render builds no longer reference scan symbols, so `ckey_interchange_tests` links.
* **Docs:** the release table and changelogs record build.76 as tagged but not released.

## 0.1.0-build.76 — 2026-11-06

The first release, promoted to `Stable-Release` as `v0.1.0-build.76`.

* **README:** rewritten for people using CKey: a gallery of rendered codes, install lines and a short example for every package (Swift, npm, Maven, NuGet, Homebrew, CMake), what CKey can do, and an index of the documentation. The former README's technical walk-through moved, almost unchanged, to `docs/OVERVIEW.md`.
* **Gallery:** `docs/gallery/` holds eight codes rendered by the core (plain, print layout, custom colours, Concept, dotted rings, accents, light on dark and a dense v17). `tools/gallery/ckey_gallery.cpp` (target `ckey-gallery`) renders them, reads each one back with the real decoder, certifies it and writes the gallery page. All eight read back and certify.
* **Publishing:** `tools/release/publish.sh <tag>` publishes a release from the Release workflow's draft (npm, NuGet, Maven Central, Homebrew tap, then the GitHub Release), after checking `SHA256SUMS` and provenance. It rebuilds nothing. `docs/PUBLISHING.md` covers registry accounts and the release procedure.
* **Lock-down:** every GitHub Action is pinned to a commit SHA with its tag as a comment; `.github/CODEOWNERS` added; `docs/REPOSITORY-SETTINGS.md` lists the branch, tag and security settings to apply in GitHub.
* **Repository:** `CONTRIBUTING.md` and `SECURITY.md` added; `.gitignore` covers SDK build outputs. `tools/check_version.py` checks `CONTEXT.md` only where it exists, since release branches ship without it. `tools/bump_version.py` also updates the README and SDK READMEs, whose install lines name the version.

## 0.1.0-build.75 — 2026-11-05

* **Swift package resolves in Xcode:** the `CKeyScan`, `CKeyRender` and `CKeyCertify` targets now ship an `include/` directory. Git stores no empty directories, so a fresh clone lacked them and Xcode refused the package graph with "public headers ("include") directory path for 'CKeyScan' is invalid or not contained in the target".

## 0.1.0-build.74 — 2026-11-05

Swift SDK fixes, found while building the Open CKey Scanner iPhone app:

* **`CKeyPayload.classify`, `tel:` and `sms:`:** the number now ends at the first `;` or `?`, so URI parameters are never dialled as digits. Previously `tel:+15550100;ext=7` became `+155501007`.
* **`CKeyPayload.text(from:)` and `CKeyResult.text`:** only C0 controls other than tab, LF and CR, plus DEL and C1, make a payload binary, as in the web reader. Emoji ZWJ sequences, soft hyphens, bidi marks and BOMs are text.
* **vCards** must begin with a `BEGIN:VCARD` line and end with an `END:VCARD` line, as in the web reader.
* **`CKeyCameraScanner(observer:resolution:)`:** new `.uhd4K` (default) and `.hd1080` resolutions. `.hd1080` suits memory-limited camera extensions.
* **Camera errors:** reported as `.cameraAccessDenied` or `.cameraUnavailable` instead of `.invalidConfiguration`.
* XCTests added for each fix. They have not been compiled here; `sdk-swift.yml` runs them on macOS.

## 0.1.0-build.73 — 2026-11-05

* **The photo sweep, not fixed rules, decides certification.** Custom backgrounds were refused by the old 3:1 rule even when they scanned well; concept on slate `#555`, for example, has a 2.9:1 accent and is graded "good" by the sweep. Now:
  * **Hard refusals** remain only for near-invisible marks (below **1.5:1**), mixed polarity, and logo limits (Ring Key, plate, 50% budget).
  * **Low contrast below 3:1** and strong depth on dark-on-light styles are warnings. They appear in the style check ("style OK, but …; certify it to check that it scans") and in the certification message ("note: …").
  * **Strict renderers** draw these styles. Adaptive repairs only hard failures up front, and the advisory repairs only if the sweep fails.
  * **Measured:** concept on navy, forest and burgundy is excellent; on slate, teal and `#777` it is good; on tan and cream it is refused or marginal.
* Tests updated: `#e8e8e8` on white is the near-invisible refusal case, and `#bbbbbb` renders but fails the sweep.

## 0.1.0-build.72 — 2026-11-05

* **Certification now models real photographs.** The old check decoded the styled symbol from a perfectly known position at a comfortable 8 px per module, so almost every style passed. `ckey_certify_scalable_style` now runs a photo sweep:
  1. place the symbol in a cluttered frame, with rotation, keystone, glare, defocus and noise;
  2. run the full reader pipeline: find borders, refine, decode;
  3. walk a ladder of camera resolutions (9 down to 3 px per module) to find the smallest that still decodes;
  4. grade the style against plain black on white for the same symbol: **excellent** as small as plain, **good** one step larger, **marginal** two steps, otherwise **fails**.
  * Certification needs the safety rules plus a grade of good or better.
  * The message states both sizes, so the cost of a decoration is visible.
  * The plain reference is cached per symbol.
  * Typical cost is 0.1–0.7 s for v1–v11 and about 5 s for v31, natively.
  * The API and ABI are unchanged; `model.px_per_module` now sets the resolution the style must decode at.
  * The design-system spec and the header document the new semantics.
* **Profile 0.1 regression coverage:** the simulated-reader test now also scans print-layout symbols (v2, v11, v20) through the exact pipeline the iPhone reader uses. All 48 new scenes decode, and under heavy blur Profile 0.1 decodes far better than the standard layout. The reported CKey Reader failure is consistent with an app build from before build 58, when Profile 0.1 did not exist.
* **CKey Reader:** the subtitle now lists Profiles 0, 0.1 and 1–3.

## 0.1.0-build.71 — 2026-11-05

* **Removed `services/openckey`,** the Node prototype of the openckey.org service. Its telemetry ingestion, renderer page and abuse controls now live in the proprietary openckey.org site. That site follows the same normative `docs/adoption-and-telemetry.md`, and it pins this repository's commits to build the web SDK.

## 0.1.0-build.70 — 2026-11-05

* **openckey.org is now a separate, proprietary site.** It provides:
  * the browser renderer;
  * package downloads;
  * `/text/{id}` and `/sq/{id}` destinations;
  * telemetry ingestion.
  * README, implementation status and context now say so. The CI job for the prototype `services/openckey` is removed. The prototype itself is retired in the next build.
* **Release workflow:** reuses the SDK workflows, which now accept `workflow_call`, to build and test each package, and attaches it to the draft release:
  * the npm tarball;
  * NuGet packages;
  * a Maven repository zip for Android;
  * a Swift source zip.
  * These join the CLI archives, the source archive and the Homebrew formula, so openckey.org can host every package for download.
* The `sdk-web` workflow also uploads the `npm pack` tarball.

## 0.1.0-build.69 — 2026-11-05

* **Android SDK, Maven `org.openckey:ckey` (`sdk/android/`, spec §3 and §7):** AGP 8 and Kotlin. NDK CMake compiles the repository's own `src/` files with a JNI bridge that exports only the `Java_*` functions.
  * **Modules:**
    * `ckey`, the core AAR, with no dependencies beyond the Kotlin standard library;
    * `ckey-compose`, which provides `CKeyImage`;
    * `ckey-camerax`, which provides `CKeyAnalyzer`.
  * **Scan-only and render-only builds:** set the Gradle properties `ckey.native.scan`, `ckey.native.render` and `ckey.native.telemetry`.
  * **API:**
    * `CKeySymbol.encode`;
    * `toBitmap`, which converts straight alpha to Android's premultiplied form;
    * `toPng`, `toSvg`, `toPdf` and `certify`;
    * `CKeyStyle`, with presets and a policy;
    * `CKeyDecoder`, for gray buffers, a `ByteBuffer`, `android.media.Image` or a `Bitmap`;
    * `CKeyScanner`, which records telemetry sessions;
    * `CKeyPayload.classify`.
  * **Telemetry:** `CKey.telemetry` takes 0, 1 or 2; any other value throws `IllegalArgumentException`. One background thread sends batches over HTTPS only, and the SDK tries a send when the app moves to the background.
  * **Publishing:** maven-publish to a local directory, with no credentials in the build.
  * **Verified here** with `jni-host-test/run.sh`:
    * the Kotlin native declarations and the C++ bridge are checked against each other through a generated JNI header;
    * a JVM harness runs 60 checks under `-Xcheck:jni`, including round trips, premultiplication, exports, refusals, certification and the full telemetry cycle;
    * the real Kotlin sources compile with Kotlin 2.0.21 against stubs;
    * 22 Java-side API checks pass, and 17 JUnit tests pass.
  * **Not verified here:** the Gradle/AGP/NDK build, the Compose module and the instrumented tests run only in `sdk-android.yml`.
* `tools/bump_version.py` now also covers the SDK READMEs.

## 0.1.0-build.68 — 2026-11-05

* **`docs/IMPLEMENTATION-STATUS.md`:** rewritten against builds 61–67, item by item against the specification. It ends with a list of what only the maintainer can do: deploy openckey.org, publish to the registries, set up branch protection, make real prints and camera captures, and pin action SHAs.
* **`docs/platform-integration.md`** (new) is for camera-platform teams. It covers:
  * what to link (only `ckey_codec` and `ckey_scan`);
  * the decoding pipeline for each frame;
  * threading and memory;
  * the security review surface;
  * conformance;
  * a proposed device benchmark plan, with a p90 first-decode target to be confirmed by measurement.
* **README:** new sections on libraries, SDKs and services, and on rendering, certification and verification. The documentation index is extended.
* **CONTEXT:** records the decisions from builds 61–68.
* No code changes.

## 0.1.0-build.67 — 2026-11-05

* **Swift package `CKey` (`Package.swift` at the repo root, `sdk/swift/`, spec §3 and §7):** apps add the repository URL in Xcode.
  * **C++ targets:** `CKeyCodec`, `CKeyScan`, `CKeyRender`, `CKeyCertify` and `CKeyTelemetry`. Each is a one-line shim that compiles the repository's own `src/` file, so nothing is copied.
  * **Products:**
    * `CKey`, the full SDK;
    * `CKeyScanner`, scan only: module `CKeyScanKit`, with no renderer and no telemetry.
  * **Rendering API:**
    * `CKeySymbol.encode`;
    * `cgImage`, `pngData`, `svg` and `pdfData`;
    * `certify`;
    * `CKeyStyle`, with presets and a policy.
  * **Scanning API:**
    * `CKeyView`, a SwiftUI view that draws native pixels and is transparent by default, plus UIKit and AppKit views;
    * `CKeyDecoder` for `CGImage` and `CVPixelBuffer`;
    * `CKeyScanner`, an AVFoundation camera helper that drives telemetry sessions.
  * **Payloads:** `CKeyPayload.classify`.
  * **Telemetry:** `CKey.telemetry` takes 0, 1 or 2, and `try CKey.setTelemetry(rawValue:)` rejects anything else. Batches are sent off the scan queue through an ephemeral `URLSession`, and the SDK tries a send when the app moves to the background.
  * **Privacy:** `PrivacyInfo.xcprivacy` declares analytics that are not linked to the user and not used for tracking.
  * **Verified here:** the C++ shims and module maps compile with gcc and clang, and their results were checked.
  * **Not verified here:** the Swift sources have not been compiled. `sdk-swift.yml` runs `swift build`, `swift test` and an iOS Simulator build on macOS.
* **.NET SDK, NuGet `CKey` (`sdk/dotnet/`, namespace `OpenCKey`):**
  * **Native library:** one shared `ckey` library built by CMake, exporting exactly the 55 C ABI functions. On Linux it statically links the C++ runtime. The package carries it for `win-x64`, `linux-x64`, `osx-arm64` and `osx-x64`.
  * **Interop:** `LibraryImport` on net8.0 and `DllImport` on netstandard2.0. Struct layouts are checked against a C `offsetof` probe in tests and in CI.
  * **API:**
    * `CKeySymbol.Encode`;
    * `RenderRgba`, `ToPng`, `ToSvg` and `ToPdf`;
    * `Certify`;
    * `CKeyDecoder.Decode`, using the same pipeline as the iOS reader;
    * `CKeyPayload.Classify`.
  * **Telemetry:** `CKey.Telemetry` takes 0, 1 or 2, and anything else throws `ArgumentOutOfRangeException`. Batches are sent asynchronously with `HttpClient`. `CKeyScanSession` records scan sessions.
  * **Optional `CKey.Wpf`** adapter, which returns `WriteableBitmap`.
  * **Verified here:** the native build, its exported symbols and the struct layouts.
  * **Not verified here:** the C# has not been compiled. `sdk-dotnet.yml` builds the native libraries on Linux, Windows and macOS, then runs `dotnet test` and `dotnet pack`.
* `.gitignore`: SwiftPM's `.build/` and `.swiftpm/`.

## 0.1.0-build.66 — 2026-11-05

* **CMake package (spec §7):** `cmake --install` installs the libraries, headers and the `ckey` CLI, plus `CKeyConfig.cmake`.
  * Consumers call `find_package(CKey)` and link `CKey::core`, or individual targets: `CKey::scan`, `CKey::render`, `CKey::codec`, `CKey::certify`, `CKey::telemetry`.
  * `examples/c/` has an encode → raster → PNG → scan example and a scan-only example. CI installs the package and builds and runs the example.
* **Homebrew (proposed in spec §7):** `packaging/homebrew/ckey.rb.in` and `tools/release/make_homebrew_formula.py`, which fills in the tag and the source archive's SHA-256.
* **Release workflow (`release.yml`, spec §7–8):**
  * **Trigger:** runs only for `v*` tags. The tag must equal `v` + `VERSION` and sit on `Stable-Release`. The full test suite runs first.
  * **Artifacts:**
    * CLI and library archives for Linux x64, macOS arm64 and Windows x64;
    * the source archive;
    * the Homebrew formula;
    * `support-matrix.json`;
    * `SHA256SUMS`.
  * **Provenance:** Sigstore build provenance through `actions/attest-build-provenance`.
  * **Result:** a draft GitHub Release titled `CKey <version>`, for the maintainer to publish.
* **Deprecate workflow (`deprecate-release.yml`, spec §8.2):** a manual run retitles a release `Deprecated - {release name}` and adds a notice naming the replacement and the reason. The tag and its source stay.
* **Community workflow (`community.yml`, spec §8):**
  * runs ASan/UBSan tests on pull requests and pushes to `community`;
  * uses `pull_request` with a read-only token and no persisted credentials, so fork code never sees secrets;
  * rejects changes to `VERSION` or to the release workflow, and warns when `CHANGELOG-COMMUNITY.md` has no entry.
* **CI:** new jobs for the openckey.org service tests, and for synthetic benchmarks with QR Code and Data Matrix through zxing-cpp (uploaded as an artifact).
* **Web SDK, npm `ckey` (`sdk/web/`):** an Emscripten build of codec, scan, render, telemetry and certify, plus a small C bridge and an ES module API. It matches the openckey.org renderer page:
  * `encode`, `renderToCanvas`, `renderPixels`, `renderPNG`, `renderSVG` and `renderPDF`;
  * `decodeImageData`, `createScanner`, `classifyPayload` and `certify`;
  * `telemetry`, valued 0, 1 or 2, raising `RangeError` otherwise. The telemetry sender uses `fetch` with keepalive, sends no credentials or referrer, and never throws;
  * TypeScript types.
  * **Verified here:** 26 JS tests pass, and native bridge tests pass under ASan/UBSan.
  * **Not verified here:** the wasm build and its tests run only in `sdk-web.yml`, because emsdk is not available in this environment.
* **`tools/bump_version.py`** bumps every versioned file, including SDK manifests.

## 0.1.0-build.65 — 2026-11-05

* **`.ckey` interchange v1 (spec §2.5; `spec/CKey-Interchange-v1.md`):** `ckey_write_interchange` and `ckey_read_interchange` store a Profile 0 or 0.1 codeword, an optional style and an optional logo reference.
  * **Layout:** a 12-byte header, type-length-value records, and a closing CRC-32C. Bit 15 marks a record as optional.
  * **Strict, bounded reader:** at most 64 KiB, all reserved bytes must be zero, and an unknown required record is rejected. The codeword must decode and must be the canonical encoding of its payload.
  * The file is optional: rendering never needs it. The payload is stored in clear.
  * **Library:** the reader is in `ckey_codec` and needs no renderer.
  * **Tests:** new test `ckey_interchange` covers round trips, the rejection cases, and 20,000 mutated files, clean under AddressSanitizer and UBSan.
* **Conformance suite (spec §6; acceptance criterion 1):** `tools/conformance/ckey_conformance.cpp` and `tests/vectors/conformance-v1.json`.
  * **19 vectors:** P0 and P0.1 across versions and levels, an empty payload, a vCard, and P1, P2 and P3 legacy encodings.
  * **Expected outputs:** the codeword, an FNV-1a 64 hash of the canonical geometry with coordinates rounded to 10⁻⁶, a `.ckey` file hash, and a check that a raster decodes. Pixels are checked by behaviour, not by hash.
  * **Verified identical** under gcc -O0 and -O2, and under clang -O2 with `-ffast-math`.
  * **New CTest `ckey_conformance_vectors`.** SDKs load the same JSON; see `tools/conformance/README.md`.
* **Support matrix (spec §8.2):** machine-readable `docs/support-matrix.json` with the current build, ABI, tag form, formats and their status, interchange and telemetry schema versions, packages and deprecations. `check_version.py` checks it.
* **Benchmark harness (spec §5, §6):** `ckey-bench` (`tools/optical-benchmarks/`).
  * **Pipeline:** generate, degrade (clutter, rotation, keystone, glare, defocus, noise), then decode with the full reader pipeline.
  * **Measures:**
    * recovery;
    * corrected bytes;
    * latency p50, p90 and p99;
    * the smallest side for 90% success, and bytes per 1,000 px² at equal area;
    * false positives.
  * **QR Code and Data Matrix** comparison through zxing-cpp with `-DCKEY_BENCH_ZXING=ON`.
  * A quick, CKey-only synthetic run is committed in `docs/benchmarks/`. It found 0 false positives in 60 clutter frames. Results are explicitly synthetic.

## 0.1.0-build.64 — 2026-11-05

* **ABI 4:** `ckey_style` gains `policy`. Code that fills styles with `ckey_style_default` or `ckey_style_preset_fill` needs no other change.
* **Style policies (spec §4):** `CKEY_POLICY_STRICT` (the default), `CKEY_POLICY_ADAPTIVE` (renderers repair the style by fixed rules, then check it) and `CKEY_POLICY_OVERRIDE` (renderers draw the style as given; checks report "NOT scan-safe"). Structurally invalid values are refused under every policy.
* **Optical certification (spec §4; acceptance criteria 5 and 6):** `ckey_certify_scalable_style` certifies through a modelled scan, not a contrast ratio:
  1. render with the native raster;
  2. composite over each backdrop;
  3. apply glare, defocus and noise;
  4. decode with the real decoder, on 3 seeded trials per backdrop.
  * It also reports the minimum contrast, the mark/gap luma separation and the logo load.
  * Adaptive certification returns the repaired style and a `CKEY_ADJUST_*` bit for each change.
  * **Transparent output** needs a `ckey_backdrop` (colours and/or the host's pixels). Without one the result is "background unknown" and is never certified.
* **New library `ckey_certify`** (needs scan and render). `ckey_core` now includes it.
* **New test `ckey_style_certification`:**
  * The default style certifies at v1, v11 and v31 (v31 in about 1.3 s).
  * A coarse, blurred model fails v31.
  * Strict refuses low contrast in the check, the renderer and certification.
  * Override renders but never certifies.
  * Adaptive repairs ink, accents and depth.
  * Transparent output: refused without a backdrop, certified on white, refused for black on #333333, which adaptive fixes by flipping the ink.
  * A half-black host image is refused.
  * The concept preset certifies.
* `spec/CKey-Design-System.md`: new section on policies and certification.

## 0.1.0-build.63 — 2026-11-05

* **Separate libraries (spec §2.1, §6, §9.2; acceptance criterion 8):** the core is now `ckey_codec`, `ckey_scan` (needs only the codec), `ckey_render` and the optional `ckey_telemetry`. `ckey_core` remains as the union of the first three.
  * A scanner app links no renderer and no telemetry. `ckey_optical_image_decode` now links `ckey_scan` alone, so CI enforces this.
  * The source is split into shared internals plus one file of public functions per library. `src/ckey.cpp` stays as the amalgamation for the Xcode projects. See `src/README.md`.
  * `-DCKEY_BUILD_TELEMETRY=OFF` leaves telemetry out entirely.
* **Telemetry core (spec §9; `docs/adoption-and-telemetry.md`):** `include/ckey/telemetry.h`.
  * **Modes:** 0, 1 (the default) and 2. Any other value returns `CKEY_INVALID_ARGUMENT` and leaves the mode unchanged. Mode switches follow the documented table: switching to 0 purges everything; 1 → 2 drops the unsent delta and ignores any batch in flight.
  * **Sessions:** a session starts when scanning starts, succeeds at the first decode, and fails when scanning ends or after a 30 s timeout. Frames are counted separately.
  * **Data:** counts by profile, and histograms of time to decode and per-frame processing time.
  * **Batches:**
    * the exact schema-1 JSON, with per-batch clamping to the schema's limits;
    * an hourly send cadence;
    * result handling that drops a batch on 2xx, 400 or 413, and keeps it on 429, 5xx or a network error.
  * **State:** optional export and import, strictly parsed.
  * **No networking:** the core never sends; SDK wrappers post to `CKEY_TELEMETRY_ENDPOINT` (`https://openckey.org/api/telemetry/v1/batch`). It uses no clock, heap or exceptions, and is thread-safe.
  * **Tests:** new test `ckey_telemetry`, clean under AddressSanitizer, UBSan and ThreadSanitizer.
* **openckey.org service (`services/openckey/`):** a Node 22 service with no dependencies. Run `npm test`; 75 tests pass.
  * **Telemetry ingestion:**
    * strict parsing, rejecting unknown fields, non-integer literals, duplicate keys and over-size bodies;
    * rate limits keyed by a daily-salted HMAC of the client IP, kept in memory only;
    * daily aggregates only, kept for 400 days;
    * a summary labelled "not total usage".
  * **Short links (`/r/:id`):** created by an admin. The interstitial shows the destination host first; `?go=1` redirects directly. Counts are opt-in, per link and per day.
  * **Hosted text pages (`/t/:id`):** escaped HTML under a strict CSP, plus `.txt`.
  * **Web renderer page (`/`):** uses the web SDK when it is built.
  * No IPs, bodies or URLs are logged.

## 0.1.0-build.62 — 2026-11-05

* **Canonical geometry (spec §2.1):** `ckey_scalable_geometry` returns a Profile 0 / 0.1 symbol as a list of primitives in canonical units, in painter's order. The primitives are rect, rect outline, circle, diamond, line, arc and logo, each tagged with a layer and a paint. Every native backend draws this list.
* **Direct raster backend (spec §2.2, §2.5, acceptance criteria 2 and 4):** `ckey_render_scalable_raster` turns the geometry into pixels with no SVG, file system, network or graphics library.
  * **Output:** RGBA8 (straight alpha) or GRAY8.
  * **Three raster policies:**
    * `COVERAGE`: supersampled antialiasing.
    * `BINARY`: a pixel is ink when at least 50% of its samples are covered.
    * `HYBRID`: coverage, with marks thinner than a minimum feature, 1 px by default, widened to it.
  * **Transparency:** in transparent mode the background is not painted and unpainted pixels have alpha 0.
  * **Materials:** natively rendered approximations of sheen, vignette, grain, drop shadow and bevel. The logo is supplied by the caller as RGBA and drawn into its clip circle.
* **PNG export:** `ckey_encode_png`, with DEFLATE implemented in the core (fixed Huffman codes, LZ77), RGBA8, GRAY8 or 1-bit, and pHYs. There is no zlib dependency.
* **PDF vector export (spec §2.3–2.4):** `ckey_render_scalable_pdf` writes a one-page PDF 1.4 with exact paths, arcs as cubic Béziers, flat ink, accents and background, and an optional transparent mode.
* **Print path:** `ckey_render_scalable_print` now rasterizes the geometry directly, with the binary policy and a 3×3 sample grid, instead of generating SVG and parsing it back. The core no longer includes the SVG parser, which stays a test and reference tool.
* **CLI:**
  * `encode-scalable` gains `--raster out.png --size-px N --policy coverage|binary|hybrid --transparent yes|no` and `--pdf out.pdf` (`--size-in` sets the width).
  * `--png` and `print-sheet` use the core print raster and PNG encoder.
* **New test `ckey_native_render`:**
  * The direct raster matches the rasterized SVG export to within 0.01% of pixels, from SVG's 4-decimal rounding, at v2, v11, v20 and v31 across shapes, ring styles and caps.
  * All three policies decode at 7 px/module for v1, v9 and v31 in both layouts, and the output is deterministic.
  * HYBRID adds ink to thin features.
  * Transparent margins have alpha 0.
  * The concept preset with depth, grain and a logo decodes.
  * Geometry API layers are checked.
  * PNG round-trips through an independent inflater.
  * PDF cross-reference offsets are valid.
  * Print output is pure ink or paper.
* No format changes, and conformance hashes are unchanged.
* The partial `services/openckey/` files committed by accident in build 61 are finished in the next build.

## 0.1.0-build.61 — 2026-11-05

* **Version scheme:** from this build, versions are `0.1.0-build.N` and release tags are `v0.1.0-build.N`, as the maintainer chose. Builds 1–60 keep their `0.1.0-dev.N` names in this log. See the [map](docs/RELEASE-AND-VERSIONING-MAP.md#tag-and-version-form).
* **Default look (specification §4):** with no style, every profile now renders black ink on white. This covers the P0 default style and the P1–P3 SVG renderers.
* **Presets:** the previous cream-on-black concept look is a named preset, `ckey_style_preset_fill(style, CKEY_PRESET_CONCEPT)`. The CLI's `--preset concept` uses it.
* **Mac renderer:** defaults to black on white. Its colour swap for P1–P3 now goes through placeholders, so a black background cannot be repainted as ink.
* No format changes. Site hashes are unchanged; only SVG colours changed.
* Added `docs/adoption-and-telemetry.md`, the normative telemetry definition:
  * modes 0/1/2 and what switching between them does;
  * what counts as a scan session;
  * the batch schema, version 1;
  * the openckey.org service's obligations.

## 0.1.0-dev.60 — 2026-11-05

* Cleaned up `CHANGELOG.md`:
  * Removed the duplicate dev.11 and dev.42 entries.
  * Restored newest-first order.
  * Recorded what happened to dev.27–30: dev.28's entry had been folded into dev.31, and the other three were never committed builds.
  * Added a header that explains the log.
* Added `CHANGELOG-STABLE.md` for tagged `Stable-Release` releases (none yet) and `CHANGELOG-COMMUNITY.md` for `community` merges. Each has an entry template and rules.
* Added `docs/RELEASE-AND-VERSIONING-MAP.md`. It covers:
  * how the software, format, profile and ABI numbers relate;
  * the current support matrix, including legacy formats that still decode;
  * the history of all 28 formats, with byte, profile, introducing build and status;
  * the ABI history;
  * a build-to-commit table;
  * the procedures for cutting and deprecating a release.
* README: added sections on the repository, its branches, releases, versions, changelogs and documentation. The current-format list now includes Profile 0 and 0.1.
* `tools/check_version.py` now also requires the map and both branch changelogs to exist and be linked from the README.
* No code or format changes.

## 0.1.0-dev.59 — 2026-11-05

* Work moves to the maintainer-only `scratch` branch. It was created from `main` at `47f80a9`; `community` and `Stable-Release` are being set up by the maintainer.
* Added the consolidated **Rendering, SDK, Distribution, Adoption & Telemetry Specification** (`docs/SPECIFICATION-rendering-sdk-distribution-telemetry.md`), as written by the maintainer.
* Added `docs/IMPLEMENTATION-STATUS.md`, which maps every section of that specification to the current code. It also flags five conflicts:
  * the print path rasterizes through SVG;
  * there is no transparent output;
  * the default palette is cream on black, not black on white;
  * the style check certifies from WCAG contrast alone;
  * the scanner and renderer ship as one library.
* Added `docs/BRANCHES-AND-RELEASES.md` with the branch model and release rules. Releases are immutable tags on `Stable-Release`, and deprecated releases are retitled `Deprecated - {release name}`.
* No code changes.

## 0.1.0-dev.58 — 2026-11-04

* **Profile 0.1, a print-robust layout** (Format 0.28.0, byte `1C`).
  * **Every mark and gap is at least about one module:** equal-arc ring slots (dots at least one module apart on every ring), 0.3-module ring dots, 0.35-module rays at least one module apart, and a compact 8-slot Ring Key.
  * **Shared with P0:** the same skeleton, 31-version table, framing and correction levels.
  * **Identification:** the decoder tells the layouts apart by their keys.
  * **Simulated result:** the same payload decodes at about half Profile 0's printed width. For example, on an old inkjet with plain paper: 30 B at 0.24 in (P0: 0.42), 120 B at 0.32 in (0.68), 1,200 B at 0.91 in (1.47).
* **ABI 3:** `ckey_scalable_symbol` gains `layout`, and the P0 encode, decode, capacity and logo-radius calls take a layout. `ckey_decode_scalable_gray8` also reports it.
* **Print output.**
  * `ckey_render_scalable_print` renders black ink on paper on the printer's dot grid, with no grey for halftoning.
  * The CLI gains `encode-scalable --png --size-in --dpi --layout` and `print-sheet`, a self-identifying calibration ladder written as a 1-bit PNG that records its dpi.
  * The CKey Renderer gains a Layout picker and **Export Print PNG…**.
* **Print simulator** (`tests/print_sim.h`): binary device dots, ink spread, paper blur, then a camera at any resolution.
  * **Findings for P0:** dark-on-paper prints about 35% smaller than the cream-on-black palette on plain inkjet. Ink-spread compensation does nothing at 600 dpi, because the spread is about one dot. Ring dots fail first.
  * **Regression test:** a 120-byte payload at 0.42 in on the inkjet model must fail as P0 and decode as P0.1.
* The reader shows `Profile 0.1 · print · v<N>`. Guides added: `spec/CKey-Printing.md` and `spec/CKey-Format-v0.28.0.md`. The reference rasterizer moved to `src/svg_raster.h`.

## 0.1.0-dev.57 — 2026-11-04

* **Profile 0 design system** (`spec/CKey-Design-System.md`). Styling changes how a symbol is painted, never where a reader samples.
  * **Fixed:** the frame, finders and keys keep their geometry.
  * **Mark shapes:** payload modules can be rounded, square, circle or diamond; ring marks joined or dotted; rays round or flat.
  * **Paint:** colour is free within checks: metallic sheen, radial vignette, up to four accent colours, bevel, depth and grain.
  * **Logo:** sits in the empty centre, or larger on a plate within an error-correction budget.
  * **API:** `ckey_style`, `ckey_check_scalable_style` (contrast ≥ 3:1, one polarity, effect limits, logo budget ≤ 50% of the worst block), `ckey_render_scalable_styled_svg`, `ckey_scalable_logo_max_radius`, `ckey_scalable_logo_load`. The plain P0 renderer is now the default style.
* **P0 decoding no longer depends on the finders' split or the empty centre.** Its skeleton is the frame stroke plus all eight finder arm ends, against the moat and quiet margin, so it is symmetric by design. Orientation comes from the Grid, Ring and Ray Keys. A logo in the centre therefore costs no calibration evidence. No wire change.
* **Tests.** All 16 mark-shape combinations decode at v2 and v20. The concept palette passes the checker. Low-contrast, mixed-polarity and heavy-shadow-on-light styles are refused. At every level, the largest logo the checker allows still decodes. The test rasterizer gained polygons, `<defs>`, background detection and painter's-order logo plates.
* **Real-browser check.** The concept look rendered in Chromium (champagne sheen, navy vignette, sage/grey accents, bevel, depth, grain, PNG logo) decodes exactly at v6, v11 and v20, sharp and blurred.
* **CKey Renderer:** a Profile 0 **Design** panel (shapes, sheen, vignette, accents, bevel, depth, grain, logo picker, size, plate, Concept preset), with the style check shown live.
* **CLI:** `encode-scalable` gains `--preset plain|concept`, `--modules`, `--rings`, `--rays`, `--logo <file>` and `--logo-radius`.
* **Known limit:** Profile 0 scatters each byte's bits across the symbol. That keeps stripes and smudges cheap but makes covered area expensive, so logos currently fill the empty centre (about 22% of the width) with little room beyond. Byte-grouped placement would be a future wire-format change.

## 0.1.0-dev.56 — 2026-11-04

* **Profile 0, the scalable family (Format 0.27.0, byte `1B`, profile `00`).** Added alongside Profiles 1–3, which are unchanged.
  * **One skeleton at 31 sizes.** Every version shares Profile 2's frame, finders, rings and dial in the 29-unit field. Only the module pitch changes, from 29 to 232 modules across (normative integer tables), about 7% finer per step.
  * **No appearance jumps.** Capacity at level M rises smoothly from 27 to 4,619 bytes, with no step above 1.6×. A 33-character URL is v2, which looks like a locator, not Profile 2.
  * **No filler waste.** Every site carries a codeword bit. Four correction levels are available: L/M/Q/H.
  * **The keys identify the version.** The Grid, Ring and Ray Keys are drawn at each version's own module and slot pitch, so the decoder recognizes the version from them. The frame's version byte, RS and CRC confirm.
* **New core API:** `ckey_encode_scalable`, `ckey_decode_scalable`, `ckey_decode_scalable_gray8`, `ckey_calibration_score_scalable_gray8`, `ckey_render_scalable_svg`, `ckey_scalable_capacity`, `ckey_scalable_modules_across`, and `CKEY_FIELD_SCALABLE` for `ckey_refine_quad_gray8`.
* **Localization improvements (all profiles):**
  * Border candidates are ranked by how well the shared skeleton fits, so a dense symbol's interior components can't crowd out its border.
  * Per-corner refinement iterates down to sub-pixel steps.
  * P0 aligns on the frame stroke's two edges with coarse-to-fine sample spacing. At v31 the corners resolve to 0.06–0.22 modules, from 2–5.5 px before.
* **Tests and tools.**
  * New `ckey_scalable_profile0` suite.
  * The simulated reader covers P0 v1/v2/v11/v20/v31 and now decodes 144/144 scenes.
  * New CLI command: `ckey encode-scalable [--version 1-31] [--level L|M|Q|H]`.
* **Apps.**
  * The iOS reader tries P0 first and shows `Profile 0 · v<N>`.
  * The CKey Renderer defaults to P0, with version and correction pickers, and its read check reports `px/module`.
  * Both use 64 KB output buffers, because P0 payloads exceed P3's 2,048 bytes.

## 0.1.0-dev.55 — 2026-11-04

* **Payload conventions.** New informative document, `spec/CKey-Payload-Conventions.md`. It defines how readers interpret text payloads:
  * Standard URI forms: `https:`/`http:`, `mailto:`, `tel:` (E.164), `sms:`, `geo:` (RFC 5870), and vCard.
  * One CKey convention: `address:` followed by a free-text street address, handed to the platform map app.
  * Safety rules: never act automatically, allowlist only, keep the payload visible, and validate before offering an action.
  * No wire format changes.
* **iPhone reader actions.**
  * The reader offers one tappable action per recognized payload: Open, Email, Call, Message, Show in Maps (`geo:` and `address:`, through `maps.apple.com`, which iOS routes to Maps), or Add to Contacts (vCard, through the share sheet).
  * `javascript:`, `data:`, `file:` and unknown schemes are shown as text only.
  * Text detection now accepts CR, which vCard line endings use.

## 0.1.0-dev.54 — 2026-11-04

* **The iPhone reader shows decoded payloads as text.** A payload that is valid UTF-8 without control characters (other than tab and newline) is displayed as text. An `http`/`https` URL gets an explicit "Open host" button and never opens automatically. The hex stays underneath for checking, and binary payloads still show hex.
* **First successful live scan:** the dev.53 reader decoded `https://citeory.com/tour` on the maintainer's iPhone.

## 0.1.0-dev.53 — 2026-11-04

* **The iPhone reader started almost every decode from the wrong orientation, so P1 never scanned and P2 rarely did.** Its border detector oriented each candidate from the border's second moments (principal axes). A square outline's second moments are the same in every direction, so that fit cannot orient a square, and the angle came from pixel noise.
  * On rendered P1/P2 symbols, an upright symbol was fitted at −45°, a diamond around the square. Rotations from 0° to 30° gave orientation errors of 11–45°.
  * The refinement that followed could only scale, shift and nudge corners. It could not recover.
* **Full-frame localization moved into the shared C++ core**, so it is tested here and identical on every platform.
  * `ckey_find_borders_gray8` orients each border from its convex hull's minimum-area rectangle, which is exact for a square. It then line-fits each side's straight middle for a perspective-correct quadrilateral.
  * It requires the whole border in view, instead of capping the border at 90% of the frame. That cap rejected large rotated P3 symbols.
  * `ckey_refine_quad_gray8` starts from the known border-to-field scale (29/28.65). Its descent covers scale, **rotation** and offset, then per-corner moves.
  * The iPhone reader now calls both functions. Its Swift border finder, threshold, and principal-axis fit are deleted.
* **New simulated-reader test** (`ckey_simulated_reader`). Rendered symbols are composited into full 1080×1920 (P1/P2) and 2160×3840 (P3) camera frames with background clutter, rotation 0–33°, 8% keystone, 3×3 blur, noise, and dark-on-light polarity. The test then runs the reader's exact pipeline.
  * Result: 80/80 scenes decode, with 16 skipped because the rotated symbol cannot fit in the frame.
  * Restoring the principal-axis orientation drops this to 51/80.
* Measured cost per frame on the CI-class CPU: about 28 ms (1080p) or 42 ms (4K) to find borders, plus about 11 ms per candidate to refine all three profiles.

## 0.1.0-dev.52 — 2026-11-04

* Ignore `macos/build/` and `macos/DerivedData/`. A command-line `xcodebuild` of the renderer writes there; only `/build/` and `/ios/build/` were ignored.
* Recorded the first verified CKey Renderer build: `xcodebuild` (Xcode 27A9269, macOS 27.0 SDK, arm64 + x86_64) succeeded on the maintainer's Mac with the dev.51 Swift changes.

## 0.1.0-dev.51 — 2026-11-04

* **The iPhone reader could not decode Profile 3 at all.** It captured 1920×1080, but P3 needs about 11 px per symbol unit, about 1,280 px across its 116-unit field, which is more than the frame's height. Its border detector also capped candidates at 78% of the short frame side.
  * The reader now prefers 4K capture, falling back to 1080p.
  * The border search scales its sampling step with frame size, so it keeps about 960 × 540 points and the same angular resolution at constant cost.
  * The size limits scale with that step, and the upper bound is now 90% of the short side.
  * Not yet compiled or run on a device in this environment.
* **The CKey Renderer's preview read check now reports the raster density it decoded at.** It flags results below each profile's decode floor, so a P3 `FAIL` caused by a small window or the Resolution slider is distinguishable from a symbol fault.
* The app READMEs now describe how to confirm a rebuild picked up the current core, and what correct P2/P3 output looks like.

## 0.1.0-dev.50 — 2026-11-04

An independent second review of dev.49 was run under rule 7 and every verified finding is resolved. Formats 0.25 and 0.26 are unchanged on the wire; their specifications gain clarifications only.

* **P3 isolated ring dots under blur.** Each 3×3 blur turned every isolated P3 ring dot into a 0. The reviewer measured 608/608 failing at 12 px/unit, and P3 failed with two blurs at every density. Ring sites are now re-thresholded by neighbour context, with fill sites' known values as the anchor. P3 now decodes 3/3 at 10–16 px/unit through two blurs. P2 now survives one blur at 10 px/unit and two blurs at 12.
* **Threshold bound.** The two-means cut bound widened from [0.2, 0.8] to [0.05, 0.95]. P3 width coverage separates near 0.85.
* **P2 calibration reference.** One "empty" reference, at (5.626, 5.626), was a payload site under the new safe area. It now samples the finder's split gap at 225°.
* **Faster dense decoders.** They gate each orientation on fixed-key agreement (≥ 80%) before sampling every site. On a 1080p noise frame, P3 decode fell from 18 ms to 0.42 ms per call and P2 from 0.34 ms to 0.05 ms. Calibration scores are value-identical.
* **Specification clarifications.** v0.25/v0.26 now give full render geometry, the explicit SplitMix32 definition and whitening bit order, the P3 Gray component order, the gap measurement basis, the reason for the ring-33 Ring Key clear field, and an integer-only conformance vector.
* **Tests.**
  * The layout tests now check known-answer vectors.
  * They measure the Cartesian-to-radial-alphabet ink gap; it is 0.58 in both profiles.
  * They enforce the documented boundary slack.
  * The round-trip tests add blur across densities and noise rejection.
  * The SVG payload layer is split into `ckey-data-cartesian` and `ckey-data-polar`.
  * A dead branch was removed.

## 0.1.0-dev.49 — 2026-11-04

* **Profile 2/3 image decoding fixed.** At HEAD, neither profile could decode its own clean renders at any resolution. The sampler's fixed ±0.12-unit support, sized for P1's one-unit modules, averaged P2/P3's 0.10–0.20-unit dots and rays below threshold. Each feature class now uses a support matched to its width and a threshold calibrated from its own samples (two-means). P3 ray width is read as integrated coverage.
* **Dead space removed.** These changes ship as Profile 2 Format 0.25 (byte `19`) and Profile 3 Format 0.26 (byte `1A`). Every site now carries a value, and sites are ordered by a keyed bijective hash so the codeword is spread uniformly. A specified SplitMix32 whitening stream (modulation, not ECC) prevents payload-dependent empty or solid regions. The previous P2 layout left everything below y = 22.75 empty. In the previous P3 layout, 400 of 573 rays were blank stubs.
* **Safe area enforced.** Payload modules now keep a declared ink clearance from the frame, from the finder arcs (including round caps and the concave side), from the Grid Key, and from the radial alphabet. The old moat let modules overlap finder ink (P2 −0.32, P3 −1.5 units).
* **P3 polar join.** The ring field and Ring Key move outward (24.62 / 25.11), so the Ring Key-to-ray gap matches P2 (1.19 units) instead of a 1.85-unit dark annulus.
* **Ring arcs over half a turn** now set the SVG large-arc flag. Previously they were drawn as the complementary short arc on the wrong side of the ring.
* **Profile 1 decoder (format unchanged).** P1's empty reference at (1.5, 11.5) was a payload module, which skewed the threshold whenever that bit was set. It now uses a point in the polar aperture. Thresholds are also per class. On 40 random renders, failures at 6–8 px/unit fell from 19/40 to 0/40, and the committed iPhone fixtures still decode. The P3 calibration score's polar empty reference was a data ray and has moved too.
* **Tests now run in Release.** `assert()` was compiled out under `NDEBUG`, so CI executed none of the checks. Tests use an always-on `CKEY_CHECK`. New suites cover render → rasterize → decode round trips for all profiles (including all-zero/all-`FF` payloads, rotation, winding, inversion, keystone and blur) and white-box layout checks for site counts, clearance-boundary stability, uniform spread, measured ink gaps and dead space. CI adds a Debug ASan/UBSan job.
* Site layouts are built once and cached instead of on every call. On a 1080p noise frame, P3 decode fell from about 27 ms to 18 ms per call. The remaining cost is sampling all 8 orientations when the cheap contrast gate passes.
* The image decoders and calibration scores share one argument check (finite coordinates, `height × stride` overflow).
* A corrupt Profile 2 length field now returns `CKEY_INTEGRITY_FAILURE` instead of `CKEY_INSUFFICIENT_BUFFER`.
* The CLI rejects malformed hex such as `1g`, `-1`, ` 1` and `0x`, and reports real profile and format numbers.
* Stale format references corrected in the README, CONTEXT, header and code comments. The three `patches/` files no longer applied and were removed (they remain in history).

## 0.1.0-dev.48 — 2026-11-04

* Enlarged the radial safety moat surrounding the actual P2/P3 finder arcs and closed the unnecessary annular gap between each radial alphabet and Cartesian field. P2 emits Format 0.23; P3 emits Format 0.24.
* Corrected the dev.47 real-time diagnostic string so the iOS reader builds.

## 0.1.0-dev.47 — 2026-11-04

* Added a live reader diagnostic panel: detected border side, candidate count, independent P1/P2/P3 key-fit scores, decode-attempt time, and per-profile integrity outcome refresh while scanning.

## 0.1.0-dev.46 — 2026-11-04

* Added an independent native Profile 2 calibration-score ABI and reader fit. Profile 2 no longer uses Profile 1 key coordinates before projective sampling.

## 0.1.0-dev.45 — 2026-11-04

* Added Profile 2 Format 0.22, correcting its Cartesian finder clearance from four square reservations to the actual curved finder quadrants.

## 0.1.0-dev.44 — 2026-11-04

* Corrected Profile 3 finder clearance to reserve the finders' true outer corner quadrants. This removes the four false inward wedges and prevents Cartesian payload marks from printing behind the visible finder arcs.
* Added Profile 3 Format 0.21: all 34 ring rows now occupy the same outer radius while preserving a 7-unit central quiet zone.
* The iOS reader now fits compact (P1/P2) and extended (P3) candidates against their own calibration fields. A calibration score from one profile can no longer choose the sampling transform for another profile.

## 0.1.0-dev.43 — 2026-11-03

* Replaced the live reader's generic Vision rectangle path with a measured border-component locator. It fits an oriented quadrilateral from the continuous frame, then independently refines all four corners against the permanent Grid, Ring, Ray, and finder keys before decoding.
* Explicitly selects the physical back wide camera, holds 1× zoom, limits focus to the far range, and disables video stabilization so the capture geometry does not switch focal systems mid-scan.
* Added a reproducible synthetic training script and generated on-device logistic candidate ranker. It only orders measured border components; structural keys plus RS and CRC remain the decode acceptance gate.
* Added Profile 2 Format 0.19's border safety moat and Profile 3 Format 0.20's 34-ring filled polar field and 5-unit frame moat. These mappings prevent payload marks from spilling into the frame and remove the large unused P3 centre.

## 0.1.0-dev.42 — 2026-11-03

* Added Profile 3 capacity/ECC tiers at 256, 384, 512, 768, 1,024, 1,536, and 2,048 payload bytes. The fixed optical codeword stays fully populated; each tier changes the RS data/parity split, and automatic encoding selects the smallest tier that fits.
* Added deterministic SplitMix32 visual fill to new Profile 2 and Profile 3 frames, protected by RS and checked by frame/CRC validation. This prevents short payloads from rendering as mostly empty fields; it provides no secrecy.
* Kept decoders compatible with legacy Profile 2 and 3 frames, and added the 0.17/0.18 format specifications.
* Fixed the Mac renderer's stale-preview behavior on invalid input, stopped padding Profile 1 text into a different payload, debounced regeneration, updated preview effects without reloading the SVG, and rendered Profile 3 at the same module scale as lower profiles.
* Expanded live scanner candidates from the visible border bounds to account for the border's inset from the canonical sampling field. This is a geometry correction; renderer-origin camera scans still need physical-device validation.
* C++ core tests, optical locator fixtures, and version-node validation pass. Xcode builds and physical iPhone scans are not available in this environment.

## 0.1.0-dev.41 — 2026-11-03

* Reworked the iOS scanner hot path to try measured quadrilaterals directly against Profiles 1, 2, and 3 before any search. Vision now runs on alternating frames as a rotation/perspective fallback; continuous key-based refinement is restricted to one leading candidate instead of every candidate. Camera capture resolution remains adaptive at 1080p/high.
* Split detection and CKey decode timing so the displayed measurements no longer include the same work in both values. Updated scanner documentation to distinguish experimental multi-profile camera paths from camera-validated performance.
* No on-device speed, distance, or reliability target is claimed yet. Xcode and a physical iPhone are required to build and characterize the live scanner.

## 0.1.0-dev.40 — 2026-11-03

* Fixed Swift/C interop in the macOS renderer: C diagnostic message storage is converted from its imported fixed-size tuple safely, and SVG output buffers are passed through scoped mutable buffer pointers. This resolves the renderer's Xcode compile error without changing C ABI or wire-format behavior.
* Bumped all current software-version nodes. The CKeyReader and renderer still require Xcode validation on macOS; this Linux workspace cannot run Xcode.

## 0.1.0-dev.39 — 2026-11-03

* Corrected the macOS renderer project's current-SDK import and runtime status interpolation, keeping its UI build path explicit and versioned.

## 0.1.0-dev.38 — 2026-11-03

* Added the shared C ABI contracts required for projective grayscale sampling of the Profile 2 and Profile 3 encodings. The Mac rendering laboratory and camera reader use the same versioned native core.

## 0.1.0-dev.37 — 2026-11-03

* Removed the stale Grok-era nested-finder rewrite script so no repository tool can reapply rejected protruding or nested finder geometry.

## 0.1.0-dev.36 — 2026-11-03

* Applied the approved one-arc, inward-facing rounded-corner finder to all profiles. Profile 2 now emits Format 0.15 (`0F`); Profile 3 now emits Format 0.16 (`10`). Their legacy Format 0.9/0.10 codewords remain decoder-readable.
* Added profile-specific format documents and tests asserting each encoder emits its versioned frame byte and the common canonical finder SVG construction.

## 0.1.0-dev.35 — 2026-11-03

* Established the approved finder construction as CKey Format 0.14.0 / byte `0E`: a continuous rounded-square border and exactly one broad inboard arc per corner. Every arc opens toward the central polar field; only the top-left arc is split for orientation.
* Applied this normalized construction to the Profile 1 locator, Profile 2 text/binary renderer, and Profile 3 extended renderer. Added profile-specific SVG regression assertions for the one-arc geometry and retained reader calibration sampling of the same signature.

## 0.1.0-dev.34 — 2026-11-03

* Replaced the still-incorrect inside-corner interpretation with CKey Format 0.13.0 / byte `0D`. Finder arcs now form the symbol's convex external rounded corners; frame side segments terminate into the outermost finder arc. Only the top-left finder set is split for orientation.
* Updated reader calibration geometry and Profile 3 finder reservations to the external-corner definition. Historic formats 8, 11, and 12 remain decoder-readable; new locators emit format 13.

## 0.1.0-dev.33 — 2026-11-03

* Superseded the rejected dev.32 nested-finder interpretation with CKey Format 0.12.0 / byte `0C`: one broad rounded-corner arc per corner, aligned to the structural rounded border. The top-left arc alone has the measured diagonal split; it does not project a three-finger pattern into payload space.
* Updated Profile 3 clearance and shared reader calibration samples to the actual single-arc geometry. Formats 8 and 11 remain decoder-readable for historic fixtures; new locator encodes emit format 12.

## 0.1.0-dev.32 — 2026-11-03

* Introduced CKey Format 0.11.0 / byte `0B` for the locator's reference-oriented finder correction. Each corner now has three inset, edge-oriented, round-capped quarter arcs; top-right, bottom-right, and bottom-left sets are continuous, while every top-left arc has a measured 0.85-module diagonal clear gap.
* Applied the same proportional finder renderer to all capacity profiles and corrected Profile 3's reservation geometry to follow the real inset arc envelope rather than the previous large leaf-shaped clearance.
* Added finder-signature samples to the native continuous calibration score used by CKeyReader's geometry refinement. These samples are localization evidence only; Grid/Ray/Ring keys plus RS and CRC remain the decode acceptance gates. Format 8 remains decoder-readable for historical camera fixtures but is no longer emitted.

## 0.1.0-dev.31 — 2026-11-03

* Added experimental Format 0.10.0 Profile 3 for 1–2,048 byte payloads: ten byte-interleaved systematic RS(255,223) blocks, 320 total parity bytes, CRC-32C, deterministic codeword decode, and SVG rendering.
* Expanded polar capacity by lateral geometry rather than dot shrinkage: 24 rings at 288 angular slots, exact run-length arcs, and 573 data rays with a calibrated 3-bit Gray-length/stroke-weight alphabet. Cartesian candidate order is a deterministic two-dimensional permutation so all usable regions fill evenly instead of forming top-to-bottom or radial dead zones.
* Restored the Grid Key to a 2×2 block made of the exact same 0.34-unit marks and 0.5-unit pitch as Cartesian payload; it now genuinely calibrates payload scale and clear-space ratio.
* Restricted Profile 3 finder reservation to its curved finder strokes and guard bands rather than an entire corner square.
* Excluded the structural-border guard from Profile 3 payload placement after visual inspection found modules outside the rounded frame.
* Reserved the interior of each curved finder as well as its stroke guard, while leaving the surrounding exterior Cartesian field available for data.
* Preserved the camera-validated Format 0.8 locator unchanged. Profile 3 image/mobile decoding remains unimplemented and is explicitly not camera validated.

## 0.1.0-dev.30, dev.29, dev.28, dev.27 — 2026-11-03

* **dev.28** (`98cdcb9`) first added Profile 3, Format 0.10.0. That entry was folded into the dev.31 entry above when dev.31 corrected it, so its text appears there.
* **dev.27, dev.29 and dev.30** have no commit that changed `VERSION` and no recorded entry. They were not published builds.

## 0.1.0-dev.26 — 2026-11-03

* Added experimental Format 0.9.0 Profile 2 for 1–200 byte text/binary payloads: a framed full RS(255,210) codeword with 45 parity bytes, CRC-32C, and deterministic SVG rendering in a denser 58×58 Cartesian / 144-slot eight-ring polar field.
* Added the public `ckey_text_symbol`, text encode/decode APIs, a `ckey_render_text_svg` renderer, and `ckey encode-text --utf8` / `--hex` CLI commands. Core tests verify a 200-byte round trip and correction of 22 distinct byte errors; renderer output is XML-valid and includes connected multi-unit payload arcs.
* Preserved the Format 0.8.0 locator and its camera reader. Profile 2 is not yet accepted by the grayscale/mobile image decoder and must not be represented as camera validated until its own sampling, real-capture, blur, damage, and noise suite passes.

## 0.1.0-dev.25 — 2026-11-03

* Replaced discrete scale/offset ladders in the iPhone reader with continuous, key-driven quadrilateral refinement. The new C ABI calibration-score operation evaluates the permanent Grid, Ring, and Ray keys for contrast and expected evidence under a proposed projective field; it never accepts a payload.
* The reader starts from each observed border/rectangle, then performs a shrinking-step coordinate search over physical scale and position to maximize that score before one shared-core decode. This removes fixed distance intervals from localization while retaining the decoder's RS and CRC acceptance checks.
* Kept C ABI version 2 and Format 0.8.0 unchanged: the new function is an additive ABI entry point. Physical-device benchmark evidence remains required.

## 0.1.0-dev.24 — 2026-11-03

* Reverted the dev.23 localization throttle and candidate caps after physical testing reported slower, less reliable reads. Restored the dev.22 farther-reading search behavior rather than retaining a regression.
* Configured the rear capture stream for 1920×1080 where supported (falling back to `.high`) and enables continuous autofocus plus continuous autoexposure. This addresses the acquisition limit directly: a distant CKey needs sufficient pixels per module before any decoder can recover blur, noise, or damage.
* Kept format 0.8.0 unchanged. This is a recovery and acquisition-quality change; benchmark results remain unclaimed pending device measurements.

## 0.1.0-dev.23 — 2026-11-03

* Made the border-first CKey locator the bounded hot path: up to six square-border candidates and three framing hypotheses are tested per sampled frame. The earlier broad Vision scale/offset sweep could attempt up to 1,800 core decodes per frame, producing the slow, intermittent far-distance reads reported from dev.22.
* Throttled generic Vision localization to once per 300 ms, capped it at eight candidates and three framing hypotheses, and retained it only as a fallback for rotated or projective symbols. The direct border locator is sampled every 80 ms and accepts dimmer blurred display edges before the calibration/ECC/CRC checks reject false candidates.
* Kept format 0.8.0 unchanged. Physical-device latency and damage/blur/noise recovery still require benchmark evidence.

## 0.1.0-dev.22 — 2026-11-03

* Added a CKey-specific, luminance connected-component locator before Vision rectangle analysis. It searches for the continuous bright rounded-square border at half resolution and tests the recovered square bounds directly with the shared C++ decoder.
* This directly addresses the dev.21 field result: Vision still failed to propose the distant symbol despite the core successfully decoding a 135 px camera-derived fixture when supplied its field corners. Vision remains as the fallback for rotated and projective candidates.
* Kept format 0.8.0 unchanged. This Swift implementation requires Xcode/iPhone validation and has no distance or latency claim until measured.

## 0.1.0-dev.21 — 2026-11-03

* Fixed distant-symbol candidate selection: the reader previously ranked generic Vision rectangles by area and only tested the largest eight, which the field recording shows were laptop/display rectangles rather than the contained CKey.
* Added a second Vision request for small, near-square candidates (up to 64 observations) and ranks the merged candidate set by squareness before testing up to 24 candidates. The expensive refinement is limited to the leading four candidates.
* Kept format 0.8.0 unchanged. This fix requires iPhone/Xcode validation.

## 0.1.0-dev.20 — 2026-11-03

* Expanded the far-distance fallback from 12 to 18 scale hypotheses (0.25×–1.80×) and from nine to 25 center shifts per scale for each of the six leading candidates.
* Added on-screen Vision candidate-size diagnostics to reveal whether the square detector is selecting the symbol, its surrounding canvas, or a larger object.
* Kept CKey Format 0.8.0 unchanged. The wider fallback is not yet compiled or benchmarked on-device; the UI reports the candidate bounds for the next field test.

## 0.1.0-dev.19 — 2026-11-03

* Broadened the distant-symbol localization fallback to test 12 candidate scales from 0.40× to 1.40× and a 3×3 center-shift grid for each of the six leading Vision squares. The existing small set of fast hypotheses still runs first.
* Kept format 0.8.0 unchanged. The wide fallback has not yet been compiled or benchmarked on-device; far-camera fixtures demonstrate decoder performance only when corners are supplied.

## 0.1.0-dev.18 — 2026-11-03

* Fixed the Swift compiler type-check timeout in quadrilateral generation by using explicit Double conversions and an imperative coordinate loop.
* Replaced deprecated iOS 17 camera orientation properties with supported video rotation angle APIs.
* Kept wire format 0.8.0 unchanged. Xcode build verification requires the user's Apple toolchain.

## 0.1.0-dev.17 — 2026-11-03

* Added a coarse-to-fine camera localization fallback: the six leading square candidates are tested at smaller and larger scales with small center shifts when the fast hypotheses fail. This addresses distant targets whose display boundary is detected instead of the symbol boundary.
* Added a far-camera regression image with a 135 px symbol (~4.7 px/module); the C++ image decoder recovers its payload when supplied the observed field corners.
* Kept format 0.8.0 unchanged. The far-distance fixture validates image decoding with known geometry; live Vision localization and latency still require physical-device measurement.

## 0.1.0-dev.16 — 2026-11-03

* Fixed the iPhone reader's corner-expansion helper: it previously ignored each requested scale, causing every candidate to use the same quadrilateral.
* Increased camera sampling from 8 to 20 frames per second and exposed CKey check latency while scanning, alongside Vision detection latency.
* Added a grayscale camera-preview regression crop from the user's laptop-screen capture; the shared C++ decoder recovers its locator payload with the captured perspective and rasterization.
* Kept wire format 0.8.0 unchanged. Camera live performance is not yet independently measured on the user's iPhone; QR-parity performance is not claimed.

## 0.1.0-dev.15 — 2026-11-02

* Fixed a throttled UI status update that left the reader stuck on “Testing square candidates…” after each frame.
* Candidate frames now publish their final CKey validation result immediately; empty-frame progress remains throttled.

## 0.1.0-dev.14 — 2026-11-02

* Loosened Vision square-candidate thresholds and added nearby quadrilateral scale hypotheses for the rounded CKey frame.
* Added live reader status showing whether Vision found candidates or the CKey structural/ECC checks rejected them, plus ongoing localization timing.
* Added a regression image from the user's exact CKey target at 20 px/module; the C++ image decoder reads its payload successfully. iPhone camera localization still requires on-device validation.

## 0.1.0-dev.13 — 2026-11-02

* Fixed two SwiftUI color inference errors in the iPhone reader by making foreground and status colors explicitly use `Color`.
* Versioned the build fix. Apple-platform compilation still needs verification in Xcode.

## 0.1.0-dev.12 — 2026-11-02

* Added an experimental iPhone SwiftUI reader that uses Vision for square-candidate localization and calls the existing C++ C ABI for pixel decoding.
* Added an Xcode project, camera permission copy, live result display, and separate Vision/CKey timing readouts; the app compiles `src/ckey.cpp` directly without duplicating encoding or decoding logic.
* Recorded iPhone build/run instructions and current camera-validation limits. The app has not been compiled or tested on an Apple device in this environment.

## 0.1.0-dev.11 — 2026-11-03

* Added C ABI grayscale image decoding for a caller-supplied symbol quadrilateral, with projective rectification, calibration-key checks, polarity normalization, data sampling, RS correction, and CRC-32C validation.
* Added an optical PGM fixture cropped from a browser-rendered v0.8 symbol; tests cover actual pixel-to-payload decode, rotated quadrilateral order, inverted polarity, and invalid geometry.
* Kept wire geometry at CKey Format 0.8.0. Full-frame symbol localization, lens-distortion correction, camera video scanning, and camera robustness benchmarks remain open.

## 0.1.0-dev.10 — 2026-11-03

* Corrected the finder interpretation to match the supplied geometry blueprint: one thick quarter-arc at bottom-left, top-right, and bottom-right, with the top-left arc split into two pieces for orientation.
* Kept the corner centerline radius at 4.5 modules and made each arc a 1.0-module band with round caps; the split has a one-module visible gap.
* Versioned the geometry as CKey Format 0.8.0 / format byte 08 and regenerated the CRC-32C/Reed–Solomon fixture. The payload, RS code, and C ABI remain unchanged.

## 0.1.0-dev.9 — 2026-11-03

* Restored three nested quarter-arc finders at all corners, with the three top-left arcs split by a one-module orientation gap.
* Made all inner-ring and Ring Key dots the same 0.30-module diameter; Ring Key arc caps are round while its visible length remains exactly four angular pitches.
* Encoded each of the 69 data dial bits by a short 0.65-module ray or long 1.35-module ray; calibration rays remain reserved and fixed.
* Versioned the wire geometry as CKey Format 0.7.0 / format byte 07 and regenerated the CRC-32C/Reed–Solomon example vector. The RS code and C ABI are unchanged.

## 0.1.0-dev.8 — 2026-11-03

* Increased the central clear radius and moved the four rings outward into a tighter radial group.
* Sized each ring dot to 72% of its adjacent-slot chord, so marks are visibly sized and closely spaced at every ring radius.
* Set the Ring Key arc width to one dot diameter and its centerline length to exactly four slot intervals.
* Versioned the geometry as CKey Format 0.6.0 / format byte 06; the RS(77,26) code and ABI remain unchanged.

## 0.1.0-dev.7 — 2026-11-03

* Fixed oversized ring dots: SVG circles inherited the parent group’s default 1-module stroke. Circle marks now explicitly use no stroke, rendering at their specified 0.12-module diameter.
* Added a renderer regression assertion for stroke-free 0.06-radius dots. The four-slot Ring Key arc remains 0.12 modules wide and spans the same four angular slots.

## 0.1.0-dev.6 — 2026-11-03

* Reduced ring data dots and Ring Key dots to 0.12-unit diameter; made the four-slot Ring Key arc exactly match four angular slots and dot diameter.
* Increased locator protection to shortened RS(77,26), adding 51 mathematically computed parity bytes and using 616 of 621 eligible sites for codeword bits.
* Bumped the C ABI because the public codeword grew from 42 to 77 bytes.
* Versioned the wire geometry as CKey Format 0.5.0 / format byte 05 and refreshed the independent vector.

## 0.1.0-dev.5 — 2026-11-03

* Updated locator geometry to one thick arc at three corners and a split top-left orientation arc with a one-module visible gap; enlarged the reserved corner envelopes to eliminate finder/data collisions.
* Replaced the previous vertical calibration sequence with the specified 2×2 Grid Key.
* Spread payload placement across angular sectors of the Cartesian matrix and round-robin across the dial plus all four inner rings.
* Versioned the wire format as 0.4.0 / format byte 04 and regenerated the CRC-32C/Reed–Solomon interoperability vector.
* Added maintained geometry and version context for the supplied blueprint.

## 0.1.0-dev.4 — 2026-11-02

* Reworked the polar field into four inner concentric rings plus a separate 1.9-unit annular dial band; all 12:00/9:00/9:05 calibration rays now stay within that band rather than extending to the center.
* Added a round-robin angular traversal, retained the outer Cartesian matrix, and replaced corner dots with three nested arcs; split the top-left finder into two arc segments for orientation.
* Advanced the wire format to 0.3.0 and preserved earlier version specs and fixtures.

## 0.1.0-dev.3 — 2026-11-02

* Replaced solid QR-like block finders with nested open corner arcs and switched the reference SVG to dark-field/light-mark styling with rounded data modules to follow the supplied target geometry.
* Advanced the wire geometry version to CKey Format 0.2.0; preserved the 0.1.0 draft and fixture as historical versions.
* Kept Reed–Solomon error correction and CRC-32C framing; geometry evolved independently of framing.

## 0.1.0-dev.2 — 2026-11-02

* Corrected the format note to reflect the implemented byte-codeword RS decoder and its tested eight-error correction bound.
* Fixed shared-library C ABI exports and bounded SVG render scale to prevent dimension overflow and unreasonable outputs.
* Added independent-review findings and synchronized the public software version and fixture metadata.

## 0.1.0-dev.1 — 2026-11-02

* Added draft locator-v1 framing with CRC-32C and shortened RS(42,26) over GF(256).
* Added C++20 encoder and byte-codeword decoder with correction of up to eight unknown byte errors.
* Added fixed Cartesian/polar calibration geometry and monochrome SVG renderer.
* Added C ABI, CLI encoder, independent RS parity reference/vector, unit tests, and CI workflow.
* Recorded versioning, context, research, license, and milestone rules.
* Not included: erasure correction, image/camera decoder, perspective recovery, SDKs, browser lab, or measured device benchmarks.
