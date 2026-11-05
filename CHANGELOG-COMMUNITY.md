# Community changelog

This log lists changes merged into the `community` branch, newest first. `community` is open to everyone and is **unstable**. Nothing here is a release, and nothing here is supported until the maintainer brings it into `scratch` and promotes it to `Stable-Release`.

## How to add an entry

Every pull request into `community` adds one entry to the top of the list below:

```markdown
### YYYY-MM-DD — short title (#PR)

* **Author:** @handle
* **Based on:** Stable-Release tag `v0.1.0-build.N`
* **Changes:** …
* **Formats or ABI touched:** none  <!-- a community change must not claim a new format byte or ABI number; propose it instead -->
* **Tests:** what was run, and the result
```

Rules:

* Do not edit `VERSION` or `CHANGELOG.md`. The maintainer assigns build numbers when a change moves to `scratch`.
* Do not claim new format bytes or ABI numbers. Format and ABI numbers are allocated only on `scratch`, so two contributors can never pick the same one.
* When the maintainer adopts a change, its entry here gets a line `Adopted in 0.1.0-build.M`.

## Changes

_None yet._
