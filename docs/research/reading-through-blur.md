# Reading through blur and low resolution: what other systems do, and what CKey uses

**Status:** working research note, 2026-11-10 (build 86). It records a literature survey made for the reader repair pass, what was taken from it, what was measured, and what is still untried. Items marked *abstract only* were not read in full; treat their details as unverified.

## The problem in CKey's terms

A Profile 0 symbol has three kinds of mark, all sized in module pitches (p):

| Mark | Size | Spacing | Under blur |
|---|---|---|---|
| Square module | 0.68 p across | 1 p | Keeps 83% of its contrast at a blur of 0.2 p, 55% at 0.3 p |
| Ring dot (standard layout) | radius 0.2 p (0.4 p across), stroke 0.4 p | centres 0.30 p apart on the inner ring, about 0.6 p on the outer | Neighbouring printed dots overlap on the inner rings; an unprinted slot between two printed ones is a gap of about 0.2 p |
| Dial ray | 0.2 p wide, long (2.2 p) or short (1.0 p) | about 0.7 to 0.8 p | Read at one radius |

So a ring is a one-dimensional code along an arc whose bits are about a third of a module apart, and each sample is a mixture of three to five neighbouring bits once there is any blur. Reading each slot against a threshold fails long before the modules do.

## What the survey found

Every field that reads dense marks through blur arrived at the same design: fit a forward model to the samples, detect sequences jointly, estimate the channel from known marks, then refine it from decisions.

### One-dimensional barcodes

* **Joseph and Pavlidis (1993–94)** *(abstract only)*: classify on waveform peaks, which survive blur better than edges; estimate the blur width from an edge pair.
* **Esedoglu (2004)**: a two-level signal and a Gaussian blur width are jointly identifiable; total-variation fit. The existence proof that the two-level constraint pins the blur down.
* **Iwen, Santosa and Ward (2013)**: do not reconstruct an image. Model the scan as gain × blur × (dictionary of symbols) and decode symbol by symbol, estimating blur and gain from the known start, middle and end patterns. Works to a blur of about 0.45 to 0.75 bar widths.
* **Gallo and Manduchi (2011)**: no binarisation; the likelihood of each candidate symbol is computed from all grey pixels and integrated over small shifts and scales, then dynamic programming enforces consistent geometry. Reads UPC-A at about 1.2 pixels per module.
* **Kim and Lee (2007)** *(abstract only)*: fit the bars, the blur and a smooth illumination field together.

### Two-dimensional codes

* **van Gennip, Athavale, Gilles and Choksi (2015)**: estimate the point spread function by regularised least squares between the known finder pattern and what was observed, then deconvolve. The closest published method to "inverse filtering by least squares against a known pattern".
* **Sörös et al. (2015)** *(abstract only)*: alternate sharpening, kernel estimation and a decode attempt; the checksum stops the loop and guards against false reads.
* **Cognex patents on under-resolved symbols**: build the module-to-pixel overlap matrix, fix the modules known from finder and timing patterns, then test combinations of neighbouring modules against the pixels. Claims decoding below one pixel per module.
* **Open decoders** (ZXing-cpp, ZBar, Quirc, libdmtx): binarise, then sample a grid. Their only answers to blur are retries with other binarisers and scales. OpenCV's WeChat decoder adds a learned super-resolution step.
* **Commercial SDKs** (Dynamsoft, Scandit, barKoder): publish lists of "deblur modes" and neural models. None publicly describes its point-spread estimate or inverse filter, so claims about their methods are unverified.

### Storage and communications read channels (the closest analogue)

* **Partial-response maximum likelihood**: do not undo the interference; shape it to a short known response and run a Viterbi sequence detector.
* **Turbo equalisation**: the detector and the error-correcting code exchange soft information. With a hard-decision Reed–Solomon code the practical form is: decode what decodes, treat those bits as known, re-detect the rest.
* **Two-dimensional interference** (two-dimensional optical storage, bit-patterned media, holographic storage): stripe-wise Viterbi over a few rows with neighbouring stripes exchanging decisions, iterated.
* **Channel estimation**: least squares on known preambles; per-survivor processing; expectation-maximisation (Baum–Welch) when there is no preamble.

### Text, photography and other fields

* **Slanted-edge method (ISO 12233)**: a known straight edge crossing the pixel grid gives the edge-spread function at sub-pixel resolution; its derivative is the line-spread function. Linearise the tone curve first.
* **Blind deconvolution (Levin et al. 2009)**: fitting image and kernel together favours "no blur"; estimate the kernel alone. Text deblurring (Pan et al. 2014) uses two-tone priors.
* **Crowded-field photometry (DAOPHOT, 1987)**: build an empirical point spread function from isolated stars, then fit all overlapping stars in a group at once. With positions known the fit is linear.
* **Localisation microscopy and sequencing base-callers**: estimate cross-talk between neighbours from the data and invert it; neighbour cross-talk dominates the residual errors.
* **Multi-frame**: lucky imaging (keep the sharpest frames) and super-resolution from hand tremor. For a decoder, summing per-bit likelihoods over frames is simpler than fusing images.

## What build 86 takes from this

| Idea | Source | In the reader |
|---|---|---|
| Channel from known marks | van Gennip; slanted edge; read-channel preambles | Blur measured two ways: from the frame stroke's edge profile, and from how far each module leaks into its eight neighbours (a 2 × 2 covariance, so motion blur has a direction) |
| Geometry belongs in the model | Gallo and Manduchi | A lens term estimated from which distortion makes ring, ray and module samples spread widest; tried before "no lens" |
| Forward model, not a threshold | Iwen; DAOPHOT | A ring sample is modelled as base + gain × (dots and their overlaps or joins, each smeared by the measured blur along that part of the ring) |
| Sequence detection | Partial-response maximum likelihood | Viterbi detection along each ring over a window of up to seven slots, with known slots (Ray Key bearings, Ring Key) fixed |
| Decision-directed channel refinement | Expectation-maximisation; per-survivor processing | After the first detection each ring refits free kernels to its own decisions and detects again, up to six rounds |
| Share the channel between neighbours | Bit-patterned media practice | A ring whose residual stays high is restarted from the kernels a well-converged neighbour learned |
| Decode in the loop | Sörös | Several reading strategies run on one set of samples; Reed–Solomon and CRC-32C choose |
| Cancel neighbour cross-talk | Base-callers; decision feedback | Modules and rays: least-squares leak per direction, subtracted before deciding |

## What was measured

All measurements are simulated frames from the key-test harness; see `CHANGELOG.md` for the before and after comparison.

1. **The information is in the frame.** With the true bits known, a linear model with seven dot terms and eight pair terms explains ring samples to 3 to 5% of their range in frames where thresholding gets 20 to 30% of ring bits wrong, and each bit is then decidable from its own sample. The true sequence has about one sixth of the model cost of the sequence a greedy detector settles on.
2. **Registration was a large part of the ring problem.** A corner-anchored perspective fit leaves the middle of a large symbol 2 to 3 pixels out for a lens coefficient of only −0.02, which bows the frame by a fraction of a pixel but is half a slot on the rings.
3. **The model must have the true geometry.** With the dot radius wrong by a factor of two (and overlaps therefore missing) the sequence detector was no better than a threshold. With the right geometry, in one representative frame (v22, 6.9 pixels per module, blur 0.30 p) ring errors went from 1,355 of 5,702 bits to 5.
4. **Convergence, not capacity, is the remaining limit in such frames.** Rings either converge to almost no errors or stay near 35%; restarting from a neighbour's kernels fixes most of the latter.
5. **Across the hard set the gain is real but partial.** On 141 standard-layout frames that failed with the true corners given, the share with ring errors under 3% rose from 15 to 33. Frames with blur of 0.4 p or more, and frames whose modules also misread (bent surfaces, glare), did not improve.

## Limits

An isolated feature of radius a keeps 1 − exp(−a² / (2σ²)) of its contrast at its centre under a Gaussian blur of width σ. The hardest ring event is a single unprinted slot between two printed dots on the inner ring: a gap about 0.2 p wide, which keeps roughly 37%, 25% and 19% of full contrast at σ = 0.2, 0.3 and 0.4 p. Sequence detection uses the whole footprint and not only the centre, so it reads further than these figures suggest, but its reach falls as 1/σ and linearly with pixels per module. At 2 to 3 pixels per module a slot is under one pixel wide, and no detector recovers what the sampling did not record.

## Not yet tried

In rough order of expected value:

1. **Detect in the pixel domain.** The reader still takes one sample per slot. A matched filter over each mark's blurred footprint, with several samples per slot and across the stroke, uses several times more signal (survey estimate: about 23 grey levels of distance for an isolated dot at 0.2 p and 4 pixels per pitch, against 18 from the centre alone, and without uncancelled leakage).
2. **Use decoded blocks as known data.** Once some Reed–Solomon blocks decode, their bits are known; refit the channel from them and re-detect the rest. This needs byte-grouped placement to be useful, because in the scattered formats every block has bits on every ring.
3. **Soft outputs and a list of candidates.** The detector reports one sequence. Emitting the second-best path per ring, or per-bit likelihood ratios from a forward-backward pass, would give Reed–Solomon better erasures and the CRC more to choose from.
4. **Joint detection across rings and between rings and rays.** Neighbouring rings are 0.86 p apart and are treated as independent.
5. **A parametric camera model** (anisotropic Gaussian, motion length, sharpening gain and radius, tone curve) fitted to the frame edges by nonlinear least squares, replacing the two separate blur measurements.
6. **Multi-frame combining** at the likelihood level.
7. **A learned detector as an extra candidate proposer**, trained on renders from the forward model, guarded by Reed–Solomon and the CRC.

## Sources

One-dimensional codes: Joseph and Pavlidis, doi:10.1109/34.295907; Esedoglu, https://websites.umich.edu/~esedoglu//Papers_Preprints/barcodes_esedoglu.pdf; Iwen, Santosa and Ward, https://arxiv.org/abs/1210.7009; Gallo and Manduchi, https://users.soe.ucsc.edu/~manduchi/papers/barcodes.pdf; Kim and Lee, https://pure.ewha.ac.kr/en/publications/joint-nonuniform-illumination-estimation-and-deblurring-for-bar-c/.

Two-dimensional codes: van Gennip et al., https://arxiv.org/abs/1410.6333; Sörös et al., doi:10.1145/2802083.2808390; Cognex, https://patents.google.com/patent/US10599902; ZXing-cpp, https://github.com/zxing-cpp/zxing-cpp; OpenCV WeChat QR, https://github.com/opencv/opencv_contrib/tree/4.x/modules/wechat_qrcode; Dynamsoft deblur modes, https://www.dynamsoft.com/capture-vision/docs/core/parameters/reference/barcode-reader-task-settings/deblur-modes.html.

Read channels: Cideciyan et al., doi:10.1109/49.124468; Tüchler et al., doi:10.1109/TCOMM.2002.1006557; Singla and O'Sullivan, https://arxiv.org/abs/cs/0509009; Raheli et al., doi:10.1109/26.380054; Shental et al., doi:10.1109/TIT.2008.917638.

Imaging and other fields: Levin et al., https://people.csail.mit.edu/billf/papers/deconvLevinEtalCVPR09.pdf; Pan et al., https://openaccess.thecvf.com/content_cvpr_2014/papers/Pan_Deblurring_Text_Images_2014_CVPR_paper.pdf; Stetson (DAOPHOT), https://articles.adsabs.harvard.edu/pdf/1987PASP...99..191S; Wronski et al., https://arxiv.org/abs/1905.03277; Law et al., https://arxiv.org/abs/astro-ph/0507299.
