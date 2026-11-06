# Repository settings

The repository is locked down in two places: in the files (pinned dependencies, code owners) and in GitHub's settings, which only an owner can change. This page lists both.

## In the repository

- **Pinned actions.** Every GitHub Action in `.github/workflows/` is pinned to a full commit SHA, with its version tag as a comment (`actions/checkout@<sha> # v4`). A moved tag cannot change what runs. To upgrade an action, look up the new tag's commit (`git ls-remote https://github.com/<owner>/<repo> refs/tags/<tag>^{}`) and update the SHA and the comment together.
- **Pinned toolchains.** The Android build pins Gradle, the NDK and CMake; the web build pins emsdk; the Swift package and the CMake build compile the core from this repository.
- **Code owners.** `.github/CODEOWNERS` makes the maintainer a required reviewer for every path.
- **Immutable releases.** Release tags (`v0.1.0-build.N`) are made only on `Stable-Release` and are never moved or reused ([branches and releases](BRANCHES-AND-RELEASES.md)).

## In GitHub (owner only)

**Settings → General**

- Default branch: `Stable-Release`.
- Pull requests: allow squash merging only; enable **Automatically delete head branches**.

**Settings → Rules → Rulesets → New branch ruleset**

1. **Stable-Release**: target `Stable-Release`. Enable:
   - Restrict updates, creations and deletions (bypass: repository admins only);
   - Block force pushes;
   - Require linear history;
   - Require a pull request before merging, with 1 approval and code-owner review;
   - Require status checks to pass: the `ci` workflow's jobs.
2. **scratch**: target `scratch`. Enable Restrict updates (bypass: repository admins only) and Block force pushes.
3. **community**: target `community`. Enable Require a pull request before merging with 1 approval, Block force pushes and Restrict deletions.

**Settings → Rules → Rulesets → New tag ruleset**

- Target `v*`. Enable Restrict creations, Restrict updates and Restrict deletions (bypass: repository admins only), so a published tag can never be moved or removed.

**Settings → Code security**

- Private vulnerability reporting: on ([SECURITY.md](../SECURITY.md)).
- Dependabot alerts: on.
- Secret scanning and push protection: on.

**Settings → Actions → General**

- Workflow permissions: **Read repository contents** by default. The release workflow asks for the extra permissions it needs itself.
- Fork pull request workflows: require approval for first-time contributors.

**Settings → Environments**

- Create a `release` environment that only `v*` tags can deploy to, and keep any publishing tokens there, never as repository-wide secrets. Workflows that run for `community` must not see them.
