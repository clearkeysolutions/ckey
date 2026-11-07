#!/usr/bin/env python3
"""Validates field-tests/suite.json and generates the files made from it.

    python3 tools/field_tests.py          # validate and write
    python3 tools/field_tests.py --check  # validate; fail if a generated file is stale
    python3 tools/field_tests.py --verify-with build/ckey  # encode every test code

Generated: field-tests/README.md, field-tests/tests/<id>.md and
.github/ISSUE_TEMPLATE/field-test-report.yml. openckey.org reads the same
suite.json from the release it pins, so the site and GitHub always describe
the same tests.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "field-tests" / "suite.json"
SITE = "https://openckey.org"
REPO_BLOB = "https://github.com/clearkeysolutions/ckey/blob/Stable-Release/"
FIELD_TYPES = {"text", "number", "boolean", "choice"}
LEVELS = {"L", "M", "Q", "H"}
LAYOUTS = {"standard", "print"}
STYLE_KEYS = {"ink", "background", "accents", "accent_percent", "rings", "modules", "rays"}


def fail(message):
    sys.exit("field-tests: " + message)


def check_field(field, where, allow_trials):
    for key in ("id", "label", "type"):
        if not isinstance(field.get(key), str) or not field[key]:
            fail(f"{where}: missing {key}")
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", field["id"]):
        fail(f"{where}: id {field['id']!r} must be lower_snake_case")
    if field["type"] not in FIELD_TYPES:
        fail(f"{where}: type must be one of {sorted(FIELD_TYPES)}")
    if field["type"] == "choice" and not (isinstance(field.get("options"), list) and field["options"]):
        fail(f"{where}: a choice needs options")
    if "trials" in field:
        if not allow_trials:
            fail(f"{where}: environment fields cannot have trials")
        trials = field["trials"]
        if not (isinstance(trials, list) and trials and len(set(trials)) == len(trials)):
            fail(f"{where}: trials must be a non-empty list of distinct labels")
    known = {"id", "label", "type", "required", "help", "options", "unit", "min", "max", "trials"}
    extra = set(field) - known
    if extra:
        fail(f"{where}: unknown keys {sorted(extra)}")


def validate(suite):
    if suite.get("schema") != 1:
        fail("schema must be 1")
    for key in ("suite_version", "title", "summary"):
        if not isinstance(suite.get(key), str):
            fail(f"missing {key}")
    ids = set()
    for field in suite["environment"]:
        check_field(field, "environment." + str(field.get("id")), False)
    styles = suite.get("styles", {})
    for name, style in styles.items():
        extra = set(style.get("ckey_style", {})) - STYLE_KEYS
        if extra:
            fail(f"style {name}: unknown ckey_style keys {sorted(extra)}")
    for test in suite["tests"]:
        tid = test.get("id", "")
        if not re.fullmatch(r"FT-\d{2}", tid) or tid in ids:
            fail(f"test id {tid!r} must be FT-NN and unique")
        ids.add(tid)
        for key in ("title", "category", "claim", "rationale"):
            if not isinstance(test.get(key), str) or not test[key]:
                fail(f"{tid}: missing {key}")
        for key in ("setup", "steps"):
            if not (isinstance(test.get(key), list) and test[key]):
                fail(f"{tid}: {key} must be a non-empty list")
        if set(test.get("rubric", {})) != {"pass", "partial", "fail"}:
            fail(f"{tid}: rubric must define pass, partial and fail")
        labels = set()
        for code in test["codes"]:
            label = code.get("label")
            if label in labels:
                fail(f"{tid}: duplicate code label {label}")
            labels.add(label)
            if code.get("level") not in LEVELS or code.get("layout") not in LAYOUTS:
                fail(f"{tid} code {label}: bad level or layout")
            if code.get("style") not in styles:
                fail(f"{tid} code {label}: unknown style {code.get('style')!r}")
            size = code.get("size_in")
            if not isinstance(size, (int, float)) or not 0.1 <= size <= 8:
                fail(f"{tid} code {label}: size_in must be 0.1 to 8")
            version = code.get("version", 0)
            if not isinstance(version, int) or not 0 <= version <= 31:
                fail(f"{tid} code {label}: version must be 0 (auto) to 31")
            if not isinstance(code.get("payload"), str) or not code["payload"]:
                fail(f"{tid} code {label}: missing payload")
        mids = set()
        for field in test["measurements"]:
            check_field(field, f"{tid}.{field.get('id')}", True)
            if field["id"] in mids:
                fail(f"{tid}: duplicate measurement {field['id']}")
            mids.add(field["id"])


def test_md(suite, test):
    tid = test["id"]
    lines = [f"# {tid}: {test['title']}", "",
             f"<!-- Generated from field-tests/suite.json by tools/field_tests.py. Edit the JSON, not this file. -->", "",
             f"**Category:** {test['category']} · **Time:** about {test.get('time_minutes', 10)} minutes · "
             f"**Run it and see results:** [{SITE}/tests/test?id={tid}]({SITE}/tests/test?id={tid})", "",
             "## What it checks", "", test["claim"], "", "## Why", "", test["rationale"], "",
             "## Test codes", "",
             f"Print or open the sheet at [{SITE}/tests/sheet?id={tid}]({SITE}/tests/sheet?id={tid}). It is drawn by the CKey web SDK from the values below.", "",
             "| Code | Size | Version | Level | Layout | Style | Holds |", "|---|---|---|---|---|---|---|"]
    for code in test["codes"]:
        version = code.get("version") or "smallest that fits"
        lines.append(f"| {code['label']} | {code['size_in']} in | {version} | {code['level']} | {code['layout']} | {code['style']} | `{code['payload']}` |")
    lines += ["", "## Set up", ""] + [f"{i}. {s}" for i, s in enumerate(test["setup"], 1)]
    lines += ["", "## Steps", ""] + [f"{i}. {s}" for i, s in enumerate(test["steps"], 1)]
    lines += ["", "## What to record", ""]
    for field in test["measurements"]:
        unit = f" ({field['unit']})" if field.get("unit") else ""
        req = "required" if field.get("required") else "optional"
        trials = f" For each of: {', '.join(field['trials'])}." if field.get("trials") else ""
        lines.append(f"- **{field['label']}**{unit}, {field['type']}, {req}.{trials}")
    lines += ["", "Every report also records the device, operating system, reader and its version, and the printer or screen.", "",
              "## How to grade it", "",
              f"- **Pass:** {test['rubric']['pass']}", f"- **Partial:** {test['rubric']['partial']}", f"- **Fail:** {test['rubric']['fail']}", "",
              "## Report a result", "",
              f"- On openckey.org: [{SITE}/tests/test?id={tid}]({SITE}/tests/test?id={tid}), with optional photos.",
              f"- On GitHub: [open a field-test report]({suite['report_on_github']}&title=%5B{tid}%5D+) and choose {tid}.", "",
              f"Both are published together at [{SITE}/tests]({SITE}/tests) and as JSON at [{suite['results_json']}]({suite['results_json']}).", ""]
    return "\n".join(lines)


def readme_md(suite):
    lines = [f"# {suite['title']}", "",
             "<!-- Generated from field-tests/suite.json by tools/field_tests.py. Edit the JSON, not this file. -->", "",
             suite["summary"], "",
             f"Anyone can run a test and report the result, on [{SITE}/tests]({SITE}/tests) or [here on GitHub]({suite['report_on_github']}). "
             f"Every result is public: on the site, and as JSON at [{suite['results_json']}]({suite['results_json']}). "
             "Results reported on GitHub are imported to the site, and each one links back to its issue.", "",
             f"Suite version **{suite['suite_version']}**. The machine-readable definition is [`suite.json`](suite.json); "
             "openckey.org reads it from the release it pins, so the site and this folder always match.", "",
             "| Test | Category | What it checks |", "|---|---|---|"]
    for test in suite["tests"]:
        lines.append(f"| [{test['id']}: {test['title']}](tests/{test['id']}.md) | {test['category']} | {test['claim']} |")
    lines += ["", "## Grading", "",
              "Each test has a rubric with three outcomes. The person running the test chooses the outcome; the measurements are published beside it so anyone can check the grade.", ""]
    for key in ("pass", "partial", "fail"):
        lines.append(f"- **{key.capitalize()}:** {suite['outcomes'][key]}")
    lines += ["", "## Your own tests", "",
              f"You can also design a test on [{SITE}/tests/new]({SITE}/tests/new): its purpose, set-up, steps, what to record and its rubric. "
              "It is listed at once, marked Community, and anyone can report results for it. "
              "A community test that proves useful can be proposed for this suite in a pull request to `suite.json`.", "",
              "## Photos and privacy", "",
              "Photos sent with a result on openckey.org are checked before anyone else can see them, and a photo that shows a face waits for a person to review it. "
              f"See [{SITE}/privacy#field-tests]({SITE}/privacy#field-tests). Photos attached to a GitHub issue are published by GitHub under its own terms.", "",
              "## Changing the suite", "",
              "Edit `suite.json`, run `python3 tools/field_tests.py` to regenerate this folder and the issue form, and commit both. CI runs `--check`, which fails when they differ.", ""]
    return "\n".join(lines)


def issue_form(suite):
    def q(s):
        return json.dumps(s, ensure_ascii=False)
    options = [f"{t['id']} {t['title']}" for t in suite["tests"]] + ["Community test (give its ID below)"]
    env = {f["id"]: f for f in suite["environment"]}
    out = ["# Generated from field-tests/suite.json by tools/field_tests.py. Edit the JSON, not this file.",
           "name: Field-test report",
           "description: Report the result of a CKey field test. Results are imported to openckey.org/tests.",
           "title: \"[FT-] \"",
           "labels: [\"field-test\"]",
           "body:",
           "  - type: markdown",
           "    attributes:",
           "      value: " + q(f"Run a test from {SITE}/tests (each test page has its set-up, steps, sheet and rubric), then report it here. This report is public and is copied to {SITE}/tests with a link back to this issue."),
           "  - type: dropdown", "    id: test", "    attributes:", "      label: " + q('Test'), "      options:"]
    out += [f"        - {q(o)}" for o in options]
    out += ["    validations:", "      required: true",
            "  - type: input", "    id: community_test", "    attributes:", "      label: " + q('Community test ID'),
            "      description: " + q('Only for a community test, for example C-7Q2K9P.'), "    validations:", "      required: false"]
    for fid in ("device", "os", "reader_version", "medium"):
        f = env[fid]
        out += ["  - type: input", f"    id: {fid}", "    attributes:", f"      label: {q(f['label'])}"]
        if f.get("help"):
            out.append(f"      description: {q(f['help'])}")
        out += ["    validations:", f"      required: {'true' if f.get('required') else 'false'}"]
    out += ["  - type: dropdown", "    id: reader", "    attributes:", f"      label: {q(env['reader']['label'])}", "      options:"]
    out += [f"        - {q(o)}" for o in env["reader"]["options"]]
    out += ["    validations:", "      required: true",
            "  - type: textarea", "    id: results", "    attributes:", "      label: " + q('Measurements'),
            "      description: " + q('One line per trial, as the test page lists them, for example `A: read, 1.2 s` or `60 cm: no`.'),
            "    validations:", "      required: true",
            "  - type: dropdown", "    id: outcome", "    attributes:", "      label: " + q('Outcome'),
            "      description: " + q("Grade it with the test's rubric."), "      options:",
            "        - \"pass\"", "        - \"partial\"", "        - \"fail\"", "    validations:", "      required: true",
            "  - type: textarea", "    id: notes", "    attributes:", "      label: " + q('Notes and photos'),
            "      description: " + q('Anything else worth knowing. You can attach photos; do not include faces or personal information.'),
            "    validations:", "      required: false", ""]
    return "\n".join(out)


def verify_codes(suite, cli):
    """Encodes every test code with the ckey command-line tool, so a payload that no longer fits its version fails CI."""
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        for test in suite["tests"]:
            for code in test["codes"]:
                args = [cli, "encode-scalable", "--utf8", code["payload"], "--level", code["level"],
                        "--layout", code["layout"], "--out", str(Path(tmp) / "code.svg")]
                if code.get("version"):
                    args += ["--version", str(code["version"])]
                result = subprocess.run(args, capture_output=True, text=True)
                if result.returncode != 0:
                    fail(f"{test['id']} code {code['label']} does not encode: {(result.stderr or result.stdout).strip()}")
    print("field-tests: every test code encodes")


def main():
    suite = json.loads(SUITE.read_text())
    validate(suite)
    if "--verify-with" in sys.argv:
        verify_codes(suite, sys.argv[sys.argv.index("--verify-with") + 1])
        return
    outputs = {ROOT / "field-tests" / "README.md": readme_md(suite),
               ROOT / ".github" / "ISSUE_TEMPLATE" / "field-test-report.yml": issue_form(suite)}
    for test in suite["tests"]:
        outputs[ROOT / "field-tests" / "tests" / f"{test['id']}.md"] = test_md(suite, test)
    expected = {p.name for p in outputs if p.parent.name == "tests"}
    stale_extra = [p for p in (ROOT / "field-tests" / "tests").glob("*.md") if p.name not in expected]
    if "--check" in sys.argv:
        stale = [str(p.relative_to(ROOT)) for p, text in outputs.items() if not p.exists() or p.read_text() != text]
        stale += [str(p.relative_to(ROOT)) for p in stale_extra]
        if stale:
            fail("generated files are stale; run python3 tools/field_tests.py: " + ", ".join(stale))
        print(f"field-tests: {len(suite['tests'])} tests valid; generated files current")
        return
    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    for path in stale_extra:
        path.unlink()
    print(f"field-tests: wrote {len(outputs)} files for {len(suite['tests'])} tests")


if __name__ == "__main__":
    main()
