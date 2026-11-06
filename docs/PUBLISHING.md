# Publishing a release

A release is published in two steps:

1. **Build (automatic).** Pushing a tag `v0.1.0-build.N` on `Stable-Release` runs the Release workflow. It checks the tag against `VERSION`, runs the full test suite, builds every package on Linux, macOS and Windows, signs build provenance with Sigstore, and attaches everything to a **draft** GitHub Release with `SHA256SUMS`. Nothing is published yet.
2. **Publish (on the maintainer's machine).** `tools/release/publish.sh <tag>` downloads that draft, verifies every checksum and provenance attestation, and publishes the same bytes to each registry. It rebuilds nothing.

```sh
tools/release/publish.sh v0.1.0-build.83                 # every registry, then un-draft the release
tools/release/publish.sh v0.1.0-build.83 npm nuget       # only some
```

Targets: `npm`, `nuget`, `maven`, `homebrew`, `release`. Swift needs no publishing: the tag is the release.

## One-time setup

Tools on the Mac:

```sh
brew install gh gnupg node
brew install --cask dotnet-sdk
gh auth login
```

### npm: package `openckey`

1. Create an account at <https://www.npmjs.com/signup> and turn on two-factor authentication.
2. `npm login` on the Mac.
3. The package is `openckey`. The unscoped name `ckey` belongs to an unrelated 2018 package, so it cannot be used.

### NuGet: packages `CKey` and `CKey.Wpf`

1. Sign in at <https://www.nuget.org> with a Microsoft account.
2. **API Keys → Create**: scope *Push new packages and package versions*, glob `CKey*`.
3. `export NUGET_API_KEY=…` in the shell before running the script. Do not save it in the repository.

### Maven Central: `org.openckey:ckey`, `ckey-compose`, `ckey-camerax`

1. Create an account at <https://central.sonatype.com>.
2. **Namespaces → Add namespace** `org.openckey`. Central shows a verification key; add it as a DNS TXT record on `openckey.org` (Cloudflare → DNS → Add record → TXT, name `@`). Press **Verify** once it resolves.
3. **Account → Generate User Token.** Then `export MAVEN_CENTRAL_TOKEN=$(printf '%s:%s' '<username>' '<password>' | base64)`.
4. Create a signing key and publish its public half:

   ```sh
   gpg --quick-generate-key "ClearKey Solutions <you@example.com>" ed25519 sign 3y
   gpg --list-keys --keyid-format long            # note the key ID
   gpg --keyserver keyserver.ubuntu.com --send-keys <KEYID>
   export GPG_KEY_ID=<KEYID>
   ```

5. The script uploads a signed bundle as a *user-managed* deployment. Open <https://central.sonatype.com/publishing/deployments>, check the validation, and press **Publish**. Artifacts appear on Maven Central within about 30 minutes.

### Homebrew: `brew install clearkeysolutions/ckey/ckey`

1. Create the public repository `clearkeysolutions/homebrew-ckey` on GitHub with an empty `Formula/` folder.
2. The script copies the release's `ckey.rb` into `Formula/ckey.rb` and pushes it. The formula builds the CLI from the tagged source archive and checks its SHA-256.

### Swift

Nothing to publish. SwiftPM resolves the tag directly: `.package(url: "https://github.com/clearkeysolutions/ckey", from: "0.1.0-build.83")`. Optionally add the package at <https://swiftpackageindex.com/add-a-package> so it can be found.

## Cutting a release

1. On `scratch`: `python3 tools/bump_version.py`, add the `CHANGELOG.md` entry, and run the full test suite and `python3 tools/check_version.py`.
2. Promote to `Stable-Release` (`CONTEXT.md` stays on `scratch`) and add the release to `CHANGELOG-STABLE.md`.
3. Tag the `Stable-Release` commit: `git tag -s v0.1.0-build.N -m "CKey 0.1.0-build.N"` and `git push origin v0.1.0-build.N`.
4. Wait for the Release workflow's draft, then run `tools/release/publish.sh v0.1.0-build.N`.
5. Bring `community` level with `Stable-Release`.

The full procedure, including deprecation, is in the [release and versioning map](RELEASE-AND-VERSIONING-MAP.md).

## Security rules

- Tokens live only in the maintainer's shell (or a password manager), never in the repository and never in a workflow that runs for `community`.
- Published versions are permanent. npm, NuGet and Maven Central do not allow a version to be replaced; a bad release is fixed by the next build number and the old one is deprecated.
