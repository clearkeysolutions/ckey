# Branches and releases

| Branch | Who can push | Purpose |
|---|---|---|
| `scratch` | Maintainer only | All changes are written and tested here first. |
| `community` | Anyone (through the maintainer's chosen controls) | Experimental community work. **Unstable:** do not depend on it in production. |
| `Stable-Release` | Maintainer only; anyone may fork | The supported source. Official releases are built and tagged only from here. |


## How changes flow

1. **Scratch:** work lands on `scratch`, with every commit versioned (`VERSION`, `CHANGELOG.md`, `CONTEXT.md`) and CI passing. `CONTEXT.md`, the engineering record, lives only on `scratch`.
2. **Stable-Release:** reviewed work is promoted from `scratch` to `Stable-Release`.
3. **Community:** contributors fork or branch from `Stable-Release`, test locally, and propose changes to `community`.

Automation that merges into `community` must not have access to publishing credentials. See the specification, §8.

## Releases

- **Tags:** every release is an immutable tag on a `Stable-Release` commit, such as `v0.1.0-build.61`. Published tags are never moved or reused.
- **GitHub Releases:** each release has one tag. Its title is the release name.
- **Deprecation:** when a release is deprecated, its title is changed to **`Deprecated - {release name}`**. The tag and its source stay available. Deprecation means "discouraged for new use", not deleted.
- **Version history:** the build counter is part of the version itself, `0.1.0-build.N`. Tags are `v` + `VERSION`, for example `v0.1.0-build.61`. Builds 1–60 were named `0.1.0-dev.N`.

See the [release and versioning map](RELEASE-AND-VERSIONING-MAP.md) for the step-by-step release and deprecation procedure, format and ABI history, and the release table. Each branch has its own changelog: [`CHANGELOG.md`](../CHANGELOG.md) for `scratch`, [`CHANGELOG-STABLE.md`](../CHANGELOG-STABLE.md) for `Stable-Release` and [`CHANGELOG-COMMUNITY.md`](../CHANGELOG-COMMUNITY.md) for `community`.
