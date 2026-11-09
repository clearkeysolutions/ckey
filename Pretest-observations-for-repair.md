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
| **Burst fragility** | Bit-scattered placement lets a small blot damage many bytes. This is a design question: see section 3. |

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

## 3. Burst fragility (format-level; decide after the final report)

Bits of each byte are scattered across the field (keyed hash ordering). A small blot (scratch, glare spot, finger) therefore touches a few bits of *many* bytes, and one bad bit ruins its byte. QR and Data Matrix do the opposite: each byte occupies a compact patch of about 8 modules, and RS blocks are interleaved across regions.

Options, in recommended order:
1. **Reader-only, no format change:** erasure decoding (1.2) and soft retries (1.3). Works on every symbol already printed.
2. **Format change (a new format version, e.g. 0.29):**
   * **Byte-grouped placement:** keep SplitMix32 whitening as an XOR mask, but place each byte's 8 bits in adjacent sites.
   * **Regional block interleaving:** each RS block is spread across the field so one blot hits every block a little.

   Build 57 already noted that byte-grouped placement would also allow larger logos. Needs a spec revision, encoder change, a decoder that reads both formats, and a compatibility review.
3. **Higher default ECC (Q/H):** costs capacity.
4. **Inner per-region code:** strongest, but complex. Probably not needed.

Go to option 2 only if the final S11 (occlusion) and S10 (glare) results show bursts dominate *after* option 1 is in.

## 4. Comparison baseline (key-test harness work, after the campaign)

Published QR, Data Matrix and Aztec studies are not comparable to this campaign's design. The fair comparison is to run zxing-cpp (QR, Data Matrix, Aztec) through the same simulator, with matched size, px/module, ECC fraction and payload, in a separate data directory. Also compare decode-from-true-corners, to separate the format from the reader.

## 5. Caveats
* The simulator's camera models are not yet validated against a real capture corpus. Confirm real-device behaviour, especially for 2.2 and 2.3, with physical iPhone tests.
* No certification is implied by any of this.
