# Pre-test observations for repair

**Temporary file.** Implement what applies, then delete this file in the same change.

**Source:** owner discussion during the CKTEST-RH-001 hardening campaign (`clearkeysolutions/key-test`), 2026-11-09. The campaign tests the reader at `ede38f5` (0.1.0-build.85) and was still running when this was written.

**Rules for using this file:**
* Numbers below are **interim** (about 727,000 of 1.5M cases) and come from simulation. The final campaign reports (`REPORT_1M`, the adaptive report, minimal counterexamples) supersede them.
* Use the final reports to prioritise, not to decide *whether* to do the items in sections 1 and 2. Those apply either way.
* Validate every reader change with the harness: `./ckt build-candidate --ckey <this checkout> --label <name>`, then `./ckt regress --label <name>`, before it ships.

## 0. Interim picture (why this list exists)

| Observation | What it means |
|---|---|
| **Integrity: 0 wrong payloads** across every hard negative (random codewords, corruption, t+1, CRC forgery, splices, mirrors, foreign grids, blanks) | RS + CRC acceptance is sound. Do not weaken it. |
| **S01, clean reference: 99.92%** | The clean round-trip gate requires 100%, so it will fail. Investigate why clean, in-range symbols ever miss. |
| **Recoverable cases in the two easiest bins: 83.72%** | These frames contain the information. The ~16% gap is reader headroom, not a physical limit. |
| **S09, contrast: about 50%** even in its easiest bins | The weakest stratum. Probably the same root cause as bright screens (section 1.1). |
| **Failure split: 71% sampling/decoding, 29% corner fitting** | `gt_quad_decodes` in the results separates the two. |
| **Resolution:** success falls sharply below about 3 px/module | Roughly at parity with published QR results (about 3–3.5 px/module). |
| **P0.1 outperforms P0** | Consistent with the build-58 print findings. |
| **Burst fragility** | Bit-scattered placement lets a small blot damage many bytes. Plan in section 3. |

## 1. Reader hardening (blanket fixes; apply whatever the final numbers say)

### 1.1 Bright screens and low contrast
The owner sees, in personal testing, that bright screens fail. Often the code is not visible in the camera preview either. That part is a capture problem (section 2). The reader should still handle what does reach it:
* **Local adaptive thresholding.** Estimate black and white per tile or neighbourhood, using the symbol's own frame, finders and keys as references. Avoid one level per candidate.
* **Clipped pixels are unknown, not white.** Pixels at or near 255 inside the field should mark their sites as unreliable and feed erasure decoding (1.2).
* **Highlight-aware normalisation** before sampling, so a hotspot does not drag the whole field's threshold.
* **Moiré pre-filter.** A slight blur or downsample, applied only when a screen-like periodic pattern is detected.

### 1.2 Erasure decoding
`CONTEXT.md` states "Erasure decoding is not implemented." Implement it:
* RS corrects `2·errors + erasures ≤ parity`, so a *known*-bad byte costs half as much parity as an unknown one.
* **Erasure sources:**
  * clipped or saturated sites;
  * sites outside the image or crop;
  * sites with low sampling confidence (near the threshold, or calibration disagreement);
  * occluded regions detected by local statistics.
* CRC-32C stays the final authority. Erasure marking must not raise false accepts; the harness negatives check this.

### 1.3 Soft-decision retries
When hard decoding fails, retry with the least-confident bytes erased, Chase/GMD style. Cap the retries, because each costs CPU per frame.

### 1.4 Corner fitting (29% of failures)
* Prioritise cases where `gt_quad_decodes = true` but the reader failed. These are pure localization misses.
* An earlier harness observation is related, though out of the current P0 scope. On clean P3 and P1 renders, `ckey_refine_quad_gray8` converged up to about one module away from the true corners. In one case the calibration score was higher at the wrong quad than at the truth, so the refinement objective itself can be biased. Check whether the P0 refinement shares this.

### 1.5 S01 clean misses
Pull every S01 failure from the final report, reproduce it with `./ckt repro <case_id>`, and fix it. The clean reference must reach 100%.

### 1.6 Screens with moiré: decodes only from a few inches away (owner field test, 2026-11-08)
**Observation:** iPhone, the build-83 reader, a lower-resolution monitor showing a small P0 v1 code (`https://openckey.org`, 20 bytes).
* The camera image shows strong moiré: the rainbow interference bands between the screen's pixel grid and the sensor's.
* It decodes only when the code nearly fills the frame: about 3–4 in away for P0.1 and about 2–3 in for P0.
* At a normal distance, with the code well inside the scan frame, nothing decodes.

**Why it is not a resolution limit:**
* At the failing distance the code spans roughly 400–500 px of a 1080p frame. For v1 (29 modules) that is about 14–17 px/module, far above the ~3 px/module floor.
* Up close, the screen's pixels are resolved and the moiré disappears; further away they alias.
* So the failure is aliasing, not missing pixels.
* P0.1 working from further away fits this: its marks are bigger, and the fine P0 ring dots and rays alias first.

**What the reader does today** (source at `ede38f5`):
1. **Border search point-samples the frame.** `ckey_find_borders_gray8` reads one pixel every `step` pixels (2 at 1080p, 4 at 4K) with no averaging (`src/api_scan.inc`, the `is_mark` lambda). Point decimation is exactly what turns a screen's pixel grid into large moiré bands.
2. **One global threshold.** `frame_threshold` (`src/core_internal.inc`) takes the 10th and 90th percentiles of the whole frame (every 8th pixel), so a bright screen inside a darker room, plus moiré bands of ±tens of grey levels, pushes parts of the code to the wrong side.
3. **A small sampling footprint.** `gray_sample` uses a 3×3 bilinear footprint at ±`kLocatorSampleSpread`, too small to average out a moiré period that spans several modules.

**Fixes, in order of expected value:**
1. **Area-averaged decimation for border search:** average each step×step block, or build one box-filtered pyramid level, instead of reading one pixel. This is a cheap, local change, and it alone should remove most screen aliasing.
2. **Local thresholds:** per-tile (e.g. 32×32 px) black and white levels, or levels taken from the candidate's own frame, finders and quiet zone (also 1.1). Not one percentile for the whole frame.
3. **Moiré-aware module sampling:** once a candidate quad is found, low-pass the rectified region with σ ≈ 0.15–0.25 module, or widen the sampling footprint to about half a module, before reading sites. Do this always or when a periodic pattern is detected (a quick FFT peak check on the region). The modules are many pixels wide here, so the blur costs nothing.
4. **Multi-frame soft combining:** moiré bands move with tiny hand motion, while the code does not. Register consecutive frames to the same quad and average each site's sampled value (or log-likelihood) over 3–5 frames before RS. It also helps noise, glare and erasures (1.2, 1.3), and fits the SDK-owned capture loop (section 2).
5. **Capture side (section 2):** capture at a higher resolution, so the screen grid is closer to resolved, then decimate *with* area averaging. A slight intentional defocus also suppresses moiré, but is hard to control.

**How to verify:**
* **Harness:** stratum S13 (screens: subpixel rendering, moiré, refresh) measures this directly. Compare S13 by `screen_px_per_module` bin and by P0 vs P0.1, before and after each fix: `./ckt build-candidate`, then `./ckt regress`.
* **Real device:** the same monitor, recording the maximum decode distance for P0 and P0.1 before and after (target: decode at the distance where the code fills about half the scan frame).

### 1.7 Viewing angle, perspective and circle flattening (owner field test, 2026-11-08)
**Controlled colour test (same payload and size, round marks, one monitor):**

| Candidate | Background (RGB) | Marks (RGB) | Contrast | Result |
|---|---|---|---|---|
| A | 246,246,246 | 28,28,28 | 15.8:1 | like D |
| B | 246,246,246 | 36,36,36 | 14.4:1 | like D |
| C | 248,248,248 | 43,43,43 | 13.4:1 | similar to D, less reliable |
| D | 242,242,242 | 36,36,36 | 13.9:1 | fairly reliable up to ~24 in, only when perfectly straight on |
| E | 238,238,238 | 44,44,44 | 12.0:1 | up to ~16 in, slow and unreliable |

**Outcome:**
* **Angle was the confound in the earlier style comparison.** Once the phone is held perfectly straight on, Plain (black on white, rounded squares) does about as well as A, B and D.
* **Contrast matters at the low end only.** E (12:1) is clearly worse. Softening the default is not the main win, so keep Plain for now.
* **Round marks helped Plain** before angle was controlled (2–4 in to ~10 in). Re-check them straight on.

**What is left is angle sensitivity:** at a distance the reader needs the code almost perfectly square to the camera. Likely causes, which compound:
1. **Moiré changes sharply with small tilts and rotations** of the screen grid against the sensor grid. Fixes: section 1.6 (area-averaged decimation, a pre-blur before sampling, multi-frame combining).
2. **LCD contrast drops off-axis.** Fix: local thresholds (1.1, 1.6).
3. **Corner fit under perspective.** About 29% of interim failures are corner fitting (1.4), and the harness's S04 stratum (tilt) measures it.

**Core change to make the fit robust to trapezoid distortion and circle flattening:** both are the same projective transform, and the decoder already models it with a homography from four corners. But the fit uses only the corners, so a small corner error at an angle distorts the whole polar field, which is mapped through the same homography. Use the code's own circles as extra constraints:
* Fit an ellipse to ring 0 (r = 3.4) and the Ring Key ring in the image. A circle under a homography is an ellipse, and its centre, axes and orientation constrain the homography independently of the corners.
* Refine the homography jointly over the frame edges, the finder arcs and the ring ellipses (least squares, or robust IRLS), then sample.
* Optionally, for strong tilt, re-render the expected mark layout under the current homography and refine by direct image alignment (a few Gauss-Newton or ECC iterations on the rectified field).

Test it on S04 (tilt), S05 (rotation) and S13 (screens), with `gt_quad_decodes` showing how much is corner error.

**Already done in the iPhone app** (clearkeysolutions/ckey-web, `apps/ios`; app code, the SDK unchanged):
* region-of-interest decoding, alternating with whole frames;
* focus and exposure on the reticle;
* the phone no longer auto-locks while scanning.

Move these into the SDK camera layer (section 2) so every integrator gets them.

### 1.8 iPhone app results and what the SDK camera layer should adopt (owner field tests, 2026-11-09)
The changes are in the app only (clearkeysolutions/ckey-web `apps/ios`, merged to `main` at `28f5780`). The SDK is unchanged and still pinned to `0660391` (build 83).

**Field results** (an older Samsung TV, code about 4 in wide, P0 v1, 20 bytes):

| Before | After |
|---|---|
| Plain: about 3 in, only straight on | Plain: 30-38 in, reliable with considerable tilt |
| Paper on Teal: — | Paper on Teal: 66 in with considerable tilt, a little slow but reliable |
| Phone auto-locked mid-scan | Fixed |

At distance, light marks on a dark background read about twice as far as Plain on a screen. Recommend dark-background styles for codes meant for display; keep Plain for print.

**What the app does now** (move these into the SDK camera layer, section 2):
1. **Region decoding.** Two of every three frames decode only the reticle area plus an 18% margin, at full resolution and in place. Every third frame is decoded whole, so codes closer than the reticle still read.
2. **Focus and exposure on the code.** They follow the reticle centre, then the found code's centre, re-aiming only when it moves.
3. **Real-detail zoom only.** Zoom is used only up to `videoZoomFactorUpscaleThreshold`, where the sensor is cropped at full resolution. On device, the app's 4K format reports 1.0, so there is no zoom there.
   * Zoom that only enlarges was tried and dropped as unreliable.
   * The main gain it would bring is a workaround for the core's sparse border search: at 4K, `ckey_find_borders_gray8` reads every 4th pixel. That is the same point as 1.6 fix 1.
4. **Brackets on the code.** Candidates come from `ckey_find_borders_gray8`, refined by `ckey_refine_quad_gray8` (`CKEY_FIELD_SCALABLE`), and are gated on the calibration score.
   * Measured on build 85: whole codes score 297–752 on clean renders, and every other rectangle (banners, cards, buttons, parts of the code) scores 0.
   * The app's gate is 15. A score of 150 or more locks at once; weaker candidates need a second sighting.
   * The top three candidates are each decoded in a tight crop, so the brackets lock onto the code that actually read, on its four refined corners (they lean with tilt).
5. **No auto-lock while the camera runs** in the app.

**Tracking: solved in the app; ship it in the SDK as an optional flag.** After several iterations the owner rates the app's live brackets "pretty good" (ckey-web `main` at `223d6b5`, files `apps/ios/Shared/Scanning/RegionCameraScanner.swift`, `BracketOverlay.swift`, `ScannerController.swift`). What made it work, and what did not:

| Tried | Result |
|---|---|
| Brackets from failed-decode frames only | frozen after a read, jumpy |
| Vision `VNTrackObjectRequest` between core fits | drifted and grew on the flat, low-texture code |
| Fixed exponential easing in SwiftUI | wobbly and steppy |
| **Core re-detection on a small crop every frame** | true corners every frame, no drift |
| **Corner correspondence between frames** | fixed the "diamond" twist |
| **Size continuity in the tracker** | fixed jumps to a same-centred larger card |
| **Capture timestamps + One Euro filter + critically damped spring, drawn by CADisplayLink on CAShapeLayers** | smooth |

The pipeline that works:
1. **Detect every frame, separately from decoding**, on its own queue, and keep going after a read.
   * Run `ckey_find_borders_gray8` and `ckey_refine_quad_gray8` (`CKEY_FIELD_SCALABLE`) on a crop around the last corners: the last box grown 35%, plus the region margin.
   * Keep the best-scoring candidate whose centre moved less than half the code's size and whose size changed by less than 25%.
   * Hold the last corners for up to 8 missed frames, then drop the track.
   * A core fit re-seeds the track whenever a code is found or decoded.
2. **Gate on the fit score.** `ckey_refine_quad_gray8` scored 297–752 for whole codes and exactly 0 for every other rectangle in renders, and real frames score lower. The app uses:
   * a gate of 15;
   * an immediate lock at 150 or more, or when the code just decoded;
   * otherwise, a second agreeing sighting.
3. **Corner correspondence.** The core reports a code's corners from any starting corner and in either winding. Before comparing, smoothing or tracking, reorder each set to the cyclic order (4 starts × 2 windings) with the least squared distance to the previous set.
4. **Time by capture.** Carry each frame's presentation timestamp, converted to the host clock (`CMSyncConvertTime` from `session.synchronizationClock`).
5. **Smooth in screen space, per display frame:**
   * **One Euro filter** (Casiez et al., CHI 2012) on the 8 coordinates, timed by capture time: `minCutoff` 1.2 Hz, `beta` 0.02 per pt/s, derivative cutoff 6 Hz.
   * **Latency projection:** project the filtered target along its velocity by 75% of (display target time − capture time), capped at 120 ms.
   * **Critically damped spring** (ratio 1, response 0.08 s), integrated exactly per display frame.

   In simulation this gives about 0.2 pt of wobble when still (against 1.5 pt of noise), about 14 pt of lag at 300 pt/s, and a few points of overshoot on an instant stop.
6. **Draw outside the UI framework.** `CAShapeLayer`s stepped by a `CADisplayLink` (60–120 Hz). Only a "locked" flag goes through SwiftUI; the corners go through a lock-protected feed.

**SDK work: make all of this optional, so integrators can draw their own.** Some developers will have their own overlay, branding or tracking. Proposed API for the SDK camera layer (Swift first, then Kotlin):
* **`CKeyCameraScanner.tracking: Bool`** (default on). It runs the per-frame detection and tracking pipeline (1–4) and publishes tracked corners even when no overlay is shown.
* **`CKeyCameraScanner.onTrack: ((CKeyTrack?) -> Void)?`**, the raw data for custom UIs. `CKeyTrack` holds:
  * `corners` (4 points, normalized sensor coordinates, in a stable order across frames);
  * `captureTime` (host seconds);
  * `score`;
  * `decoded` (Bool).

  It is delivered on a caller-chosen queue. `nil` means the track was lost.
* **`CKeyBracketOverlay`** (UIView / SwiftUI view), the built-in brackets (5–6), with tunable parameters (`minCutoff`, `beta`, `response`, `leadGain`) and styling (colours, line width, corner length, padding). It is opt-in: an integrator who wants their own drawing uses `onTrack` and never adds it.
* **`CKeyCameraPreview` helper:** converts normalized sensor points to view points (`layerPointConverted(fromCaptureDevicePoint:)`) for custom overlays.
* **Region, focus and zoom** (items 1–3 above) are also separate flags: `regionOfInterest`, `focusOnCode`, `realDetailZoom`, each on by default. An app that manages its own capture device can turn them off.
* **Core C ABI (optional, later):** a `ckey_track_gray8(gray, …, prev_quad, out_quad, out_score)` helper that does step 1's crop, search and continuity check in C, so the Kotlin and WASM SDKs get the same tracking. Corner correspondence (step 3) belongs there too, or the core's refine should report corners in a canonical order.

Corner accuracy itself (1.4 and 1.7: the ring-ellipse-constrained fit) will make the brackets steadier still, and the tracking parameters should be re-tuned after it lands.

## 2. Own the camera (ship it in the SDK, not app code)

Industry scanners (Scandit, Dynamsoft, barKoder) run their own barcode-specific exposure and focus control rather than the OS's whole-scene auto-exposure. CKey should ship this **in the SDK**, so it works as a clean plug-in.

### 2.1 Architecture
* **C core:** add a per-frame *camera advice* output alongside the decode result. It covers:
  * the candidate's centre and size;
  * the clipped fraction inside the candidate;
  * local contrast;
  * a sharpness measure;
  * a recommendation, e.g. "exposure −1 EV" or "zoom 1.8×".

  The **policy lives in C**, so iOS and Android behave identically and the key-test simulator can test it in a closed loop.
* **Swift capture module** (and later Kotlin/CameraX): owns `AVCaptureSession` and applies the advice.
* **Two integration modes:**
  * **Turnkey:** a scanner view or session where the SDK owns the camera.
  * **Bring your own camera:** the host feeds frames and receives advice, or lets the SDK configure its `AVCaptureDevice`, with opt-out flags so the two don't fight over exposure and focus.
* **The host app must still** provide `NSCameraUsageDescription`. Ship as an XCFramework or Swift Package.

### 2.2 Exposure (fixes "can't even see it in the preview")
* **Meter on the candidate:** `exposurePointOfInterest` = candidate centre, then set `exposureMode = .continuousAutoExposure` to apply it.
* **Negative bias when the candidate clips:** `setExposureTargetBias(...)`, within `min/maxExposureTargetBias`.
* **Bias cycling while nothing decodes:** after N failed frames, rotate through 0 / −1 / −2 EV. Reset on decode or when the candidate is lost. This also covers screens too bright to detect at all.
* **Trade-off:** a darker exposure on screens shortens exposure time and can show refresh banding. Multi-frame attempts cover it.

### 2.3 Focus up close
* Use the virtual multi-camera device (`.builtInTripleCamera`, then `.builtInDualWideCamera`, then wide). On iPhone 13 Pro and later it auto-switches to the ultra-wide macro lens, which focuses at about 2 cm.
* **Zoom so users hold the phone farther away** (Apple's WWDC21 barcode guidance): read `minimumFocusDistance` and set `videoZoomFactor` so a typical code fills the frame at or beyond that distance.
* Set `autoFocusRangeRestriction = .near` where supported, with `.continuousAutoFocus`.

### 2.4 Other capture items
* **Region of interest:** restrict the search area; this is faster, and exposure and focus follow the code.
* **Multi-frame decoding:** try several frames, not one.
* **Capture preset:** build 51 made the reader prefer 4K for P3's decode floor. P3 is out of scope, and the campaign tests P0/P0.1 at 1080p-class capture. Revisit whether 1080p (faster, lower latency) should be the default for P0, once the final per-density results are in.

## 3. Burst fragility: the chosen plan

**The problem.** Format 0.27 and 0.28 order data sites by `lowbias32(id XOR lowbias32(0x434B5030 XOR v))`, and site i carries codeword bit i. Each byte's 8 bits are therefore scattered across the whole field. A small blot (a scratch, a glare spot, a finger) touches a few bits of *many* bytes, and one bad bit ruins its byte, so a 5% blot can wreck far more than 5% of the bytes. QR and Data Matrix place each byte in a compact patch of about 8 modules.

Owner decision (2026-11-09): pursue options 1, 2, 3 and 4. Option 6 is conditional. Option 5, higher default ECC, is not a code change. It stays a caller and renderer choice, but recommend Q or H whenever a logo covers data (3.3).

### 3.1 Option 1: erasure decoding (reader only, do first)
This is item 1.2. A byte the reader *knows* is bad costs 1 parity byte; an unknown error costs 2. It works on every symbol already printed. It is also the mechanism that makes 3.3 (logos over data) cheap.

### 3.2 Option 2: soft-decision retries (reader only, do first)
This is item 1.3. When hard decoding fails, erase the least-confident bytes and retry, Chase/GMD style. Cap the attempts.

### 3.3 Option 3: byte-grouped placement (new format version; scoping)
**The one rule that changes:** the site-ordering rule in the "Order and value" section of the 0.27 and 0.28 specs. Each byte's 8 bits go to 8 *neighbouring* sites instead of hash-scattered sites.

**Everything else is unchanged:**
* the skeleton (frame, rounded border, finders, split top-left arc);
* the Grid, Ring and Ray Keys, and version identification from them;
* the lattice, ring and dial geometry, mark shapes, site sets, and capacity per version and level, for all 31 versions in both layouts;
* SplitMix32 whitening;
* the frame layout, RS blocks, parity and CRC-32C;
* the renderer and the design system.

**The symbol looks the same to the eye.** Whitening still randomises every printed value; grouping only changes which sites belong to the same byte. The empty centre does not grow.

**Design decisions to make in the spec:**
1. **Clumps per site kind.** Define a deterministic path through each region and cut it into runs of 8:
   * Cartesian: compact 2×4-style clumps, e.g. a serpentine walk over row pairs;
   * rings: 8 consecutive slots on one ring;
   * dial: 8 consecutive rays.

   Define a rule for leftover sites near the curved border, finders and radial clearances. The existing "at most 7 trailing whitening-only sites" rule may absorb them. All boundaries must be decided with slack, so independent implementations agree.
2. **Shuffle bytes, not bits.** Keep a keyed hash, but use it to permute *clumps* (which clump gets which codeword byte). This is option 4 (3.4).
3. **Telling old from new.** The format byte sits inside the frame, so the reader must choose an ordering before it can read it. Recommended: sample the sites once, then try both orderings. Unscrambling, RS and CRC are cheap, and RS + CRC make a wrong guess fail safely. The alternative, a visible key change, is more invasive.
4. **Keep each byte within one mark kind where possible.** Ring dots fail first in print (build 58). Grouping weak sites together *reduces* the expected number of bad bytes for random noise as well as for bursts. Expected bad bytes = Σ over bytes of (1 − Π(1 − p_site)), which is smaller when similar error rates share a byte.

**Side benefit: logos over data.** Build 57 noted that a logo larger than the empty centre (radius about 3.2) "would need byte-grouped placement, a wire-format change". With grouping, a logo covers only the bytes under it, and the reader can mark them as erasures. The covered area comes out of the ECC budget, so a large logo needs level Q or H. The design-system checker should compute the remaining margin, not just test that the symbol decodes.

**Cost:**
* new format numbers for P0 and P0.1 (e.g. 0.29 and 0.30);
* a rewritten placement section and new conformance hashes;
* encoder and decoder ordering functions plus the dual-ordering attempt: on the order of a few hundred lines plus tests;
* readers keep the old ordering, so printed codes still scan.

### 3.4 Option 4: regional block interleaving (with option 3)
The keyed clump permutation in 3.3 spreads each RS block's bytes across the field. A blot then costs every block a little instead of one block a lot. The existing byte interleave (j outer, b inner) already alternates blocks in codeword order. The clump permutation adds the spatial spreading. Verify with a test: for every version, each block's bytes cover every quadrant and site kind in proportion, as `tests/layout_tests.cpp` already checks for the codeword as a whole.

### 3.5 Option 6: inner per-region code (conditional; probably not needed)
A small parity code per region, whose main value is *flagging* which regions are bad so they become erasures.

**Do it only if** the final results, with 3.1 to 3.4 in, show that the reader cannot flag damaged regions reliably from sampling confidence alone. That would show up as erasure-capable failures that the erasure decoder misses. It costs capacity in every region and adds spec complexity. Erasures (3.1) plus grouping (3.3) cover most of its benefit.

### 3.5a Generative toughness option: levels above H, instead of option 6
Owner question (2026-11-09): could option 6 be an opt-in encoder setting, off by default, for people who will trade payload for damage tolerance? Recommendation: offer that choice as **extra ECC levels above H**, not as an inner code.

* **Readers still pay the full cost.** Optional *generation* does not make *support* optional: every reader must decode every mode, or those codes will not scan. An inner code would add spec, conformance tests, decoder machinery, and one more thing to guess before the format byte can be read.
* **A higher level is the same machinery.** It is the same RS code with more parity: one more row in the level table, no new decoder path. Aztec takes this route and allows up to 95% error correction.
* **Spending the capacity on outer RS parity is about as good as an inner code, or better,** for phone-camera damage (blobs, glare, scratches, moderate noise), especially with erasures (3.1) and grouped placement (3.3).

**Proposed level set.** `pct` is parity as a percentage of each RS block. A level fixes `pct/2`% of the block in unknown bad bytes, or the full `pct`% in bytes the reader knows are missing (erasures, 3.1).

| Level | pct | Corrects (unknown bad bytes) | Corrects (known-missing) | Status |
|---|---|---|---|---|
| L | 14 | (7%) | (14%) | existing |
| M | 28 | (14%) | (28%) | existing |
| Q | 44 | (22%) | (44%) | existing |
| H | 60 | (30%) | (60%) | existing |
| **U** (Ultra) | 80 | (40%) | (80%) | proposed |
| **X** (Extreme) | 95 | (47.5%) | (95%) | proposed |

Names (owner decision, 2026-11-09): **U = Ultra** (80) and **X = Extreme** (95). The owner first called the 80% level "M" for Max, but M is already Medium, so it is U.

**Effective payload, honestly.** P0 (standard) capacity in bytes for the same symbol at each level, computed from the build-85 capacity table and the spec's parity rule. Every symbol also spends 12 bytes on header and CRC.

| Version (modules across) | L | M | Q | H | U (80) | X (95) |
|---|---|---|---|---|---|---|
| v1 (29) | 33 | 27 | 17 | 9 | **0** | **0** |
| v4 (36) | 66 | 52 | 38 | 24 | 6 | **0** |
| v8 (47) | 159 | 131 | 99 | 67 | 27 | **0** |
| v11 (58) | 276 | 228 | 176 | 124 | 56 | 4 |
| v16 (82) | 607 | 505 | 391 | 277 | 133 | 25 |
| v21 (116) | 1,289 | 1,085 | 835 | 593 | 293 | 65 |
| v26 (164) | 2,684 | 2,242 | 1,748 | 1,244 | 620 | 152 |
| v31 (232) | 5,555 | 4,619 | 3,579 | 2,585 | **1,285** | **303** |

P0.1 (print) at the same versions:
* U: 0 / 0 / 14 / 35 / 91 / 342 at v1 / v4 / v8 / v11 / v16 / v24, with a maximum of **945** bytes at v31;
* X: 0 until v15 (9 bytes), 76 at v24, and a maximum of **223** bytes at v31.

**Bottom line:**
* **U roughly halves H's payload** at the same size, and is about 23% of L's (1,285 vs 5,555 at v31).
* **X keeps about 5% of L's** at the same size: the maximum is 303 bytes (P0) or 223 bytes (P0.1). The smallest versions carry nothing at X, because the 12-byte header and CRC plus the parity eat the whole symbol.
* **Cost of a 64-byte payload:** H at v8 (47 modules across), U at v13 (67), X at v21 (116; P0.1 v24, 143). The symbol grows quickly above H, and a denser symbol needs more pixels, which works *against* robustness at the 1080p floor (about 3 px/module).
* **What X is for:** short identifiers on large prints or harsh surfaces, not general payloads. U is the more broadly useful addition.

**Implementation notes:**
* New level codes (4 = U, 5 = X) in frame byte 5, the spec's level table, and the encoder, renderer and SDK level choices.
* The reader must also try the new levels, since parity depends on the level and the level is read from inside the frame.
* Old readers will reject U and X codes, so ship reader support before encoder support.
* No new placement format is needed.

**Decide with data.** Add U only if the final campaign shows level-H cases in S10 and S11 still failing with *recoverable* damage once 3.1 is in. X is a product decision about niche use, not a robustness fix.

### 3.6 Order of work and proof
1. Implement 3.1 and 3.2 (reader only). Measure them with the key-test harness: `./ckt build-candidate`, then `./ckt regress`. Look first at S10 (glare), S11 (occlusion) and S16 (crops).
2. Specify and implement 3.3 and 3.4 as the next format versions. Add the new layouts to the harness plan and compare against the 0.27 and 0.28 baseline on identical conditions.
3. Revisit 3.5 only on the evidence described there.

## 4. Comparison baseline (key-test harness work, after the campaign)

Published QR, Data Matrix and Aztec studies are not comparable to this campaign's design. The fair comparison is to run zxing-cpp (QR, Data Matrix, Aztec) through the same simulator, with matched size, px/module, ECC fraction and payload, in a separate data directory. Also compare decode-from-true-corners, to separate the format from the reader.

## 5. Caveats
* The simulator's camera models are not yet validated against a real capture corpus. Confirm real-device behaviour, especially for 2.2 and 2.3, with physical iPhone tests.
* No certification is implied by any of this.
