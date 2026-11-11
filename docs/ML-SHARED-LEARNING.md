# Shared learning for the ML reader (design, owner decision B, 2026-11-11)

**Status: design. Nothing here is built yet.**

The scanner's ML models (the locator and the site model, `src/ml_locator.inc` and `src/ml_sites.inc`) can improve from real scans without any image or payload leaving a phone. The owner chose option B on 2026-11-11: opt-in shared learning in which only model weight updates leave the device.

## What leaves the phone, and what never does

| Leaves the phone (opt-in only) | Never leaves the phone |
|---|---|
| A clipped, noised weight update for one model version (a few KB) | Camera frames, crops, rectified fields |
| The model version it applies to, the SDK version, the device class (for example "iPhone, A17") | Payloads, URLs, anything decoded |
| How many local examples it was trained on (rounded) | Location, account, contacts, timing of individual scans |

## How a phone learns

1. **Labels come only from verified reads.** A training example exists only when a scan decoded *and* passed Reed–Solomon and CRC-32C. The decoded payload is re-encoded on the device (`ckey_encode_scalable`), which gives the exact value of every site, and the refined corners give the exact geometry. Nothing a person types, and nothing the model itself guessed, is ever a label.
2. **Hard examples are what count.** The phone keeps the frames before a successful read (the ones that did not decode alone, kept in memory only while the code is in view), labels them from the eventual read, and trains on those: the conditions where this phone and its users struggle.
3. **Local training:**
   * a few gradient steps on the last layers, on the device, while the phone is charging and idle;
   * the frames are discarded after the step, and nothing is written to disk except the weight update.
4. **Privacy of the update:**
   * the update is clipped to a fixed norm and Gaussian noise is added (differential privacy, with the budget fixed in the SDK and published);
   * it is sent at most once a day, over TLS, with no account identifier.

## How updates become a model

1. **Aggregation** (openckey.org): the Worker sums updates per model version only once enough phones have contributed (minimum cohort of 1,000), so no single phone's update is ever used or stored alone. Individual updates are deleted after aggregation.
2. **Validation in the harness, every time:**
   * a candidate model is evaluated with `ml/compare_report.py` / `ml/sites_report.py` on fresh-seed sets and on the campaign's golden subset;
   * it must not lower decode rates in any stratum, must keep **0 wrong payloads**, and must not be slower.
   * The untouched holdout is never used for this.
3. **Signing and delivery:**
   * an approved model is signed (Ed25519, the same machinery as Safe Actions) and published as a data asset;
   * the SDK checks the signature before `ckey_ml_load_model`, and the core validates the file completely in any case.
   * A model is data, never code (key-test SPEC: OTA updates deliver approved data or model assets, not executable code).
4. **Rollback:** the SDK keeps the built-in model and the previous approved one, and the server can mark a model as withdrawn.

## Safety argument

A poisoned or simply bad update can only make the model propose worse corners or worse site values. The reader accepts a payload only after Reed–Solomon and CRC-32C, so the worst outcome of a bad model is fewer reads, never a wrong payload. On top of that, the harness gate stops a model with fewer reads before it ships.

## Privacy policy changes needed (openckey.org/privacy and docs/privacy-and-data.md)

* A new section, "Learning from scans (optional)":
  * off by default;
  * the toggle in the app and the SDK flag (`CKeyCameraScanner.contributeToLearning`);
  * what leaves the phone (the table above), and that it carries no identifier;
  * that individual updates are deleted after aggregation;
  * how to turn it off; that turning it off stops uploads at once.
* Data retention: aggregated models are kept; individual updates are not.
* Legal basis: consent (opt-in).

## Build order

1. The trained built-in models (locator, site model), measured in the harness and embedded. **In progress.**
2. On-device fine-tuning in the SDK: Core ML updatable models on iOS, and a C++ path for the last layers elsewhere.
3. The upload endpoint and aggregation in the openckey.org Worker; validation automation in key-test.
4. Signing, delivery, rollback; the privacy policy update ships with step 3.
