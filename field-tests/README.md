# CKey field tests

<!-- Generated from field-tests/suite.json by tools/field_tests.py. Edit the JSON, not this file. -->

Repeatable checks of how CKey reads in the real world: printed sizes, distance, angle, light, screens, damage, styles, density, small labels, speed and payload safety. Each test states the claim it checks, how to set it up, what to record, and how to grade the result.

Anyone can run a test and report the result, on [https://openckey.org/tests](https://openckey.org/tests) or [here on GitHub](https://github.com/clearkeysolutions/ckey/issues/new?template=field-test-report.yml). Every result is public: on the site, and as JSON at [https://openckey.org/api/field/results](https://openckey.org/api/field/results). Results reported on GitHub are imported to the site, and each one links back to its issue.

Suite version **1.0**. The machine-readable definition is [`suite.json`](suite.json); openckey.org reads it from the release it pins, so the site and this folder always match.

| Test | Category | What it checks |
|---|---|---|
| [FT-01: Smallest printed size](tests/FT-01.md) | Print | A standard-layout CKey holding a short link reads when printed at 1 inch (25 mm) wide. |
| [FT-02: Reading distance](tests/FT-02.md) | Distance | A 2-inch CKey reads from arm's length, about 60 cm. |
| [FT-03: Viewing angle](tests/FT-03.md) | Angle | A CKey reads when seen at up to 45 degrees off square. |
| [FT-04: Lighting](tests/FT-04.md) | Light | A printed CKey reads in daylight, ordinary indoor light, dim light and under a reflection. |
| [FT-05: On a screen](tests/FT-05.md) | Screen | A CKey shown on a phone or computer screen reads, including at low brightness and in light-on-dark colours. |
| [FT-06: Damage](tests/FT-06.md) | Damage | Error correction lets a CKey read through marks, a covered patch and creasing, with more damage tolerated at higher levels. |
| [FT-07: Styles in the real world](tests/FT-07.md) | Design | A style the core certifies reads in practice, and reads about as well as plain black on white. |
| [FT-08: Density](tests/FT-08.md) | Capacity | At a fixed 2-inch print size, denser versions need a closer, steadier camera; version 10 still reads. |
| [FT-09: Small labels: print layout](tests/FT-09.md) | Print | The print layout (Profile 0.1) reads at smaller sizes than the standard layout. |
| [FT-10: Time to read](tests/FT-10.md) | Speed | In ordinary conditions a CKey reads in under 2 seconds from pointing the camera. |
| [FT-11: Payload safety](tests/FT-11.md) | Safety | A reader shows what a code holds and acts only on a tap, and flags content that is not a plain link. |

## Grading

Each test has a rubric with three outcomes. The person running the test chooses the outcome; the measurements are published beside it so anyone can check the grade.

- **Pass:** Meets the pass criterion in the test's rubric.
- **Partial:** Meets the partial criterion but not the pass criterion.
- **Fail:** Meets neither.

## Your own tests

You can also design a test on [https://openckey.org/tests/new](https://openckey.org/tests/new): its purpose, set-up, steps, what to record and its rubric. It is listed at once, marked Community, and anyone can report results for it. A community test that proves useful can be proposed for this suite in a pull request to `suite.json`.

## Photos and privacy

Photos sent with a result on openckey.org are checked before anyone else can see them, and a photo that shows a face waits for a person to review it. See [https://openckey.org/privacy#field-tests](https://openckey.org/privacy#field-tests). Photos attached to a GitHub issue are published by GitHub under its own terms.

## Changing the suite

Edit `suite.json`, run `python3 tools/field_tests.py` to regenerate this folder and the issue form, and commit both. CI runs `--check`, which fails when they differ.
