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

## 0.1.0-dev.11 — 2026-11-03

* Added C ABI grayscale image decoding for a caller-supplied symbol quadrilateral, with projective rectification, calibration-key checks, polarity normalization, data sampling, RS correction, and CRC-32C validation.
* Added an optical PGM fixture cropped from a browser-rendered v0.8 symbol; tests cover actual pixel-to-payload decode, rotated quadrilateral order, inverted polarity, and invalid geometry.
* Kept wire geometry at CKey Format 0.8.0. Full-frame symbol localization, lens-distortion correction, camera video scanning, and camera robustness benchmarks remain open.

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
## 0.1.0-dev.42 — 2026-11-03

* Added Profile 3 capacity/ECC tiers at 256, 384, 512, 768, 1,024, 1,536, and 2,048 payload bytes. The fixed optical codeword stays fully populated; each tier changes the RS data/parity split, and automatic encoding selects the smallest tier that fits.
* Added deterministic SplitMix32 visual fill to new Profile 2 and Profile 3 frames, protected by RS and checked by frame/CRC validation. This prevents short payloads from rendering as mostly empty fields; it provides no secrecy.
* Kept decoders compatible with legacy Profile 2 and 3 frames, and added the 0.17/0.18 format specifications.
* Fixed the Mac renderer's stale-preview behavior on invalid input, stopped padding Profile 1 text into a different payload, debounced regeneration, updated preview effects without reloading the SVG, and rendered Profile 3 at the same module scale as lower profiles.
* Expanded live scanner candidates from the visible border bounds to account for the border's inset from the canonical sampling field. This is a geometry correction; renderer-origin camera scans still need physical-device validation.
* C++ core tests, optical locator fixtures, and version-node validation pass. Xcode builds and physical iPhone scans are not available in this environment.
