# CKTEST-RH-001: findings of the CKey reader hardening campaign

Prepared for ClearKey Solutions. 10 November 2026.

Subject: 1,364,000 completed simulated test cases against the CKey two-dimensional optical code reader, core `ede38f5` (software 0.1.0-build.85). Status of the evidence: simulated, host-side, not a certification. Release label stated in the campaign analysis: UNQUALIFIED.

## Abstract

Campaign CKTEST-RH-001 tested the CKey two-dimensional optical code reader (core build 85) in simulation. It completed 1,364,000 cases: 1,000,000 pre-registered structured cases in 20 strata, 354,000 of 500,000 planned adaptive cases and a 10,000-case pilot. The run stalled before the boundary search and the 100,000-case holdout, so it is incomplete and the reader is unqualified. Each case passed a symbol from the production renderer through a staged camera model and gave the frame to the production reader. No wrong payload was accepted in any case, including 100,000 hard negatives (invalid, damaged or absent symbols). From zero events the one-sided 95% upper bound on the wrong-payload rate is 3.0 per million structured trials, assuming independence. The reader decoded 50.14% of 900,000 valid symbols and 68.93% of the 430,176 classed physically recoverable. It missed 421 of 34,136 recoverable clean frames, some of them at or above the declared 6 pixels per module. About one failure in five (82,307 of 448,718) was a failure to locate the symbol. The rest were failures to read located marks. The report derives candidate mechanisms from the reader's source: a whole-frame threshold, point-sampled search, ring dots 0.4 module pitch across on centres about 0.3 pitch apart, bit-scattered placement that spreads local damage over most bytes, hard-decision decoding and a planar corner fit. In a local comparison on a 6,000-case subset, a repair build read 76.0% of 3,267 recoverable frames against 64.4%, at more than twice the median time. The report gives a prioritised plan with an acceptance test for each item. All results are simulated and none is a certification.

## 1. Introduction and objectives

### 1.1 System under test and purpose

CKey is a two-dimensional optical code. A rounded-square frame with four corner finder arcs encloses a field that carries data in three kinds of mark: square modules on a Cartesian lattice, dots and joined arcs on concentric rings, and radial rays on a dial around a clear centre. The reader is a C++ core that finds the frame in a greyscale camera image, fits the four field corners, samples every data site, and accepts a payload only after Reed-Solomon (RS) error correction and a 32-bit cyclic redundancy check (CRC-32C) both succeed.

Campaign CKTEST-RH-001 was commissioned by the mandate in `SPEC.md` of the campaign repository (`clearkeysolutions/key-test`). Its purpose was to measure, with pre-registered conditions and locked acceptance gates, how the production reader behaves across the conditions a phone camera meets, to find the mechanisms behind its failures, and to drive repairs from reproducible evidence. The mandate required at least 1,500,000 completed cases: exactly 1,000,000 structured cases in 20 balanced strata, at least 500,000 adaptive cases chosen from the weaknesses the first million exposed, and a separate sealed holdout of 100,000 cases.

This report states what was tested, how, what was found, what the evidence establishes, and what has been done and remains to be done. Every figure comes from the campaign's frozen evidence or from the reader repository's records, or is computed here from them with the computation shown. All results are simulated. No result in this report is a measurement on a physical device, and none is a certification.

### 1.2 Questions

The campaign was designed to answer five questions.

1. Whether the reader ever accepts a payload different from the one encoded (a wrong payload).
2. Whether it reads clean symbols at the resolution the format declares, 6 pixels (px) per module.
3. What fraction of physically readable frames it reads, by condition and severity.
4. Where it fails, whether the failure is in locating the symbol or in reading its marks.
5. Whether it stays within its time and memory limits on every input.

### 1.3 Gates

Seven release gates, G1 to G7, were fixed in `configs/gates.yaml` and locked with a signed record before any counted case ran (lock time 2026-11-08 05:57:47 UTC, approver recorded as "Caleb", file SHA-256 `a53d0dae...`). The gate evaluator refuses to run if the file changes after the lock.

Table 1. The release gates as locked. The full requirement, result and status of each gate are in Appendix B.

| Gate | Requirement |
|---|---|
| G1 | No accepted wrong payload in the structured cohort, the adaptive cohort and the holdout. |
| G2 | 100% exact round trip for clean valid symbols (stratum S01) at or above 6 px per module. |
| G3 | At least 99.9% decode success on the normal-use distribution: physically recoverable valid cases in severity bins 0 and 1, weighted by declared use weights per stratum. |
| G4 | 95th-percentile single-frame decode time of 200 ms or less on a named reference iOS device. |
| G5 | No significant regression against the baseline: non-inferiority margin 0.5 percentage points per cell of at least 400 cases, significance 0.05, Holm adjustment for multiple comparisons. |
| G6 | No sanitizer finding, crash, timeout, unbounded memory growth or unbounded compute; peak memory of 512 MB or less per single-frame decode; at least 60 minutes of fuzzing. |
| G7 | Exactly 1,000,000 structured and at least 500,000 adaptive completed cases with verifiable manifests; holdout of at least 100,000; real-device validation complete. |

The labels a campaign can earn are, in order, UNQUALIFIED, SYNTHETICALLY_HARDENED, DEVICE_VALIDATED and CERTIFICATION_CANDIDATE. A certified label is never assigned by software.

## 2. System under test

### 2.1 Formats tested

By owner decision of 2026-11-08 the campaign tested Profile 0 only, in its two layouts. Profile 0 in the standard layout is Format 0.27 (frame format byte `1B`), called P0 in this report. Profile 0 in the print layout is Format 0.28 (byte `1C`), called P0.1. Both specifications are marked draft. Profiles 1, 2 and 3 were not tested. The reader adapter still ran the full production loop, which tries their decoders on each border candidate until one decoder succeeds, so a Profile 0 symbol misread as another profile would have been counted as a wrong payload.

Both layouts share one skeleton in a canonical field of 29 by 29 units: a frame (rounded rectangle, stroke 0.65 units), one finder arc of radius 2.3 units inside each corner with the top-left arc split for orientation, ring 0 at radius 3.4 units, and a dial of rays starting at radius 7.6 units. There are 31 versions. Version $v$ has $G(v)$ modules across the field, from 29 (version 1) to 232 (version 31), and module pitch $p = 29 / G(v)$ units. Every data mark scales with $p$. The symbol with its quiet margin is 31/29 of the field, so a symbol is $31G/29$ module pitches across: 31 at version 1 and 248 at version 31.

Table 2. Mark geometry of the two layouts, in units of the module pitch $p$, from the format specifications.

| Element | Standard layout (Format 0.27) | Print layout (Format 0.28) |
|---|---|---|
| Cartesian module | square of side 0.68p, corner radius 0.16p | same |
| Ring radial pitch | 0.86p | 1.0p |
| Ring dot radius | 0.2p (diameter 0.4p) | 0.3p (diameter 0.6p) |
| Slots per ring | one count S for all rings, S = 72 to 576 | equal arc: 8 times floor(2 pi r / (8p)) on a ring of radius r |
| Dial ray width | 0.2p | 0.35p |
| Dial rays, long and short | 2.2p and 1.0p | 2.4p and 1.0p |
| Closest spacing stated | ring slot centres "as little as 0.3 module apart" on the inner rings (text of Format 0.28), which is less than the dot diameter of 0.4p, so neighbouring printed dots overlap | ink-to-ink gaps: ring dots at least 0.39p, rays at least 0.6p |

Every site carries one bit of the codeword. In both layouts the order in which sites take codeword bits is a keyed hash of the site identity, so the eight bits of one byte lie at eight unrelated places in the field. This report calls that scattered placement. Printed values are additionally XORed with a pseudo-random whitening stream.

The frame is two magic bytes, format, profile, version, level, a 16-bit payload length, the payload, and CRC-32C over everything before it; 12 bytes are overhead. The codeword of $C$ bytes is split into $B = \lceil C/255 \rceil$ RS blocks over GF(256) (primitive polynomial `0x11D`), byte-interleaved. A block of $n_b$ bytes carries parity

$$r_b = 2 \left\lfloor \frac{q \cdot n_b + 100}{200} \right\rfloor, \qquad r_b \ge 2,$$

where $q$ is 14, 28, 44 or 60 for levels L, M, Q and H. An RS block with $r$ parity bytes corrects $e$ unknown byte errors and $s$ known-position erasures whenever $2e + s \le r$.

Table 3. Error-correction levels for a full block of 255 bytes, computed from the block rule, and payload capacity in bytes at the smallest and largest versions, from the specifications.

| Level | Parity bytes r | Parity fraction r/255 | Unknown errors corrected, r/2 | Fraction | Standard layout, v1 and v31 | Print layout, v1 and v31 |
|---|---:|---:|---:|---:|---:|---:|
| L | 36 | 14.1% | 18 | 7.1% | 33 and 5,555 | 11 and 4,123 |
| M | 72 | 28.2% | 36 | 14.1% | 27 and 4,619 | 7 and 3,453 |
| Q | 112 | 43.9% | 56 | 22.0% | 17 and 3,579 | 3 and 2,679 |
| H | 154 | 60.4% | 77 | 30.2% | 9 and 2,585 | 0 and 1,919 |

The standard-layout specification declares that "all 31 versions decode at 6 px per module clean". The print layout declares no separate figure, and the campaign assumed 6 px per module for it as well (open issue OI-3 in the campaign's `CONTEXT.md`).

### 2.2 The reader as tested

The reader under test is the production loop at commit `ede38f57aa1a20c456b1858f4fccd919e98ef269` (software 0.1.0-build.85, C interface version 4). The harness drives it through `runners/native/scanner_adapter.h`, a call-for-call C++ port of the Swift loop in `CKeyDecoder.swift` that adds instrumentation only. The loop is:

1. `ckey_find_borders_gray8` returns up to 6 border candidates.
2. For each candidate in order, `ckey_refine_quad_gray8` fits the field corners for Profile 0 and `ckey_decode_scalable_gray8` attempts a decode; then the same pair is tried for Profiles 1, 2 and 3.
3. The first decode that returns success is the answer.

Four properties of the build-85 implementation matter for the results. They were read from the source at the tested commit.

* **Whole-frame threshold.** The border search separates marks from background with one threshold per polarity for the whole frame. It takes the 10th and 90th percentiles of a histogram of every eighth pixel in each direction, $L_{10}$ and $L_{90}$, and sets the dark-mark threshold to $L_{10} + 0.32 (L_{90} - L_{10})$ (and symmetrically for light marks). If $L_{90} - L_{10}$ is below 32 grey levels it falls back to fixed thresholds of 92 and 164.
* **Point-sampled reduction.** The search examines one pixel per block of `step` by `step` pixels, with `step` = 2 for a 1080 by 1920 frame, without averaging. It then grows connected components and keeps those that do not touch the frame edge, have a bounding-box aspect ratio between 0.72 and 1.38, and fill less than 38% of their box.
* **Planar model.** Refinement and sampling map the canonical field to the image with a planar homography (the projective map between two planes) through four corners. Refinement is a coordinate descent on scale, rotation and offset followed by per-corner moves. There is no lens-distortion term and no model of a curved surface.
* **Hard decisions.** Each site is sampled with a weighted 3 by 3 kernel and compared with a threshold computed for its class of mark. Ring sites are then decided again against thresholds that depend on the two neighbouring slots. A site that maps outside the image makes the whole attempt fail. The RS decoder corrects unknown errors only; no site is ever marked as an erasure.

The production reader has no multi-frame fusion. Each frame is decoded independently.

## 3. Methodology

### 3.1 Simulator

Each test frame is produced by a staged image-formation model (`simulation/`, version `ckt-sim-v2`). The stages are, in order:

1. the production renderer's vector symbol, rasterised by an independent rasteriser;
2. the substrate: ideal, print (binary device dots, signed dot gain, halftone, paper blur and texture) or display (pixel grid with sub-pixel stripes, black matrix, cover-glass reflection, refresh banding);
3. the scene: clutter, distractors, lighting gradient, shadows, specular glare, surface damage and occluders;
4. projection through a pinhole camera with Brown radial distortion (coefficients $k_1$, $k_2$), a pose with roll, yaw and pitch, an optional cylindrical bend of the surface, and an exact rolling shutter;
5. sampling at four samples per pixel, exposure-time blur (translation, rotation, zoom) and the lens point-spread function (Gaussian residual blur, Gaussian or disc defocus, chromatic aberration, vignetting);
6. the sensor: auto-exposure, Poisson shot noise, Gaussian read noise, fixed-pattern noise, full-well clipping and quantisation;
7. the image signal processor (ISP): gain, tone curve, luma conversion, noise reduction, sharpening, optional rescale and JPEG-like quantisation.

The output is the 8-bit luma frame the reader receives. Each frame is a pure function of its case line and a named pseudo-random stream per stage (xoshiro256** seeded through SplitMix64). The ground truth of each frame is the four projected field corners.

Table 4. Camera models used (`configs/devices.yaml`), with the number of structured cases generated on each. The models are generic phone-class parameterisations. They are not measurements of any phone.

| Device model | Frame (px) | Focal length (px) | k1 | k2 | Sampling weight | Structured cases |
|---|---|---:|---:|---:|---:|---:|
| 1080p wide | 1080 by 1920 | 1,180 | -0.02 | 0 | 0.50 | 693,107 |
| Budget 1080p | 1080 by 1920 | 1,250 | -0.05 | 0 | 0.20 | 143,146 |
| 720p | 720 by 1280 | 790 | -0.03 | 0 | 0.15 | 80,079 |
| 1080p telephoto | 1080 by 1920 | 3,000 | 0 | 0 | 0.10 | 71,354 |
| 1080p ultrawide | 1080 by 1920 | 550 | -0.12 | 0.02 | 0 (stratum S19 only) | 12,314 |

By owner decision no 4K frames were used. A 1080 px short side at the 0.9 fill used gives at most $972 / N$ px per module for a symbol $N$ module pitches across: 31.4 for version 1, 15.7 for version 11 and 3.9 for version 31. A version-31 symbol at 6 px per module would need $6 \times 248 = 1{,}488$ px. Dense versions were therefore imaged below the declared minimum and flagged: 289,360 of the 1,000,000 structured cases carry the note `below_declared_min_to_fit`, a further 17,469 had a resolution factor capped to fit, and 28,266 were moved from the 720p frame to the 1080p frame.

### 3.2 Strata and severity bins

The structured design has 20 strata of 50,000 cases. Each stratum varies one primary factor across five fixed severity bins, bin 0 the mildest. The factors and bins are listed in Appendix A. Units are physical or relative to the module pitch. Each stratum is split evenly between the two layouts (25,000 each) and the five bins, giving cells of 5,000 cases per stratum, layout and bin. Each cell contains 500 hard negatives (10%; Section 3.5), 2,200 controlled single-factor sweeps (44%) and 2,300 randomised joint conditions (46%). In a sweep the other conditions are clean; in a joint case they are drawn from realistic distributions (rotation over the full circle, tilt, mild defocus and motion, print or display substrate in 60% of cases, and so on). The versions 1 to 31 and the four levels are cycled so that all densities are covered evenly.

Two strata use classes in place of a continuous factor. In S16 the classes are clutter, clutter with 2 to 5 distractors, clutter with 6 to 12 distractors (classes 2 and 3) and a partial crop of 3% to 25% of the symbol's width out of frame (class 4). In S19 the classes are mild distortion with $k_1$ between -0.05 and 0.05 (class 0); the ultrawide model with $k_1$ between -0.15 and -0.08 and the symbol placed off-centre (class 1); a finder covered over 1% to 3% of the field (class 2); a finder covered over 3% to 6% together with a glare spot (class 3); and ultrawide, finder cover and 30 to 45 degrees of tilt together (class 4).

Note: as written in `generator/strata.py`, the branch of S16 that places a second valid code in the frame cannot be reached, because class 3 is taken by the preceding condition. S16 therefore did not test two valid codes in one frame, although its name includes "multiple codes".

### 3.3 Case generation and seeding

A case is a pure function of the cohort master seed, stratum, layout, bin, mode and index. Every seed is the first 64 bits of SHA-256 over a labelled string (`ckt-seed-v1`). Payloads are shared in families of five consecutive cases, which is the clustering unit used in the statistics. The structured master seed is public. The holdout master seed is secret; only its SHA-256 commitment (`5b1ea0cc...`) is in the lock. Cases are shuffled into shards of 1,000 so that every shard is a representative mix.

### 3.4 Cohorts

Table 5. Cohorts, planned and completed. "Sealed shards" counts the 1,000-case shards recorded in `evidence/manifests/`, each with SHA-256 hashes of its plan, results and traces.

| Cohort | Purpose | Planned | Completed | Sealed shards |
|---|---|---:|---:|---:|
| Pilot | Validate the simulator and instrumentation; not counted | 10,000 | 10,000 | 10 |
| Structured | 20 strata, pre-registered | 1,000,000 | 1,000,000 | 1,000 |
| Adaptive, failure-targeted | Weakest stratum, layout and bin cells from the structured results | 350,000 | 350,000 | 350 |
| Adaptive, cross-condition | The ten worst conditions recombined in pairs | 50,000 | 4,000 | 4 |
| Adaptive, boundary search | Bisection on the primary factor to the pass/fail boundary | 100,000 | 0 | 0 |
| Holdout | Sealed seed, distinct payload families, half the textures from a second rasteriser | 100,000 | 0 | 0 |
| Total completed | | | 1,364,000 | 1,364 |

The structured results were frozen and signed (freeze time 2026-11-09 07:18:10 UTC, results root `53ca368f...`) before any adaptive case was selected. The adaptive weights were hashed into that freeze record. A weakness score was computed for each of 194 cells (the 200 cells less 6 with no case outside the unrecoverable class) as a weighted sum of the no-read rate (weight 1.0), any wrong payload (100), 95th-percentile latency excess over 200 ms (0.2), median localisation error (0.2), Wilson interval width (0.5; Section 5.2) and the mean fraction of correction capacity used by successes (0.3). The failure-targeted pool allots at least 5,000 cases to each selected cell.

Note: the campaign analysis describes the failure-targeted pool as covering the "194 weakest" cells. The allocation rule in `analysis/adaptive/select.py` selects at most $350{,}000 / 5{,}000 = 70$ cells, the 70 highest scores. The per-cell allocation is not in the retained evidence, so this report cannot confirm either number and treats 194 as the number of cells scored.

### 3.5 Hard negatives

Table 6. Hard-negative kinds and the outcome each requires. REJECT means the reader must not return any payload. NO_WRONG means it may decline or may return a payload the construction allows (the true payload or, for a splice, the payload of either of the two symbols joined), but must never return any other.

| Kind | Construction | Required outcome |
|---|---|---|
| `random_codeword` | Random codeword bytes on valid geometry | REJECT |
| `foreign_grid` | A QR-like grid drawn on the same canvas in place of the symbol | REJECT |
| `blank` | No symbol in the scene | REJECT |
| `heavy_corruption` | Each codeword byte altered with probability 0.50 to 0.75 | NO_WRONG |
| `t_plus_one` | One more byte error in block 0 than its parity corrects | NO_WRONG |
| `crc_forge` | One payload byte altered and all parity recomputed, so every block is a valid RS codeword and the CRC-32C is stale | NO_WRONG |
| `splice` | Two symbols joined, 30% to 70% of the canvas from the second | NO_WRONG |
| `mirror` | The symbol reflected | NO_WRONG |

### 3.6 Independent Reed-Solomon oracle

The harness contains its own RS encoder (`runners/native/symbol_factory.h`) written independently of the production code. It recomputes the parity of each symbol and compares it with the production encoder's, and it constructs the `t_plus_one` and `crc_forge` negatives. A second oracle compares the harness rasteriser with the production repository's own rasteriser on stratum S01.

### 3.7 Recoverability classifier

So that physically impossible frames are not counted as reader failures, each valid frame is assigned a class by `Simulator::classify` in `simulation/pipeline.h` (version `recov-v2`). The inputs are computed from the case, not from the reader:

* $m_{\min}$, the worst local px per module over a 5 by 5 grid of field points, taken as the smallest singular value of the projection Jacobian times the module pitch;
* the modulation transfer function (MTF) at the module fundamental, $f = 0.5 / m_{\min}$ cycles per pixel, as the product of the Gaussian term $\exp(-2\pi^2\sigma^2 f^2)$, the pixel-aperture and motion sinc terms and, for disc defocus, the term $2J_1(z)/z$;
* the detectability $d' = \Delta S \cdot \text{MTF} / \sqrt{\bar{S} + \sigma_r^2}$, where $\Delta S$ is the mark-to-background signal difference in electrons, $\bar{S}$ the mean signal and $\sigma_r$ the read noise;
* the site-loss fraction $a$, the occluded field fraction plus the larger of the saturated and glare-washed fractions, and the implied byte-error estimate $b = 1 - (1 - a)^8$;
* $t$, the fraction of a block the level corrects without erasures (Table 3).

The rule is as follows. A frame is UNRECOVERABLE if any field corner lies within one module of the frame edge (cropped), or $m_{\min} \lt 1$, or MTF times contrast is below 0.01, or $d' \lt 1$, or $b \gt 2t$. Otherwise it is MARGINAL if $m_{\min} \lt 2$, or $d' \lt 3$, or $b \gt t$. Otherwise it is RECOVERABLE. Negatives that must be rejected are not classified, and the classes of the other negatives are not used: every rate by class in this report is for valid cases.

The classifier is an engineering estimate with two properties that affect interpretation. First, it tests modulation at the module pitch, not at the scale of the finest mark; Section 6.3 argues that this matters. Second, its labels can err in both directions: the reader decoded 102,786 of the 353,755 structured cases labelled UNRECOVERABLE (29.06%), so that label does not mean physically impossible, and the simulator's documentation states that the labels do not prove a case recoverable.

### 3.8 Ground-truth-corner diagnostic

On every failure the harness calls the production decoder once more with the true field corners in place of the reader's own. If that decode returns the correct payload, the failure is labelled localisation-limited: the marks were readable and the reader's border search or corner fit lost them. Otherwise it is labelled sampling-limited (sampling or decoding). The diagnostic never changes a case's outcome. Each failure also carries the reader's own reason: `NO_BORDER` (no border candidate returned), `BORDER_MISS` (candidates returned, none matching the true symbol) or `UNCORRECTABLE` (the symbol was found and sampled, and RS and CRC did not pass). The two labels are independent axes; a case can be `UNCORRECTABLE` and localisation-limited when the fitted corners are off by a module or more.

### 3.9 Reproducibility

The lock records the SHA-256 of the five design configuration files, of the specifications, of the worker binary (`39cee7da...`) and of the reader's Swift reference files, with an Ed25519 signature. Execution settings in `configs/campaign.yaml`, which include the per-case timeout, are outside the lock. Every sealed shard records hashes of its plan, results and traces and the worker hash; all 1,364 shards name the same worker and host. Sealed data are stored content-addressed in object storage (4,128 objects indexed in `evidence/r2_index.jsonl`). Frames are not stored. Each frame regenerates bit for bit from its case line on the same platform, and each result records the frame's SHA-256. The run used one 8-core x86-64 Linux host with GCC 15.2.0 in Release mode. Commands are in Appendix C.

### 3.10 Threats to validity

1. **The simulation is not validated against real captures.** The mandate requires comparison with a real capture corpus before any realism claim. No corpus exists (open issue OI-2). Camera models are generic. Diffraction, depth-of-field variation across a tilted symbol, multi-frame ISP processing and chroma subsampling are not modelled.
2. **Cohorts are incomplete.** 354,000 of 500,000 adaptive cases completed. The boundary search and the holdout did not run. No independent replication of any result exists.
3. **The run stalled.** No shard sealed after 16:28:29 UTC on 9 November. The cause is not established (Section 6.7).
4. **Memory was measured for reader and simulator together**, in one process.
5. **Device timing was not measured.** All times are host times on a server.
6. **Recoverability labels are estimates** (Section 3.7), and rates "of recoverable" inherit their error.
7. **The renderer cross-check is unexplained.** The cross-check figure is the fraction of non-edge pixels whose binarised value differs between rasterisations of the clean symbol (the harness rasteriser, the production repository's test rasteriser and, for Profile 0, the production raster). The pilot gate allows a maximum of 0.01, and the pilot recorded 0.0 over 500 cases. The structured run recorded a maximum of 0.463619 and a 99th percentile of 0.4076 over 49,380 S01 cases. The frozen report prints the figure without comment, and no source read for this report explains it. Until it is explained, agreement between the rasterisers on the structured cohort is not shown.
8. **Cases are not independent.** Payload families and shared conditions correlate outcomes; the campaign estimates a design effect (the ratio of the variance under clustering to the variance under independence) of 2.91 for the overall decode rate.
9. **Adaptive cases are selected to be hard.** Their rates are conditional on that selection and are not estimates of field performance.

## 4. Results

### 4.1 Overall and per cohort

No wrong payload was accepted in any of the 1,364,000 completed cases, and no reader crash or timeout was recorded (`reader_faults` is 0 in all 1,364 shard records).

In the structured cohort the reader decoded 451,282 of 900,000 valid symbols (50.14%, 95% Wilson interval 50.04% to 50.25%) and 296,526 of the 430,176 valid symbols classed RECOVERABLE (68.93%, 68.79% to 69.07%). The remaining 133,650 recoverable frames, 31.07%, were not read.

Table 7. Results per cohort. "Expected outcome met" is the count the shard manifests record: a valid symbol decoded to the right payload, or a negative handled as required. For the structured cohort and the pilot the valid-symbol decode count is also available from the reports.

| Cohort | Completed | Expected outcome met | Valid symbols decoded | Wrong payloads | Reader faults |
|---|---:|---:|---:|---:|---:|
| Pilot | 10,000 | 5,486 (54.86%) | 4,486 of 9,000 (49.84%) | 0 | 0 |
| Structured | 1,000,000 | 551,282 (55.13%) | 451,282 of 900,000 (50.14%) | 0 | 0 |
| Adaptive, failure-targeted | 350,000 | 77,881 (22.25%) | not reported separately | 0 | 0 |
| Adaptive, cross-condition | 4,000 | 1 (0.025%) | not reported separately | 0 | 0 |

Note: the campaign analysis lists the pilot as "54.86% decoded". That figure is the expected-outcome count, which includes the 1,000 pilot negatives. The pilot report gives the valid-symbol decode rate as 49.84%, and this report uses that. The structured difference, $551{,}282 - 451{,}282 = 100{,}000$, is exactly the number of hard negatives, all of which met their required outcome. No adaptive report was produced, so adaptive rates are available only as manifest counts.

Table 8. Structured valid cases by recoverability class.

| Class | Cases | Share of 900,000 | Decoded | Rate |
|---|---:|---:|---:|---:|
| RECOVERABLE | 430,176 | 47.80% | 296,526 | 68.93% |
| MARGINAL | 116,069 | 12.90% | 51,970 | 44.78% |
| UNRECOVERABLE | 353,755 | 39.31% | 102,786 | 29.06% |

### 4.2 Per stratum

Table 9. Structured results per stratum: valid cases decoded (45,000 valid cases per stratum), recoverable cases and recoverable decoded with the 95% Wilson interval, the most frequent failure reason among all valid failures, and the split of all valid failures by the ground-truth-corner diagnostic.

| Stratum | Valid decoded (of 45,000) | Rate | Recoverable cases | Recoverable decoded | Rate (95% Wilson interval) | Dominant failure reason | Localisation-limited | Sampling-limited |
|---|---:|---:|---:|---:|---|---|---:|---:|
| S01 Clean reference | 40,437 | 89.86% | 34,136 | 33,715 | 98.77% [98.64, 98.88] | `BORDER_MISS` 2,506 | 215 | 4,348 |
| S02 Payload patterns | 36,299 | 80.66% | 25,241 | 23,884 | 94.62% [94.34, 94.90] | `UNCORRECTABLE` 4,836 | 1,919 | 6,782 |
| S03 Scale and sampling | 25,575 | 56.83% | 24,019 | 18,439 | 76.77% [76.23, 77.30] | `UNCORRECTABLE` 12,912 | 4,123 | 15,302 |
| S04 Perspective and bends | 16,248 | 36.11% | 21,095 | 10,708 | 50.76% [50.09, 51.44] | `BORDER_MISS` 12,971 | 8,797 | 19,955 |
| S05 Rotation and mirrors | 36,149 | 80.33% | 25,038 | 23,726 | 94.76% [94.48, 95.03] | `UNCORRECTABLE` 4,572 | 1,830 | 7,021 |
| S06 Defocus | 13,486 | 29.97% | 28,401 | 10,524 | 37.06% [36.50, 37.62] | `UNCORRECTABLE` 22,359 | 2,660 | 28,854 |
| S07 Motion blur | 14,708 | 32.68% | 28,492 | 10,958 | 38.46% [37.90, 39.03] | `UNCORRECTABLE` 21,326 | 3,655 | 26,637 |
| S08 Low light | 29,653 | 65.90% | 9,364 | 8,575 | 91.57% [90.99, 92.12] | `UNCORRECTABLE` 9,036 | 4,879 | 10,468 |
| S09 Contrast and shadows | 9,791 | 21.76% | 35,330 | 9,472 | 26.81% [26.35, 27.27] | `NO_BORDER` 16,402 | 12,604 | 22,605 |
| S10 Glare | 2,828 | 6.28% | 2,071 | 1,716 | 82.86% [81.17, 84.42] | `UNCORRECTABLE` 29,813 | 237 | 41,935 |
| S11 Occlusion | 8,380 | 18.62% | 2,003 | 1,668 | 83.28% [81.58, 84.85] | `UNCORRECTABLE` 25,892 | 1,793 | 34,827 |
| S12 Print | 20,880 | 46.40% | 26,289 | 15,585 | 59.28% [58.69, 59.88] | `UNCORRECTABLE` 18,467 | 4,158 | 19,962 |
| S13 Screens | 22,213 | 49.36% | 24,254 | 16,959 | 69.92% [69.34, 70.50] | `UNCORRECTABLE` 15,387 | 4,942 | 17,845 |
| S14 Compression and ISP | 33,798 | 75.11% | 12,906 | 11,564 | 89.60% [89.06, 90.12] | `UNCORRECTABLE` 7,226 | 2,511 | 8,691 |
| S15 Colour | 21,322 | 47.38% | 34,659 | 18,912 | 54.57% [54.04, 55.09] | `NO_BORDER` 9,815 | 12,561 | 11,117 |
| S16 Clutter and crops | 29,300 | 65.11% | 20,661 | 19,540 | 94.57% [94.26, 94.87] | `BORDER_MISS` 9,368 | 1,696 | 14,004 |
| S17 Ring stress | 30,771 | 68.38% | 28,526 | 23,755 | 83.27% [82.84, 83.70] | `UNCORRECTABLE` 7,974 | 5,093 | 9,136 |
| S18 Ray stress | 18,208 | 40.46% | 19,475 | 12,826 | 65.86% [65.19, 66.52] | `UNCORRECTABLE` 17,320 | 5,096 | 21,696 |
| S19 Lens and finder stress | 12,711 | 28.25% | 9,165 | 6,856 | 74.81% [73.91, 75.68] | `UNCORRECTABLE` 13,490 | 797 | 31,492 |
| S20 Combined conditions | 28,525 | 63.39% | 19,051 | 17,144 | 89.99% [89.56, 90.41] | `UNCORRECTABLE` 8,889 | 2,741 | 13,734 |
| All strata | 451,282 of 900,000 | 50.14% | 430,176 | 296,526 | 68.93% [68.79, 69.07] | `UNCORRECTABLE` 248,478 | 82,307 | 366,411 |

Across all strata there were 448,718 valid failures: 248,478 `UNCORRECTABLE` (55.4%), 127,539 `BORDER_MISS` (28.4%) and 72,701 `NO_BORDER` (16.2%). By the diagnostic, 82,307 (18.3%) were localisation-limited and 366,411 (81.7%) sampling-limited.

The weakest stratum on recoverable frames is S09 (contrast and shadows) at 26.81%, followed by S06 (defocus) at 37.06%, S07 (motion blur) at 38.46%, S04 (perspective and bends) at 50.76% and S15 (colour) at 54.57%. The strongest is S01 (clean) at 98.77%, followed by S05 (rotation) at 94.76%, S02 (payload patterns) at 94.62% and S16 (clutter and crops) at 94.57%. S15 is the only stratum in which localisation-limited failures (12,561) outnumber sampling-limited ones (11,117). In S09 the dominant reason is `NO_BORDER`: the reader returned no candidate at all in 16,402 cases.

Strata S10 (glare) and S11 (occlusion) have few recoverable cases, 2,071 and 2,003, because most of their cases exceed the classifier's damage budget. Their recoverable rates (82.86% and 83.28%) describe only the mild end. Their all-valid rates, 6.28% and 18.62%, describe the whole stratum.

### 4.3 Severity

Table 10. Valid cases decoded per stratum and severity bin, of 9,000 valid cases per cell, all recoverability classes together.

| Stratum | Bin 0 | Bin 1 | Bin 2 | Bin 3 | Bin 4 |
|---|---:|---:|---:|---:|---:|
| S01 Clean reference | 8,123 (90.3%) | 8,115 (90.2%) | 8,089 (89.9%) | 8,068 (89.6%) | 8,042 (89.4%) |
| S02 Payload patterns | 7,262 (80.7%) | 7,238 (80.4%) | 7,250 (80.6%) | 7,225 (80.3%) | 7,324 (81.4%) |
| S03 Scale and sampling | 7,678 (85.3%) | 7,362 (81.8%) | 6,148 (68.3%) | 3,446 (38.3%) | 941 (10.5%) |
| S04 Perspective and bends | 5,236 (58.2%) | 5,286 (58.7%) | 4,488 (49.9%) | 1,107 (12.3%) | 131 (1.5%) |
| S05 Rotation and mirrors | 7,286 (81.0%) | 7,199 (80.0%) | 7,205 (80.1%) | 7,167 (79.6%) | 7,292 (81.0%) |
| S06 Defocus | 7,236 (80.4%) | 4,758 (52.9%) | 1,422 (15.8%) | 70 (0.8%) | 0 (0.0%) |
| S07 Motion blur | 7,031 (78.1%) | 5,009 (55.7%) | 2,194 (24.4%) | 469 (5.2%) | 5 (0.1%) |
| S08 Low light | 7,257 (80.6%) | 7,233 (80.4%) | 7,004 (77.8%) | 5,743 (63.8%) | 2,416 (26.8%) |
| S09 Contrast and shadows | 4,661 (51.8%) | 2,770 (30.8%) | 1,532 (17.0%) | 748 (8.3%) | 80 (0.9%) |
| S10 Glare | 2,738 (30.4%) | 89 (1.0%) | 1 (0.0%) | 0 (0.0%) | 0 (0.0%) |
| S11 Occlusion | 5,152 (57.2%) | 2,060 (22.9%) | 589 (6.5%) | 324 (3.6%) | 255 (2.8%) |
| S12 Print | 6,147 (68.3%) | 6,098 (67.8%) | 4,917 (54.6%) | 2,744 (30.5%) | 974 (10.8%) |
| S13 Screens | 6,437 (71.5%) | 6,371 (70.8%) | 5,216 (58.0%) | 3,116 (34.6%) | 1,073 (11.9%) |
| S14 Compression and ISP | 6,983 (77.6%) | 6,895 (76.6%) | 6,793 (75.5%) | 6,725 (74.7%) | 6,402 (71.1%) |
| S15 Colour | 6,744 (74.9%) | 5,710 (63.4%) | 4,394 (48.8%) | 2,979 (33.1%) | 1,495 (16.6%) |
| S16 Clutter and crops | 7,241 (80.5%) | 7,278 (80.9%) | 7,293 (81.0%) | 7,284 (80.9%) | 204 (2.3%) |
| S17 Ring stress | 6,735 (74.8%) | 6,701 (74.5%) | 6,621 (73.6%) | 6,310 (70.1%) | 4,404 (48.9%) |
| S18 Ray stress | 6,591 (73.2%) | 6,108 (67.9%) | 4,176 (46.4%) | 1,322 (14.7%) | 11 (0.1%) |
| S19 Lens and finder stress | 7,123 (79.1%) | 95 (1.1%) | 5,470 (60.8%) | 13 (0.1%) | 10 (0.1%) |
| S20 Combined conditions | 6,449 (71.7%) | 6,273 (69.7%) | 5,858 (65.1%) | 5,312 (59.0%) | 4,633 (51.5%) |

Three patterns are visible.

**Flat strata.** S01, S02, S05 and S14 vary by less than 7 points across bins. Rotation by any angle, payload pattern and JPEG quality down to 10 are not what limits the reader. The roughly 80% ceiling common to bin 0 of most strata is consistent with the base rate of the joint conditions and of dense versions imaged below 6 px per module, not with an effect of the stratum's factor; the frozen report does not tabulate that base rate separately.

**Slopes.** S03, S06, S07, S08, S09, S12, S13, S15 and S18 decline steadily. Defocus falls from 80.4% to 52.9% to 15.8% over blur of 0 to 0.15, 0.15 to 0.3 and 0.3 to 0.45 module pitches, and reaches 0 of 9,000 in the last bin. Motion falls from 78.1% to 24.4% by 1.0 to 1.5 pitches of travel.

**Cliffs.** Four results are steps, not slopes:

* S10 glare: 30.4% with up to 5% of the symbol under glare, 1.0% at 5% to 10%, and 1 of 27,000 beyond 10%.
* S11 occlusion: 57.2% below 5% of the field, 22.9% at 5% to 10%, 6.5% at 10% to 15%.
* S16: 80.5% to 81.0% for every class of clutter and distractors, and 2.3% (204 of 9,000) when 3% to 25% of the symbol is out of frame.
* S19: 79.1% with mild distortion; 1.1% (95 of 9,000) on the off-centre ultrawide class; 60.8% with a finder covered over 1% to 3%; 0.1% (13 of 9,000) with a finder covered over 3% to 6% and a glare spot; 0.1% for the combined class.

S04 has both shapes: 58.2%, 58.7% and 49.9% up to 45 degrees of tilt, then 12.3% at 45 to 60 degrees and 1.5% at 60 to 72 degrees.

### 4.4 Layout, density and level

The print layout decoded 242,141 of 450,000 valid symbols (53.81%) and the standard layout 209,141 of 450,000 (46.48%), a difference of 7.33 points. The print layout is ahead in 19 of the 20 strata. The exception is S11, where the standard layout is 0.47 points ahead.

Table 11. Valid cases decoded per stratum and layout. Differences are computed from unrounded rates.

| Stratum | Standard layout decoded (of 22,500) | Rate | Print layout decoded (of 22,500) | Rate | Difference (points) |
|---|---:|---:|---:|---:|---:|
| S01 | 20,143 | 89.52% | 20,294 | 90.20% | +0.67 |
| S02 | 17,722 | 78.76% | 18,577 | 82.56% | +3.80 |
| S03 | 10,554 | 46.91% | 15,021 | 66.76% | +19.85 |
| S04 | 7,742 | 34.41% | 8,506 | 37.80% | +3.40 |
| S05 | 17,565 | 78.07% | 18,584 | 82.60% | +4.53 |
| S06 | 4,891 | 21.74% | 8,595 | 38.20% | +16.46 |
| S07 | 5,837 | 25.94% | 8,871 | 39.43% | +13.48 |
| S08 | 13,991 | 62.18% | 15,662 | 69.61% | +7.43 |
| S09 | 4,808 | 21.37% | 4,983 | 22.15% | +0.78 |
| S10 | 1,284 | 5.71% | 1,544 | 6.86% | +1.16 |
| S11 | 4,243 | 18.86% | 4,137 | 18.39% | -0.47 |
| S12 | 8,244 | 36.64% | 12,636 | 56.16% | +19.52 |
| S13 | 8,677 | 38.56% | 13,536 | 60.16% | +21.60 |
| S14 | 15,932 | 70.81% | 17,866 | 79.40% | +8.60 |
| S15 | 10,099 | 44.88% | 11,223 | 49.88% | +5.00 |
| S16 | 14,299 | 63.55% | 15,001 | 66.67% | +3.12 |
| S17 | 14,551 | 64.67% | 16,220 | 72.09% | +7.42 |
| S18 | 8,987 | 39.94% | 9,221 | 40.98% | +1.04 |
| S19 | 6,312 | 28.05% | 6,399 | 28.44% | +0.39 |
| S20 | 13,260 | 58.93% | 15,265 | 67.84% | +8.91 |
| All strata (of 450,000) | 209,141 | 46.48% | 242,141 | 53.81% | +7.33 |

The difference is concentrated where marks are small relative to the optics: S13 screens (+21.60 points), S03 scale (+19.85), S12 print (+19.52), S06 defocus (+16.46) and S07 motion (+13.48). It is under 1 point in S01, S09 and S19, whose factors (clean capture, contrast, and lens distortion with finder cover) do not act mainly through the size of the marks.

Table 12. Valid cases decoded by severity bin and layout, of 4,500 valid cases per cell, for the strata most sensitive to mark size.

| Stratum and layout | Bin 0 | Bin 1 | Bin 2 | Bin 3 | Bin 4 |
|---|---:|---:|---:|---:|---:|
| S03 standard | 3,709 (82.4%) | 3,479 (77.3%) | 2,565 (57.0%) | 791 (17.6%) | 10 (0.2%) |
| S03 print | 3,969 (88.2%) | 3,883 (86.3%) | 3,583 (79.6%) | 2,655 (59.0%) | 931 (20.7%) |
| S06 standard | 3,494 (77.6%) | 1,380 (30.7%) | 17 (0.4%) | 0 (0.0%) | 0 (0.0%) |
| S06 print | 3,742 (83.2%) | 3,378 (75.1%) | 1,405 (31.2%) | 70 (1.6%) | 0 (0.0%) |
| S07 standard | 3,412 (75.8%) | 1,814 (40.3%) | 491 (10.9%) | 120 (2.7%) | 0 (0.0%) |
| S07 print | 3,619 (80.4%) | 3,195 (71.0%) | 1,703 (37.8%) | 349 (7.8%) | 5 (0.1%) |
| S12 standard | 2,974 (66.1%) | 2,878 (64.0%) | 1,839 (40.9%) | 549 (12.2%) | 4 (0.1%) |
| S12 print | 3,173 (70.5%) | 3,220 (71.6%) | 3,078 (68.4%) | 2,195 (48.8%) | 970 (21.6%) |
| S13 standard | 2,971 (66.0%) | 2,919 (64.9%) | 2,024 (45.0%) | 722 (16.0%) | 41 (0.9%) |
| S13 print | 3,466 (77.0%) | 3,452 (76.7%) | 3,192 (70.9%) | 2,394 (53.2%) | 1,032 (22.9%) |
| S18 standard | 3,190 (70.9%) | 3,065 (68.1%) | 2,108 (46.8%) | 621 (13.8%) | 3 (0.1%) |
| S18 print | 3,401 (75.6%) | 3,043 (67.6%) | 2,068 (46.0%) | 701 (15.6%) | 8 (0.2%) |

In S06 bin 1 (blur of 0.15 to 0.3 pitch) the standard layout reads 30.7% and the print layout 75.1%. In bin 2 (0.3 to 0.45 pitch) the figures are 0.4% and 31.2%. The print layout tolerates roughly one bin more of blur, ink spread, coarse display pixels and reduced resolution. S18 (ray stress) shows no such gap, which is expected because its factor is expressed in pixels per ray slot and the print layout's wider slot pitch is already built into the bin.

Table 13. Valid cases decoded by density band (versions) and layout, and by error-correction level and layout.

| Group | Standard layout | Rate | Print layout | Rate |
|---|---:|---:|---:|---:|
| Versions 1 to 8 | 60,341 of 116,100 | 51.97% | 63,060 of 113,460 | 55.58% |
| Versions 9 to 16 | 60,536 of 116,140 | 52.12% | 67,278 of 117,040 | 57.48% |
| Versions 17 to 24 | 54,762 of 116,100 | 47.17% | 63,973 of 117,180 | 54.59% |
| Versions 25 to 31 | 33,502 of 101,660 | 32.95% | 47,830 of 102,320 | 46.75% |
| Level L | 46,335 of 112,500 | 41.19% | 57,184 of 113,420 | 50.42% |
| Level M | 51,370 of 112,500 | 45.66% | 60,069 of 113,380 | 52.98% |
| Level Q | 54,403 of 112,500 | 48.36% | 62,569 of 113,560 | 55.10% |
| Level H | 57,033 of 112,500 | 50.70% | 62,319 of 109,640 | 56.84% |

Success is roughly level from version 1 to 16 and lowest in the densest band, where a 1080 px frame cannot supply 6 px per module. Raising the level from L to H multiplies the correctable error fraction by 4.3 (7.1% to 30.2%) and raises the decode rate by 9.51 points in the standard layout and 6.42 points in the print layout. Section 6.6 discusses why the gain is small.

### 4.5 Device and substrate

Table 14. Valid cases decoded by camera model and by substrate. Device and substrate are confounded with stratum (the ultrawide model appears only in S19 classes 1 and 4), so these are descriptive, not controlled comparisons.

| Group | Decoded | Valid cases | Rate | 95% Wilson interval |
|---|---:|---:|---:|---|
| 1080p telephoto | 36,023 | 64,317 | 56.01% | 55.62% to 56.39% |
| 720p | 39,645 | 72,102 | 54.98% | 54.62% to 55.35% |
| Budget 1080p | 67,351 | 128,784 | 52.30% | 52.02% to 52.57% |
| 1080p wide | 308,173 | 623,717 | 49.41% | 49.29% to 49.53% |
| 1080p ultrawide | 90 | 11,080 | 0.81% | 0.66% to 1.00% |
| Ideal substrate | 310,433 | 559,458 | 55.49% | 55.36% to 55.62% |
| Display | 48,000 | 105,810 | 45.36% | 45.06% to 45.66% |
| Print | 92,849 | 234,732 | 39.56% | 39.36% to 39.75% |

The ultrawide model read 90 of 11,080 valid cases.

### 4.6 Integrity

Table 15. Hard negatives in the structured cohort, both layouts together. "Returned an allowed payload" applies only to kinds whose required outcome is NO_WRONG; it is the number of cases not rejected, none of which returned a wrong payload.

| Kind | Cases | Rejected | Returned an allowed payload | Wrong payload |
|---|---:|---:|---:|---:|
| `random_codeword` | 12,330 | 12,330 | not permitted | 0 |
| `foreign_grid` | 12,130 | 12,130 | not permitted | 0 |
| `blank` | 12,130 | 12,130 | not permitted | 0 |
| `heavy_corruption` | 12,330 | 12,330 | 0 | 0 |
| `t_plus_one` | 12,330 | 12,330 | 0 | 0 |
| `crc_forge` | 12,330 | 12,330 | 0 | 0 |
| `splice` | 12,140 | 11,803 | 337 | 0 |
| `mirror` | 14,280 | 8,067 | 6,213 | 0 |
| Total | 100,000 | 93,450 | 6,550 | 0 |

All 36,590 negatives that had to be rejected were rejected. The 337 splices that decoded returned the payload of one of their two symbols. The 6,213 mirrored symbols that decoded returned the true payload; whether a mirrored symbol must be rejected is unspecified (open issue OI-4). The independent RS oracle agreed with production parity on all 1,000,000 structured cases and all 10,000 pilot cases, with 0 mismatches.

### 4.7 Time and memory on the host

For 1080-px-wide frames the host decode time per frame had a median of 50.1 ms, a 95th percentile of 227.6 ms and a maximum of 549.4 ms in the standard layout (427,056 frames), and 46.4 ms, 214.6 ms and 622.3 ms in the print layout (426,712 frames). For the 14,669 temporal sequences that succeeded, the time to first success had a median of 50.0 ms and a 95th percentile of 453.3 ms. These are server times. They show that the 95th percentile already exceeds 200 ms on the host, and they do not measure gate G4.

Peak heap per case for the worker process, which holds both the simulator and the reader, had a median of 50.1 MB, a 99th percentile of 476.9 MB and a maximum of 550.8 MB.

## 5. Statistical treatment

### 5.1 Upper bound on the wrong-payload rate

Let $p$ be the probability that one trial yields an accepted wrong payload, and assume $n$ independent trials with the same $p$. The probability of observing no event is $(1-p)^n$. The one-sided upper confidence bound $p_U$ at confidence $1 - \alpha$ is the largest $p$ under which observing zero events is still not rejected at level $\alpha$:

$$(1 - p_U)^n = \alpha \quad\Longrightarrow\quad p_U = 1 - \alpha^{1/n}.$$

Because $\alpha^{1/n} = \exp(\ln(\alpha)/n)$ and $\exp(x) \approx 1 + x$ for small $x$,

$$p_U \approx \frac{-\ln \alpha}{n}, \qquad -\ln 0.05 = 2.9957, \qquad -\ln 0.01 = 4.6052.$$

This is the "rule of three": about $3/n$ at 95% confidence.

Table 16. One-sided upper bounds on the wrong-payload rate from zero observed events, computed as $1 - \alpha^{1/n}$.

| Set of trials | n | 95% bound | 99% bound |
|---|---:|---:|---:|
| Structured, all | 1,000,000 | 3.00 per million | 4.61 per million |
| Structured, standard layout | 500,000 | 5.99 per million | 9.21 per million |
| Structured, print layout | 500,000 | 5.99 per million | 9.21 per million |
| Structured hard negatives | 100,000 | 29.96 per million | 46.05 per million |
| Adaptive, completed | 354,000 | 8.46 per million | 13.01 per million |
| Structured and adaptive (the G1 cohorts that ran) | 1,354,000 | 2.21 per million | 3.40 per million |
| All completed cases, including the pilot | 1,364,000 | 2.20 per million | 3.38 per million |

The structured figures agree with the frozen report (2.9957e-06 overall, 5.9914e-06 per layout).

Three qualifications apply.

* **Independence.** Cases share payloads in families of five and share conditions within cells. If the design effect estimated for the decode rate (2.9089 before rounding) applied to wrong payloads as well, the effective structured sample would be $1{,}000{,}000 / 2.9089 = 343{,}778$ and the 95% bound would be 8.71 per million. The design effect was estimated for a different outcome, so this is an illustration of sensitivity, not a corrected bound.
* **Mixture.** The pooled figures bound the rate for the mixture of conditions the campaign generated, in which 44% of the structured cases are single-factor sweeps, 10% are negatives and the adaptive cases are deliberately hard. They do not bound the rate for any other mixture, including field use.
* **Exposure.** A wrong payload requires a frame that passes RS correction and then CRC-32C with different content. A frame the reader cannot locate offers no such opportunity. Of the structured cases, 451,282 valid cases decoded correctly, 6,550 negatives returned an allowed payload, and 248,478 valid failures are recorded as `UNCORRECTABLE`, meaning the symbol was located and sampled and the check refused the result. The frozen report does not record how many of the 93,450 rejected negatives were located and reached the RS and CRC check, so the number of negatives that exercised the acceptance rule is not known.

**What zero observed wrong payloads shows and does not show.** It shows that, for build 85, in these 1,364,000 simulated cases, the reader, whose acceptance rule is RS correction followed by CRC-32C, never returned a payload other than an allowed one, including in the 100,000 structured hard-negative cases. It supports, at 95% confidence and under the independence assumption, a rate below 3.0 per million structured-design trials. It does not prove that a wrong payload is impossible, it says nothing about frames from real cameras, and it does not transfer to build 86, whose decoder makes more attempts per frame (several reading strategies, two placements and erasure decoding) and therefore presents more candidate codewords to the same check. The mandate states the same limit: zero is a release gate, not proof of impossibility.

### 5.2 Confidence intervals for read rates

For $k$ successes in $n$ trials with $\hat{p} = k/n$ and $z = 1.96$, the Wilson score interval is

$$\frac{\hat{p} + \dfrac{z^2}{2n} \pm z \sqrt{\dfrac{\hat{p}(1-\hat{p})}{n} + \dfrac{z^2}{4n^2}}}{1 + \dfrac{z^2}{n}}.$$

It is preferred to the simple $\hat{p} \pm z\sqrt{\hat{p}(1-\hat{p})/n}$ because it stays inside 0 to 1 and behaves correctly near 0 and 1, which matters for cells such as 0 of 9,000.

Table 17. Wilson 95% intervals for the headline rates and for the weakest and strongest strata. The values were recomputed for this report and agree with the frozen JSON.

| Quantity | k | n | Rate | 95% interval |
|---|---:|---:|---:|---|
| All valid, structured | 451,282 | 900,000 | 50.14% | 50.04% to 50.25% |
| All valid, with design effect 2.91 (effective n 309,400) | | | 50.14% | 49.97% to 50.32% |
| Recoverable, structured | 296,526 | 430,176 | 68.93% | 68.79% to 69.07% |
| Standard layout, valid | 209,141 | 450,000 | 46.48% | 46.33% to 46.62% |
| Print layout, valid | 242,141 | 450,000 | 53.81% | 53.66% to 53.95% |
| S01 clean, recoverable (strongest) | 33,715 | 34,136 | 98.77% | 98.64% to 98.88% |
| S09 contrast, recoverable (weakest) | 9,472 | 35,330 | 26.81% | 26.35% to 27.27% |
| S06 defocus, recoverable | 10,524 | 28,401 | 37.06% | 36.50% to 37.62% |
| S06 bin 4, valid | 0 | 9,000 | 0.00% | 0.00% to 0.04% |
| Ultrawide model, valid | 90 | 11,080 | 0.81% | 0.66% to 1.00% |

The intervals are narrow because the samples are large. Sampling error is not the main uncertainty in this campaign. The main uncertainties are whether the simulator represents real capture and whether the recoverability labels are right, and no interval expresses those.

The S01 result bears directly on gate G2. With 421 failures in 34,136 recoverable clean cases, the upper end of the interval, 98.88%, is below 100%. The recoverable class includes S01 cases imaged below 6 px per module, so 98.77% is not the G2 statistic. G2 requires 100% and is failed by any single miss at or above 6 px per module; Section 6.3 identifies three such cases.

## 6. Analysis of failure mechanisms

### 6.1 Whole-frame thresholding

Evidence: S09 reads 26.81% of recoverable frames and S15 54.57%; `NO_BORDER` is the dominant reason in both; 12,604 and 12,561 of their failures decode when the true corners are supplied. Even the mildest S09 bin, Michelson contrast 0.6 to 0.9, reads 51.8%.

Derivation. Let the frame contain a symbol that covers a fraction $\phi$ of the pixels, with background grey level $B$ and marks at $M \lt B$ (dark marks), and surroundings at level $E$ covering the rest. The search threshold is $T = L_{10} + 0.32 (L_{90} - L_{10})$. The symbol separates only if $M \lt T \lt B$.

Suppose the surroundings are darker than the marks, $E \lt M$, and dominate the frame, so that more than 90% of sampled pixels are surroundings ($\phi \lt 0.1$). Then both percentiles fall in the surroundings: $L_{10} \approx L_{90} \approx E$. The spread is under 32 levels, and the fixed fallback $T = 92$ is used. A symbol whose marks are lighter than 92, or whose background is darker than 92, does not separate. If instead the symbol's background and the surroundings each cover more than 10% of the frame, $L_{10} \approx E$ and $L_{90} \approx B$, so $T = E + 0.32(B - E)$, and the marks separate only if

$$M \lt E + 0.32 (B - E) \quad\Longleftrightarrow\quad \frac{B - M}{B - E} \gt 0.68.$$

The mark-to-background step must exceed 68% of the background-to-surroundings step. For a dark surround ($E = 20$), a light background ($B = 235$) and marks at grey level $M = 101$ (a Michelson contrast, $(B - M)/(B + M)$, of 0.4 in grey levels), the ratio is $134/215 = 0.62$, below 0.68: the marks are classified as background, the frame stroke vanishes, and no border component forms. The same algebra applies with signs reversed to a light symbol on a brighter surround, and to shadows and gradients, which move $B$ and $M$ across the fixed $T$ within one symbol. A coloured pair of low luminance contrast is the same case with a smaller $B - M$.

This is a property of where the reference is taken, not of the symbol, and it is the mechanism reported for screens in dark and bright rooms in Open CKey Findings OCF-2026-000001. A threshold computed from the neighbourhood of each pixel has no such dependence.

### 6.2 Point-sampled reduction and aliasing

Evidence: the build-85 border search reads one pixel per block (Section 2.2), and the build-86 changelog names this as the cause of moiré bands on screens. S13 (screens) reads 69.92% of recoverable frames, and the standard layout drops from 64.9% to 45.0% to 16.0% over bins 1 to 3 (Table 12). The campaign does not isolate this mechanism. Only 4,942 of the 22,787 S13 failures (21.7%) are localisation-limited, and S13 frames are imaged at 3 to 6 px per module, so most of the S13 loss is a sampling loss of the kind treated in Section 6.3.

Derivation. Taking one pixel in every $s$ without averaging samples the image at a rate of $1/s$ per pixel. Any component at spatial frequency $f$ cycles per pixel above the Nyquist limit $1/(2s)$, half the sampling rate, is not removed; it reappears at the alias frequency

$$f_a = \left| f - \frac{k}{s} \right| \quad \text{for the integer } k \text{ that minimises it.}$$

A display imaged so that its pixel grid has a period of $P$ camera pixels contributes a component at $f = 1/P$. With $s = 2$ and, for example, $P = 2.2$, $f = 0.4545$ and $f_a = |0.4545 - 0.5| = 0.0455$ cycles per pixel: a band pattern with a period of 22 pixels, several modules wide, with the amplitude the component had before the reduction. Thresholded, such bands can join or cut the frame stroke, so that the connected component that should be the border merges with its surroundings or breaks. Averaging each block of 2 by 2 pixels before thresholding multiplies the component by $\cos(\pi f)$ along each axis, which for this example is $\cos(0.4545\pi) = 0.14$, removing 86% of it. The same reasoning applies to a halftone screen in print and to the symbol's own fine ring and ray structure at high density.

### 6.3 Sampling at low resolution and under blur

Evidence: S06 and S07 are sampling-limited (28,854 and 26,637 sampling-limited failures against 2,660 and 3,655 localisation-limited); the standard layout fails about one bin earlier than the print layout (Table 12); S01 misses 421 recoverable clean frames, which the campaign analysis places at or below 6.7 px per module.

**Contrast retained by a small mark.** Model a mark as a uniform disc of radius $a$ with unit contrast, blurred by an isotropic Gaussian point-spread function of standard deviation $\sigma$. The blurred value at the centre of the mark is the integral of the Gaussian over the disc:

$$c(a, \sigma) = \int_0^a \frac{1}{2\pi\sigma^2} \exp\left(-\frac{\rho^2}{2\sigma^2}\right) 2\pi\rho \, d\rho = 1 - \exp\left(-\frac{a^2}{2\sigma^2}\right).$$

For a square of half-side $h$ the same integral separates into two error functions, $c = \text{erf}\left(h / (\sigma\sqrt{2})\right)^2$.

Table 18. Peak contrast retained by an isolated mark under Gaussian blur, evaluated from the two expressions above. Blur is in module pitches.

| Mark | Size | Blur 0.2p | Blur 0.3p | Blur 0.4p |
|---|---|---:|---:|---:|
| Standard-layout ring dot | disc, radius 0.2p | 39.3% | 19.9% | 11.8% |
| Print-layout ring dot | disc, radius 0.3p | 67.5% | 39.3% | 24.5% |
| Module, as a disc of equal half-width | disc, radius 0.34p | 76.4% | 47.4% | 30.3% |
| Module, exact | square, side 0.68p | 83.0% | 55.2% | 36.6% |

At a blur of 0.3 pitch a standard-layout ring dot keeps 19.9% of its contrast while a module keeps 55.2%, a ratio of 2.8. The print-layout dot at 0.3 pitch keeps what the standard dot keeps at 0.2 pitch (39.3% in both cases, since $a/\sigma$ is 1 in both). The step from 0.2 to 0.3 pitch is two thirds of the width of an S06 bin (0.15 pitch), in line with the offset of roughly one bin between the layouts in Table 12. Half the S06 cases use a disc defocus, for which the Gaussian figures are approximate.

**Slot spacing on the inner ring.** In the standard layout ring 0 has radius 3.4 units and $S(v)$ slots, so adjacent slot centres are

$$\Delta = \frac{2\pi \cdot 3.4}{S} \text{ units} = \frac{2\pi \cdot 3.4 \cdot G}{29 \cdot S} \text{ pitches}.$$

For version 1 ($G = 29$, $S = 72$) this is 0.297 pitch, and it is the same for versions 11 and 31; over all 31 versions it ranges from 0.285 to 0.304 pitch. The dot radius is 0.2 pitch, so a dot is 0.4 pitch across. That is larger than the slot spacing, so adjacent printed dots overlap by about 0.1 pitch and form an arc. A single unprinted slot between two printed ones leaves a clear gap of only $2\Delta - 0.4p = 0.19$ pitch (0.17 to 0.21 over the 31 versions). Treated as an infinitely long slit of that width, which overstates what survives, such a gap keeps $\text{erf}\left(0.0967 / (\sigma\sqrt{2})\right)$ of its contrast: 37.1%, 25.3% and 19.1% at blur of 0.2, 0.3 and 0.4 pitch. The spacing widens outward, to 0.45 pitch on the outer ring of version 1 and 0.63 pitch on that of version 31, and the dial rays sit 0.66 pitch apart.

**Why ring bits fail first.** The finest structures of the standard layout are therefore the 0.19-pitch gap, the 0.30-pitch slot spacing and the 0.4-pitch dot, against a module 0.68 pitch wide on a lattice of 1.0 pitch: between a fifth and a half of the module scale. Adjacent ring slots are 3.4 times closer than adjacent modules, so reading inner-ring bits as reliably as modules needs roughly three times the resolution or a third of the blur. At 6 px per module the inner-ring slot spacing is $0.297 \times 6 = 1.8$ px and the isolated gap is 1.2 px. Alternating printed and unprinted slots then have a period of 3.6 px, close to the sampling limit of 2 px per cycle, and the gap is comparable to the pixel aperture and to the lens blur of the camera models (standard deviation 0.4 to 1.4 px) before any defocus is added. The build-85 reader samples each ring site with its 3 by 3 kernel and adjusts the decision by the two neighbouring slots (Section 2.2); the campaign did not measure how much contrast that step needs. Measurements recorded during the build-86 repair point the same way: in frames that fail with the true corners, module bits typically read with 0% errors while ring bits read 10% to 30% wrong.

This may also explain part of the gap between "recoverable" and "read". The classifier of Section 3.7 tests modulation at the module pitch. A standard-layout frame can pass that test while its ring bits retain little contrast. Such a frame is labelled RECOVERABLE although its ring bits may not be readable by any decoder. The number of such frames was not measured.

**The clean floor.** The campaign analysis states that the 421 recoverable S01 misses all lie at or below 6.7 px per module. This report could not confirm that from the retained evidence: the triage export lists up to five failures for each stratum, layout and failure reason, taken from the mildest severity bin first, and its S01 counts (461) include failures classed MARGINAL. The 11 exported S01 failures have a worst local resolution of 5.73 to 6.68 px per module, and the 10 with a recorded localisation error lie between 0.8 and 1.9 modules. Three lie at or above the declared 6.0 px per module (6.13 in the standard layout, 6.25 and 6.68 in the print layout) and decode when given the true corners, as does one at 5.94. The seven at 5.73 to 5.90 px per module do not decode even then. The first three fail gate G2 on corner fitting; the seven show a sampling limit just below the declared minimum. The exact count of S01 failures at or above 6.0 px per module is not in the retained evidence, because the gate evaluation file was not produced.

### 6.4 Scattered placement and burst damage

Evidence: the cliffs of S10 and S11 in Section 4.3, and the S19 class with a finder cover and a glare spot.

Derivation. Let local damage (glare, an occluder, a tear) destroy a fraction $f$ of the sites. Under scattered placement the eight sites of a byte are at eight unrelated places, so each is destroyed approximately independently with probability $f$, and the probability that a byte has at least one destroyed site (is touched) is

$$P_{\text{touched}} = 1 - (1 - f)^8.$$

A destroyed site reads a value unrelated to the printed one. Printed values are whitened, so it is wrong about half the time, and the probability that a byte is wrong is

$$P_{\text{wrong}} = 1 - (1 - f/2)^8.$$

Under byte-grouped placement the eight sites of a byte are neighbours, a compact blot destroys whole bytes, and the fraction of bytes touched is about $f$, plus a boundary term that shrinks as the blot grows relative to a byte.

Table 19. Fraction of bytes touched, and fraction wrong, for damage covering a fraction f of the sites, under the two placements.

| Damage f | Scattered, touched, 1 - (1 - f)^8 | Scattered, wrong, 1 - (1 - f/2)^8 | Grouped, touched, about f |
|---:|---:|---:|---:|
| 2% | 14.9% | 7.7% | 2% |
| 5% | 33.7% | 18.3% | 5% |
| 10% | 57.0% | 33.7% | 10% |

Compare Table 19 with Table 3. A decoder without erasures needs the wrong fraction to stay within $r/2$ of the block; a decoder that flags every touched byte as an erasure needs the touched fraction to stay within $r$. At 5% damage scattered placement leaves 18.3% of bytes wrong, which is beyond levels L (7.1%) and M (14.1%) and within Q (22.0%) and H (30.2%). At 10% damage 33.7% are wrong, beyond every level without erasures, and 57.0% are touched, within reach only of level H with every touched byte flagged (60.4%). Inverting the formulas, the largest damage fraction each level tolerates without erasures under scattered placement is $2(1 - (1 - t)^{1/8})$: 1.8% (L), 3.8% (M), 6.1% (Q) and 8.8% (H). With perfect erasure flagging it is $1 - (1 - 2t)^{1/8}$: 1.9%, 4.1%, 7.0% and 10.9%. The recoverability classifier makes the more severe assumption that every touched byte is wrong (Section 3.7), which halves the first set to 0.9%, 1.9%, 3.1% and 4.4%.

These thresholds agree with the observed cliffs in order of magnitude, and they do not reproduce them exactly. Occlusion over 5% to 10% of the field is beyond levels L and M and near the limit of Q, and S11 falls from 57.2% to 22.9% there. Glare falls faster than the model predicts (1.0% at 5% to 10%). The S10 coverage figure counts only the area where the highlight exceeds the white level, and the classifier already counts the larger area above half that level as lost; the effect of this difference on the reader was not isolated. Build 85 uses no erasures. In build 85 a single site outside the image also fails the whole attempt. Together with the border search's rejection of any component that touches the frame edge, this is sufficient to produce the S16 crop cliff.

Under grouped placement 5% damage costs about 5% of bytes, within level L (7.1%) as unknown errors. Erasure decoding can double that tolerance, because a known-missing byte costs one parity byte in $2e + s \le r$ where an unknown wrong byte costs two. With scattered placement erasures gain little: at 5% damage, flagging every touched byte costs 33.7% of a block, against twice 18.3% (36.6%) for leaving the wrong bytes unflagged. This is consistent with the build-86 unit test in which a disc over 5% of a version-11 symbol at level M read 6 of 6 times with grouped placement and 0 of 6 with scattered placement: 18.3% wrong exceeds level M's error capacity of 14.1%, 33.7% touched exceeds its erasure capacity of 28.2%, and 5% is inside both.

### 6.5 Corner fitting under perspective, lens distortion and curved surfaces

**Perspective.** Evidence: S04 is the stratum with the most localisation-limited failures after S09 and S15 (8,797), `BORDER_MISS` is its dominant reason, and success drops from 49.9% at 30 to 45 degrees to 12.3% at 45 to 60 degrees. A square tilted by angle $\theta$ about an axis in its plane is foreshortened by $\cos\theta$ along the perpendicular direction, so its bounding box has an aspect ratio near $\cos\theta$ when the tilt axis is aligned with the image axes. The build-85 border search discards components whose aspect ratio is at or below 0.72. That limit is reached at $\theta = \arccos(0.72) = 43.9$ degrees, and at 60 degrees the ratio is 0.50. The position of the S04 cliff, between the 30 to 45 and the 45 to 60 degree bins, is consistent with this shape filter. The association is inferred from the source and the bin boundaries; it was not isolated by experiment in the campaign.

**Lens distortion.** The simulator maps normalised image coordinates $(x, y)$, in units of the focal length, to distorted coordinates by the Brown radial model

$$(x_d, y_d) = (x, y) \cdot \left(1 + k_1 r^2 + k_2 r^4\right), \qquad r^2 = x^2 + y^2.$$

A planar homography maps straight lines to straight lines. Radial distortion bends them: a line at height $y_0$ becomes $y_d = y_0 (1 + k_1 (x^2 + y_0^2))$ to first order in $k_1$, a parabola in $x$. Over a side of half-length $h$ its bow, the largest departure from the chord, is

$$\delta \approx F \cdot |k_1| \cdot y_0 \cdot h^2 \text{ pixels},$$

where $F$ is the focal length in pixels. No choice of four corners removes it. Because the bow scales with $y_0$, the distance from the image centre, off-centre placement makes it larger.

Table 20 evaluates the residual exactly, not to first order. A fronto-parallel square was projected through the simulator's model (both $k_1$ and $k_2$), a homography was fitted through its four distorted corners, and the largest distance between true and predicted positions over a 41 by 41 grid of interior points was recorded. The residual is given in pixels and in module pitches for symbols 31, 62 and 124 pitches across (versions 1, 11 and 21), using the mean side of the distorted symbol to convert.

Table 20. Largest residual of a four-corner homography under the simulator's radial distortion. The ultrawide rows use the S19 class 1 placement (centre at 22% of the frame width and 18% of its height, a normalised radius of 1.25) and $k_2 = 0.02$. The symbol side is its undistorted size.

| Camera and placement | k1 | Side (px) | Residual (px) | Pitches, N = 31 | Pitches, N = 62 | Pitches, N = 124 |
|---|---:|---:|---:|---:|---:|---:|
| Ultrawide, off-centre | -0.08 | 300 | 5.95 | 0.67 | 1.35 | 2.69 |
| Ultrawide, off-centre | -0.12 | 300 | 7.18 | 0.95 | 1.89 | 3.78 |
| Ultrawide, off-centre | -0.15 | 300 | 8.38 | 1.25 | 2.50 | 5.00 |
| Ultrawide, off-centre | -0.12 | 200 | 2.94 | 0.59 | 1.17 | 2.34 |
| Ultrawide, centred | -0.12 | 300 | 1.41 | 0.15 | 0.30 | 0.59 |
| 1080p wide, centred | -0.02 | 900 | 1.42 | 0.05 | 0.10 | 0.20 |
| Budget 1080p, centred | -0.05 | 900 | 3.17 | 0.11 | 0.22 | 0.44 |

A site is read correctly only if the sampling point lands inside its mark, which for a module of side 0.68 pitch means an error well under 0.34 pitch, and for a standard-layout ring dot under 0.2 pitch. For the symbol sizes of Table 20 on the off-centre ultrawide placement the residual is 0.6 to 1.3 pitches for the smallest symbol and several pitches for denser ones. A planar fit cannot place its sampling points inside the marks across such a frame, which is sufficient to account for the 1.1% success of S19 class 1 and the 0.81% of the ultrawide model overall. The symbol sizes actually generated in that class are not tabulated in the frozen report. The last two rows show a second consequence: on the ordinary camera models the residual is small for small symbols but reaches 0.2 to 0.44 pitch for a dense symbol that fills the frame, comparable to the mark size. This may contribute to the decline at high density; no stratum isolates it.

**Curved surfaces.** In 30% of S04 cases the symbol lies on a cylinder with a bend radius of 0.8 to 4.0 symbol widths. A cylinder is not a plane, and the departure of its image from any homography grows with the square of the symbol's width over the bend radius, by the same argument as for the lens. Build 85 has no non-planar model. The campaign does not report the bent and flat S04 cases separately.

### 6.6 Error-correction level and density

Level H corrects 4.3 times the errors of level L, yet reads only 9.5 points more in the standard layout and 6.4 in the print layout (Table 13). Three results bear on this.

1. 200,240 of the 448,718 valid failures (44.6%) are `NO_BORDER` or `BORDER_MISS`. The decoder never received the true symbol, so no amount of parity could help.
2. Among frames that are sampled, errors are not spread thinly. Under blur or low resolution a whole class of marks (the ring bits) fails together at 10% to 30% bit error. With scattered placement nearly every byte holds a ring bit, so the byte error rate jumps past every level at once: with a fraction $g$ of sites being ring sites read at bit error rate $\epsilon$, the byte error probability is about $1 - (1 - g\epsilon)^8$, already 15% at $g\epsilon = 0.02$.
3. Local damage behaves as in Section 6.4, where the damage tolerated without erasures rises only from 1.8% to 8.8% of the sites between L and H.

Parity is effective against independent, sparse byte errors. The observed errors are either total (not located), systematic (a mark class below its contrast limit) or bursty and spread across all bytes by the placement. Higher levels, including the levels U and X added in build 86, are expected to pay off mainly once placement is grouped and erasures are used. The campaign did not measure this.

Density has a separate, physical component. Versions 25 to 31 are 164 to 248 pitches across with their margin and cannot reach 6 px per module in a 1080 px frame; version 31 reaches 3.9. The standard layout reads 32.95% in this band against 46.75% for the print layout, a wider gap (13.8 points) than in versions 1 to 8 (3.6 points), which is what Section 6.3 predicts when resolution falls and the finest marks go first.

### 6.7 The stall and compute bounds

The failure-targeted pool sealed its last shard at 16:15:37 UTC on 9 November. The cross-condition pool then sealed 4 of its 50 shards between 16:24:36 and 16:28:29, with 1 of 4,000 cases meeting its expected outcome. No shard sealed afterwards, and the state log records no transition after `EXECUTE_500K`. The campaign analysis reports that the server was idle for about 10.5 hours before it was released. Up to that point throughput had been steady: 1,000,000 structured cases in 24.97 hours (40,046 per hour) and 350,000 failure-targeted cases in 8.94 hours (39,132 per hour), against a preflight estimate of 36,183 per hour.

The cause is not established. The analysis proposes that one or more cross-condition cases, which combine two of the ten worst conditions, sent the reader into very long or unbounded computation. Two facts bear on this. First, build 85's corner refinement contains a hill-climb that repeats for as long as its score improves, with no explicit iteration limit; the build-86 changelog calls it "the unbounded key hill-climb" and removes it. Very long reader compute on adversarial input therefore cannot be excluded. Second, the analysis also states that the harness has no per-case timeout, while the supervisor source (`runners/sharding/supervisor.py`) and `configs/campaign.yaml` define a limit of 180 seconds without progress, after which the worker is killed and the case recorded as a reader timeout. No timeout was recorded in any sealed shard. Either that mechanism did not act on the stalled workers, or the stall lay outside the reader, in the supervisor, the upload of sealed shards or the state machine. The retained evidence does not distinguish these.

Until the stalled case is reproduced, gate G6 cannot be passed: if the reader was the cause, this is a finding of unbounded compute against the reader; if not, it is a harness defect that prevented the campaign from completing. The cross-condition plan is deterministic and can be regenerated from the locked selection to find the case.

## 7. What the campaign establishes and what it does not

**Established by the evidence**, for build 85 on the simulated frames of this campaign:

1. No wrong payload was accepted in 1,364,000 completed cases, including 100,000 hard negatives, and every structured negative that had to be rejected was rejected.
2. The reader does not meet its declared clean floor. It missed 421 of 34,136 recoverable clean frames, and at least three exported misses lie at or above 6 px per module. Gate G2 is failed.
3. The reader read 68.93% of frames classed recoverable and 50.14% of all valid frames. Gate G3, which requires 99.9% on mild recoverable frames, was not computed. The campaign analysis records it as failed, and the per-stratum recoverable rates are far below 99.9% (Appendix B).
4. The print layout is read more often than the standard layout, by 7.33 points overall and in 19 of 20 strata.
5. Failures are steps, not slopes, for partial crops, finder cover with glare, glare beyond 5% and the off-centre ultrawide class.
6. The campaign did not complete. Gate G7 is failed: 354,000 of 500,000 adaptive cases, no boundary search, no holdout, no device data.

**Supported, with the mechanism derived but not isolated by a controlled experiment:**

1. Whole-frame thresholding contributes to the S09 and S15 localisation failures (Section 6.1). The derivation follows the source. Build 86, which among other changes added local thresholds, moved S09 from 27.4% to 60.0% on a subset (Section 8.2), which is consistent with it.
2. The standard layout's inner-ring bits, carried by dots 0.4 pitch across on centres about 0.3 pitch apart with gaps of about 0.2 pitch, are the first to fail under blur and low resolution (Section 6.3).
3. Scattered placement converts 5% of local damage into byte errors beyond levels L and M, and 10% into byte errors beyond every level (Section 6.4).
4. A planar homography cannot follow the ultrawide model's distortion or a cylinder (Section 6.5).
5. The aspect-ratio filter in the border search is consistent with the position of the perspective cliff near 45 degrees (Section 6.5).
6. Point sampling in the border search aliases display and halftone patterns (Section 6.2). Its share of the S13 loss was not measured.

**Not shown:**

1. Anything about real cameras, real prints or real displays. The simulator has not been compared with a capture corpus.
2. Device latency (G4) or reader-only memory (G6).
3. That the reader's compute is bounded. The stall is unexplained.
4. Holdout performance, and therefore the independent leg of G1.
5. Agreement of the two rasterisers on the structured cohort (Section 3.10, item 7).
6. Field read rates. The structured design weights conditions equally by stratum, not by frequency of use, and the adaptive cohorts are selected for difficulty.
7. Behaviour with two valid codes in one frame, which S16 did not generate.
8. Any property of Profiles 1, 2 and 3.

The label UNQUALIFIED follows from G2, G3 and G7.

## 8. Response to date and remaining plan

### 8.1 Build 86: what changed

Build 86 (0.1.0-build.86, 2026-11-10) is the repair pass that followed the campaign. From the changelog and the reader repository's `CONTEXT.md`:

* **Border search:** an area-averaged reduction of the frame replaces point sampling; two local-threshold passes are added to the whole-frame pass; outlines are fitted as perspective quadrilaterals at any tilt.
* **Corner refinement:** the edges of the frame stroke are located directly to sub-pixel accuracy and fitted as four lines; radial distortion is estimated from the bow of the sides; Profile 0 then registers on the module lattice. The unbounded hill-climb is removed.
* **Decoding:** sites outside the image become unknown instead of failing the frame; levels are taken locally; washed-out regions become erasures; blur leak between neighbouring marks is estimated by least squares; a sequence detector reads ring dots. Four reading strategies are tried on one set of samples.
* **Erasure and soft-decision decoding:** after plain correction fails, the least reliable bytes are erased in capped steps under $2e + s \le r$, with two syndromes held back as a check.
* **Work budget:** every image sample draws on a per-call allowance, and the call returns "no read" when it is spent.
* **Formats 0.29 and 0.30:** byte-grouped placement for the standard and print layouts, with unchanged geometry and capacity, opt-in because older readers cannot read them. Levels U (80% parity) and X (95%) are added.
* **Capture layer (Swift):** the scanner controls focus, exposure and zoom, restricts decoding to a region of interest, and does not select the ultrawide camera.

The acceptance rule is unchanged: RS correction and CRC-32C.

### 8.2 Measured effect on a 6,000-case subset

Build 86 was compared with build 85 on the first 300 structured cases of each of the 20 strata, 6,000 cases, on identical frames regenerated locally (harness commit `9ff83e3`; the shard hashes of the regenerated plan are recorded as matching the campaign's). **This is a local comparison on a subset. It is not a re-run of the campaign, not the campaign's regression procedure and not a holdout result, and it does not qualify the reader.**

Table 21. Build 85 against build 86 on the 6,000-case local subset, from the build-86 changelog. The changelog does not give the per-layout denominators. Times are host times on a 4-core container with four workers.

| Measure | Build 85 | Build 86 |
|---|---:|---:|
| Valid symbols decoded (5,401 valid) | 50.0% | 59.1% |
| Recoverable symbols decoded (3,267 recoverable) | 64.4% | 76.0% |
| Recoverable, standard layout | 60.8% | 70.5% |
| Recoverable, print layout | 68.2% | 81.5% |
| Wrong payloads (6,000 cases, 599 hard negatives) | 0 | 0 |
| Reader time per frame, median | 41 ms | 88 ms |
| Reader time per frame, 95th percentile | 187 ms | 536 ms |
| Frames that decoded, median and 95th percentile | 32 and 62 ms | 49 and 120 ms |
| Frames that did not decode, median | 77 ms | 261 ms |

Build 86 gained 562 cases and lost 71 that build 85 read, a net gain of 491, which is $491 / 5{,}401 = 9.09$ points of valid symbols and agrees with the table. Reconstructing counts from the published percentages (2,483 and 2,104 of 3,267), the Wilson 95% intervals for the recoverable rates are approximately 74.5% to 77.4% for build 86 and 62.7% to 66.0% for build 85. The same frames were used for both builds, so the difference is better determined than these separate intervals suggest.

Table 22. Recoverable symbols decoded per stratum on the 6,000-case subset, build 85 and build 86, from the changelog. Each stratum contributes 300 cases, so the recoverable denominators are small; the changelog gives one, 29 cases for S11.

| Stratum | Build 85 | Build 86 | Change (points) |
|---|---:|---:|---:|
| S01 Clean | 99.5% | 99.5% | 0.0 |
| S02 Payload patterns | 96.3% | 97.9% | +1.6 |
| S03 Scale and sampling | 69.7% | 77.8% | +8.1 |
| S04 Perspective and bends | 40.6% | 69.4% | +28.8 |
| S05 Rotation | 92.8% | 97.0% | +4.2 |
| S06 Defocus | 31.7% | 42.2% | +10.5 |
| S07 Motion blur | 33.8% | 41.0% | +7.2 |
| S08 Low light | 88.5% | 94.2% | +5.7 |
| S09 Contrast and shadows | 27.4% | 60.0% | +32.6 |
| S10 Glare | 39.0% | 63.4% | +24.4 |
| S11 Occlusion (29 cases) | 72.4% | 62.1% | -10.3 |
| S12 Print | 58.0% | 68.2% | +10.2 |
| S13 Screens | 61.5% | 73.7% | +12.2 |
| S14 Compression and ISP | 80.7% | 87.4% | +6.7 |
| S15 Colour | 54.4% | 86.0% | +31.6 |
| S16 Clutter and crops | 87.6% | 93.8% | +6.2 |
| S17 Ring stress | 75.6% | 77.7% | +2.1 |
| S18 Ray stress | 46.9% | 60.3% | +13.4 |
| S19 Lens and finder stress | 72.0% | 79.3% | +7.3 |
| S20 Combined conditions | 88.6% | 94.4% | +5.8 |

The largest gains are in S09 (+32.6 points), S15 (+31.6), S04 (+28.8) and S10 (+24.4). These are strata for which Section 6 derived a reader mechanism and build 86 changed the corresponding code: local thresholds, perspective quadrilaterals with edge fitting, and erasures for washed-out regions. Build 86 made all its changes at once, so the comparison does not attribute a gain to any single change. S06 and S07 gained 10.5 and 7.2 points and remain below 43%, and S17 gained 2.1 points. S01 did not move, so the clean floor is not repaired on this evidence.

Four cautions apply.

* **The subset is not representative of the campaign.** Build 85 reads 64.4% of recoverable frames on the subset and 68.93% in the campaign; for S10 the figures are 39.0% and 82.86%, and for S04 40.6% and 50.76%. Subset rates must not be compared with campaign rates.
* **Speed regressed.** Median time per frame rose by a factor of 2.1 (41 to 88 ms) and the 95th percentile by 2.9 (187 to 536 ms). Frames that decode cost 1.5 times as much (32 to 49 ms); frames that do not cost 3.4 times as much (77 to 261 ms). The campaign's host 95th percentile was already above 200 ms.
* **71 cases regressed** (1.18% of 6,000; 1.31% of the 5,401 valid cases), and S11 fell on its 29 recoverable cases. Neither was investigated. Under gate G5 a regression must be shown non-inferior per cell.
* **The Swift code is uncompiled.** The SDK camera layer, the reference reader and the iOS application were written without a Swift toolchain and have not been built or run. The .NET and WebAssembly builds and the Android instrumented tests were also not run.

The integrity bound of Section 5.1 does not carry over. Zero wrong payloads in 6,000 cases gives a 95% upper bound of only $1 - 0.05^{1/6000} = 499$ per million for build 86.

### 8.3 What remains open

From the reader repository's record, in its own order of importance:

1. Ring dots in the standard layout remain the main limit. On a 2,000-case development subset the standard layout read 85% of recoverable frames under 0.2 pitch of blur, 34% at 0.2 to 0.4 and 0% above; the print layout read 88%, 81% and 16% (the record gives no denominators for these bands). An earlier version of the record took 0.2 pitch as the dot diameter. The record now agrees with the specification (radius 0.2 pitch, 0.4 pitch across), which this report follows. The ring sequence detector does not lower the mean ring error on hard frames.
2. Curved surfaces: no non-planar fit.
3. Heavy blur of 0.4 pitch and more fails for both layouts.
4. Corner fit: frames remain where the true corners decode and the reader's do not.
5. Not done: moiré pre-filter keyed to a detected period; multi-frame combining; ellipse-constrained fit; a finder-loss fallback; border search for a symbol partly out of frame; ultrawide distortion beyond about -0.15; reader-only memory measurement; the Android camera layer.
6. Harness: reproduce the stall, verify the per-case timeout, add a stall alarm.
7. The full campaign has not been run on build 86.

### 8.4 The standard layout's ring dots as a format question

Section 6.3 indicates that the difficulty of the ring bits of Format 0.27 is a property of the format, not only of the reader. With a dot radius of 0.2 pitch (0.4 pitch across) and inner-ring centres 0.297 pitch apart, an isolated ring dot retains 19.9% of its contrast at a blur of 0.3 pitch, where a module retains 55.2%, and at the declared 6 px per module an isolated gap between printed dots is 1.2 px wide. A better reader model can move the limit, and the build-86 detector tried; it cannot change what the optics deliver. Grouped placement (Format 0.29) confines ring errors to ring bytes but does not make the dots readable.

Three courses are open, and the choice belongs to the owner because each has a compatibility cost.

* **Keep Format 0.27 and state its limit.** Declare the minimum resolution and maximum blur at which the ring bits, not the modules, are readable, and correct the recoverability classifier to test the finest mark. This changes no symbol and lowers the declared performance.
* **Recommend the print layout where robustness matters.** Format 0.28 already places ring dots (0.6 pitch across) and rays at least one pitch apart centre to centre, with ink-to-ink gaps of at least 0.39 pitch, and reads better in 19 of 20 strata, at the cost of capacity (3,453 against 4,619 bytes at version 31, level M; 7 against 27 at version 1). The owner decision of 2026-11-10 keeps Format 0.27 as the default.
* **Revise the standard layout's rings** in a new format version, for example larger dots at equal-arc spacing on the inner rings only. This needs a versioned specification and a compatibility review, as the mandate requires for any wire-format change, and the mandate forbids revising a format merely to fit a deficient reader and requires mathematical proof and compatibility analysis first. The derivation in Section 6.3 is a first step toward that justification; it should be confirmed on real prints and captures before a format is changed.

### 8.5 Prioritised plan and acceptance tests

Each item names the stratum and gate it should move and how that will be measured. Gate thresholds are those locked in `configs/gates.yaml`. Where no locked threshold exists the test is a comparison against the build-85 baseline of this report under the G5 rule. The owner's stated expectation, recorded in the reader repository, is 98% to 100% of recoverable frames.

Table 23. Remaining work in priority order, with the acceptance test for each item.

| Priority | Item | Should move | Acceptance test |
|---:|---|---|---|
| 1 | Reproduce the stall; verify the 180-second per-case limit; add an alarm when no shard seals for 30 minutes; measure reader-only memory | G6, G7 | The regenerated cross-condition pool seals all 50 shards; every reader timeout is a recorded result; reader-only peak memory is 512 MB or less on the structured cohort; sanitizer and 60-minute fuzz evidence is produced |
| 2 | Regression of build 86 with the campaign's own procedure (`./ckt build-candidate`, `./ckt regress`) on the golden suite of the first 1,000 structured cases per stratum | G5 | Non-inferiority at 0.5 points in every stratum, layout and bin cell of at least 400 cases, Holm-adjusted at 0.05; the 71 regressed cases and S11 are explained |
| 3 | Reader time | G4 | Host 95th percentile on 1080-px frames no higher than build 85's 227.6 ms as an interim check; then 200 ms or less on a named reference iOS device over at least 10,000 frames |
| 4 | Clean floor at 6 to 7 px per module, or a revised declared minimum | G2, S01 | Zero failures among S01 valid cases at or above the declared minimum in a full structured re-run |
| 5 | Decision on the standard layout's ring dots (Section 8.4), and a classifier that tests the finest mark | S06, S07, S03, S12, S13 for the standard layout | Standard-layout decode rates in S06 bins 1 and 2 (30.7% and 0.4% in this campaign) within the G5 margin of the print layout's (75.1% and 31.2%), or a published limit and a classifier under which such frames are not labelled recoverable |
| 6 | Non-planar fit for curved surfaces | S04 | S04 recoverable rate reported separately for bent and flat cases; bent cases reach the flat-case rate within the G5 margin |
| 7 | Lens distortion on wide-angle cameras | S19 class 1, ultrawide model | S19 bin 1 above its present 1.1%, and reported against bin 0 (79.1%) |
| 8 | Finder-loss fallback and border search for partly visible symbols | S19 bin 3, S16 bin 4 | S16 bin 4 above 2.3% and S19 bin 3 above 0.1%, with zero wrong payloads on cropped and covered negatives |
| 9 | Multi-frame combining in the capture layer | S07, S20 sequences | Sequence success in S20 and time to first success, against the single-frame baseline |
| 10 | Complete the campaign on the repaired build: structured million, full adaptive pool including boundary search, and the untouched 100,000-case holdout | G1, G3, G7 | 1,000,000 structured, at least 500,000 adaptive and 100,000 holdout cases sealed; zero wrong payloads; G3 weighted rate with its lower bound |
| 11 | Pre-registered strata for Formats 0.29 and 0.30 and for erasure decoding | S10, S11 under grouped placement | A new locked design, since the present one covers Formats 0.27 and 0.28 only; decode rate in S10 and S11 bins 1 and 2 against the scattered formats |
| 12 | Compile and field-test the Swift capture layer; capture the real-device corpus; compare it with the simulator | G4, G7, simulator validity | Two-sample tests of px per module, edge spread, noise and saturation between capture cells and matched simulated cases, as the real-device protocol specifies |

Items 1 and 2 come first because every later measurement depends on a harness that completes and on a baseline comparison that is valid. Item 5 precedes further reader work on blur for the reason given in the reader repository's own record: the decision about the ring dots determines whether that work can succeed.

## 9. Conclusions

The campaign completed 1,364,000 cases against CKey core build 85 in simulation, including a 10,000-case pilot. Of the 1,600,000 qualification and holdout cases planned, 1,354,000 ran. Its findings are these.

1. **Integrity held.** No wrong payload was accepted. The 95% upper bound on the wrong-payload rate is 3.0 per million for the structured design, 6.0 per million per layout and 2.2 per million over all completed cases, under an independence assumption that is not fully met. This is a bound for these simulated conditions and this build.
2. **Read rate is the weakness.** The reader read 68.93% of the frames classed physically recoverable and 50.14% of all valid frames. It missed 1.23% (421 of 34,136) of clean recoverable frames, some of them at or above the declared resolution.
3. **About one failure in five is a failure to locate the symbol** (82,307 of 448,718 by the ground-truth-corner diagnostic), concentrated in low contrast, colour and perspective. By derivation from the source, not by controlled experiment, they are attributed to a whole-frame threshold, point-sampled search, a shape filter and a planar fit.
4. **Four in five are failures to read located marks**, attributed in the same way to ring dots and gaps finer than the module in the standard layout, to scattered placement that spreads local damage over most bytes, and to hard-decision decoding without erasures. These interact so that stronger error correction gains little.
5. **The print layout is the more robust format** on this evidence, by 7.33 points overall and by 13 to 22 points where resolution, blur, ink spread or display pixels limit reading.
6. **The campaign is incomplete and the reader is unqualified.** Gates G2, G3 and G7 are failed, G1 lacks its holdout leg, G4 and G5 were not evaluated, and G6 cannot pass until the stall is explained and the reader's memory is measured alone.

Build 86 addresses the mechanisms identified and, in a local comparison on a 6,000-case subset, improved the recoverable read rate from 64.4% to 76.0% of 3,267 cases, at the cost of a reader 2.1 times slower at the median, with 71 regressed cases unexamined and its Swift components uncompiled. That result is unverified: it is not a campaign run and not a holdout result. The plan in Section 8.5 orders the work so that the next full run, on the locked design with the untouched holdout, can test it properly. The standard layout's ring dots require a decision about the format before more reader effort is spent on blur.

## Appendix A. Glossary of strata

Table 24. The 20 strata, their primary factor and the five severity bins, from `configs/strata.yaml` (design version `strata-v4`). Bin 0 is the mildest.

| Stratum | Subject | Factor and unit | Bin 0 | Bin 1 | Bin 2 | Bin 3 | Bin 4 |
|---|---|---|---|---|---|---|---|
| S01 | Clean, ideal reference and exact round trip | px per module as a fraction of the log range from the declared minimum to the feasible maximum | 0 to 0.2 | 0.2 to 0.4 | 0.4 to 0.6 | 0.6 to 0.8 | 0.8 to 1.0 |
| S02 | Payload length, density, pathological bit patterns | payload bytes over capacity | 0 to 0.2 | 0.2 to 0.4 | 0.4 to 0.6 | 0.6 to 0.8 | 0.8 to 1.0 |
| S03 | Symbol scale, downsampling, sub-pixel phase | px per module | 5.2 to 7.5 | 3.8 to 5.2 | 2.8 to 3.8 | 2.0 to 2.8 | 1.2 to 2.0 |
| S04 | Perspective, skew, non-planar warping | tilt off axis, degrees | 0 to 15 | 15 to 30 | 30 to 45 | 45 to 60 | 60 to 72 |
| S05 | Rotation, mirror rejection, finder orientation | in-plane rotation, degrees | 0 to 72 | 72 to 144 | 144 to 216 | 216 to 288 | 288 to 360 |
| S06 | Gaussian and optical defocus, lens point spread | blur standard deviation over module pitch | 0 to 0.15 | 0.15 to 0.3 | 0.3 to 0.45 | 0.45 to 0.65 | 0.65 to 0.9 |
| S07 | Directional and rolling-shutter motion blur | motion length over module pitch | 0 to 0.5 | 0.5 to 1.0 | 1.0 to 1.5 | 1.5 to 2.5 | 2.5 to 4.0 |
| S08 | Low light, photon and read noise, high gain | electrons at white | 1,500 to 6,000 | 500 to 1,500 | 150 to 500 | 50 to 150 | 12 to 50 |
| S09 | Contrast, uneven illumination, shadows | Michelson contrast of marks | 0.6 to 0.9 | 0.4 to 0.6 | 0.25 to 0.4 | 0.15 to 0.25 | 0.07 to 0.15 |
| S10 | Specular glare, saturation, highlights | fraction of symbol under glare | 0 to 0.05 | 0.05 to 0.1 | 0.1 to 0.2 | 0.2 to 0.3 | 0.3 to 0.45 |
| S11 | Occlusion, tears, scratches, missing regions | occluded fraction of field | 0 to 0.05 | 0.05 to 0.1 | 0.1 to 0.15 | 0.15 to 0.25 | 0.25 to 0.4 |
| S12 | Print dot gain, ink bleed, halftone, paper | ink spread over module pitch | 0 to 0.05 | 0.05 to 0.1 | 0.1 to 0.15 | 0.15 to 0.25 | 0.25 to 0.35 |
| S13 | Screen pixels, sub-pixels, moiré, refresh | display pixels per module | 8 to 12 | 5 to 8 | 3.5 to 5 | 2.5 to 3.5 | 1.6 to 2.5 |
| S14 | Compression, sharpening, denoising, ISP | JPEG quality | 90 to 100 | 70 to 90 | 50 to 70 | 30 to 50 | 10 to 30 |
| S15 | Colour foreground and background, gamma, transparency | luminance contrast ratio | 7 to 21 | 4.5 to 7 | 3 to 4.5 | 2 to 3 | 1.4 to 2 |
| S16 | Clutter, distractors, partial crops | class | clutter | clutter, 2 to 5 distractors | clutter, 6 to 12 distractors | clutter, 6 to 12 distractors | crop of 3% to 25% |
| S17 | Ring dot and arc, radial sampling stress | rotation during exposure over ring slot angle | 0 to 0.25 | 0.25 to 0.5 | 0.5 to 0.75 | 0.75 to 1.0 | 1.0 to 1.5 |
| S18 | Ray length and weight, angular aliasing | px per dial ray slot | 3.5 to 6 | 2.5 to 3.5 | 1.8 to 2.5 | 1.3 to 1.8 | 0.9 to 1.3 |
| S19 | Grid, finder safe zone, corner localisation | class | mild distortion | ultrawide, off-centre | finder cover 1% to 3% | finder cover 3% to 6%, glare | ultrawide, finder cover, tilt |
| S20 | Combined realistic conditions, temporal sequences | joint severity multiplier | 0.5 to 0.75 | 0.75 to 1.0 | 1.0 to 1.25 | 1.25 to 1.5 | 1.5 to 1.8 |

## Appendix B. Gate table

Table 25. Gates with requirement, result and status. Results are for build 85. The campaign did not produce a gate evaluation file; statuses follow the campaign analysis except where noted.

| Gate | Requirement | Result | Status |
|---|---|---|---|
| G1 No wrong payload | 0 in structured, adaptive and holdout | 0 in 1,354,000 structured and adaptive cases; holdout not run | Met for what ran; not evaluable until the holdout runs |
| G2 Clean round trip | 100% of S01 valid cases at 6 px per module or more | S01 recoverable 98.77% (421 misses); exported misses at 6.13, 6.25 and 6.68 px per module | Failed |
| G3 Normal use | 99.9% or more, recoverable, bins 0 and 1, use-weighted | Not computed. S01 alone is 98.77% of recoverable over all bins; most strata are far lower | Failed according to the campaign analysis; not computed |
| G4 Latency | 95th percentile of 200 ms or less on the reference iOS device | No device named. Host 95th percentile 227.6 ms and 214.6 ms on 1080-px frames | Not evaluated |
| G5 No regression | Non-inferior to baseline, 0.5 points per cell | This run is the baseline | Not applicable |
| G6 Memory and compute safety | No crash, timeout or sanitizer finding; 512 MB or less; 60 minutes of fuzzing | 0 reader faults in sealed shards; peak heap 550.8 MB for reader and simulator together; stall unexplained; no sanitizer or fuzz report in the evidence | Not passed; re-measurement required |
| G7 Coverage | 1,000,000 structured; 500,000 adaptive; 100,000 holdout; real device | 1,000,000; 354,000; 0; none | Failed |

Note on G3: the declared use weights sum to 1.10, not 1.00; the evaluator divides by their sum. The statistic needs counts for recoverable cases in bins 0 and 1, which the frozen report does not tabulate. A bound is available: S01 has normalised weight $0.10/1.10 = 0.0909$, so even if every other stratum read 100%, a recoverable S01 rate of 98.77% in bins 0 and 1 would cap the weighted rate at $1 - 0.0909 \times 0.01233 = 99.89\%$, below 99.9%. The rate of 98.77% is for all five bins; the rate in bins 0 and 1, the lowest resolutions, is not tabulated. As an indication only, weighting each stratum's all-bin recoverable rate (Table 9) by the normalised weights gives 74.2%.

Note on G6: the locked gate text defines the 512 MB limit as "reader + harness heap in the worker". By that wording the measured maximum of 550.8 MB exceeds the limit. The campaign analysis records the gate as needing a reader-only measurement.

## Appendix C. Reproduction

The following commands are from the campaign repository's `README.md`. They require the campaign repository at the locked harness commit, the reader at commit `ede38f5`, and access to the sealed data.

```
# Regenerate one case bit for bit (frames as PNG, trace, hash check)
./ckt repro structured-S01-P0-b0-joi-01488

# Offline verification: shard hashes, signatures, re-execution of a sample of 100 cases
scripts/verify.sh 100

# Build a candidate reader and run the golden-suite regression (gate G5)
./ckt build-candidate --ckey /path/to/ckey-with-fix --label fix1
./ckt regress --label fix1

# Independent holdout (sealed seed) on a candidate or on the baseline
./ckt holdout --label fix1

# Evaluate gates and assign a label
./ckt review

# Whole pipeline at 1/2000 scale (never qualifies)
CKT_TEST_SCALE=2000 ./ckt --data-dir /tmp/ckt-test run --approver test
```

Case identifiers for exported counterexamples are in `evidence/counterexamples/` (233 case lines). The per-stratum, per-layout and per-bin counts used in this report are in `evidence/reports/structured_1m.json`, whose SHA-256 is recorded in `structured_1m.freeze.json`. The derived figures in Sections 5 and 6 can be recomputed from the formulas given; Table 20 needs a short numerical script.

## Appendix D. Notation

Table 26. Symbols and terms used in this report.

| Symbol or term | Meaning |
|---|---|
| p | Module pitch: the spacing of the Cartesian lattice, 29/G field units |
| G, S | Modules across the field and slots per ring and dial, for a version |
| N | Width of the symbol with its margin in module pitches, 31G/29 |
| px per module | Camera pixels per module pitch in the frame given to the reader |
| RS | Reed-Solomon code over GF(256) |
| r, e, s | Parity bytes in a block; unknown byte errors; erasures (known-position missing bytes). Correctable when 2e + s is at most r |
| t | Fraction of a block correctable without erasures, r/2 over the block length |
| CRC-32C | 32-bit cyclic redundancy check, Castagnoli polynomial |
| n, k | Trials and successes in a binomial sample |
| alpha | One minus the confidence level |
| p_U | One-sided upper confidence bound on a rate |
| sigma | Standard deviation of a Gaussian blur |
| a, h | Radius of a disc mark; half-side of a square mark. In Section 3.7, a is the site-loss fraction |
| Delta | Spacing of adjacent slot centres on a ring |
| f | In Section 6.2 a spatial frequency; in Section 6.4 the damaged fraction of sites |
| k1, k2 | Radial distortion coefficients on focal-normalised coordinates |
| F | Focal length in pixels |
| MTF | Modulation transfer function |
| d' | Detectability: signal difference over noise standard deviation |
| ISP | Image signal processor |
| Stratum, bin, cell | One of 20 condition families; one of five severity ranges; a stratum, layout and bin combination |
| Valid case | A case whose symbol is a correct encoding and should decode |
| Hard negative | A case built so that the reader must reject it or must not return a different payload |
| Wrong payload | An accepted payload that differs from the encoded one |
| Recoverable | Classified RECOVERABLE by the rule of Section 3.7 |
| Localisation-limited | A failure that decodes when the true field corners are supplied |
| Sampling-limited | A failure that does not decode even with the true field corners |
| Design effect | Ratio of the variance of an estimate under clustering to its variance under independence |
