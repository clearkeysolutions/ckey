# Branches and releases

| Branch | Who can push | Purpose |
|---|---|---|
| `scratch` | Maintainer only | All changes are written and tested here first. |
| `community` | Anyone (through the maintainer's chosen controls) | Experimental community work. **Unstable:** do not depend on it in production. |
| `Stable-Release` | Maintainer only; anyone may fork | The supported source. Official releases are built and tagged only from here. |

`main` is being renamed by the maintainer, and `community` is being created manually.

## How changes flow

1. **Scratch:** work lands on `scratch`, with every commit versioned (`VERSION`, `CHANGELOG.md`, `CONTEXT.md`) and CI passing.
2. **Stable-Release:** reviewed work is promoted from `scratch` to `Stable-Release`.
3. **Community:** contributors fork or branch from `Stable-Release`, test locally, and propose changes to `community`.

Automation that merges into `community` must not have access to publishing credentials. See the specification, §8.

## Releases

- **Tags:** every release is an immutable tag on a `Stable-Release` commit, such as `v0.1.0-dev.60`. Published tags are never moved or reused.
- **GitHub Releases:** each release has one tag. Its title is the release name.
- **Deprecation:** when a release is deprecated, its title is changed to **`Deprecated - {release name}`**. The tag and its source stay available. Deprecation means "discouraged for new use", not deleted.
- **Version history:** the dev counter is preserved in the version itself (`0.1.0-dev.N`). The exact public-release form, for example `v0.1.0+build.60`, is still an open decision (specification §8.1).

See the [release and versioning map](RELEASE-AND-VERSIONING-MAP.md) for the step-by-step release and deprecation procedure, format and ABI history, and the release table. Each branch has its own changelog: [`CHANGELOG.md`](../CHANGELOG.md) for `scratch`, [`CHANGELOG-STABLE.md`](../CHANGELOG-STABLE.md) for `Stable-Release` and [`CHANGELOG-COMMUNITY.md`](../CHANGELOG-COMMUNITY.md) for `community`.
