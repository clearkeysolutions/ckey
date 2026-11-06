#!/usr/bin/env bash
# Publishes one CKey release to every registry, from the files the Release
# workflow attached to its draft GitHub Release. Nothing is rebuilt here: the
# exact bytes in SHA256SUMS are what get published.
#
#   tools/release/publish.sh v0.1.0-build.82            # everything
#   tools/release/publish.sh v0.1.0-build.82 npm nuget  # only some
#
# Targets: npm, nuget, maven, homebrew, release (un-drafts the GitHub Release).
# Account setup and the environment variables each target needs are in
# docs/PUBLISHING.md. Run it on the maintainer's machine; never in CI for
# `community`.
set -euo pipefail

tag="${1:?usage: publish.sh <tag> [npm nuget maven homebrew release]}"
shift || true
targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=(npm nuget maven homebrew release)
version="${tag#v}"
repo="clearkeysolutions/ckey"
tap_repo="clearkeysolutions/homebrew-ckey"

need() { command -v "$1" >/dev/null || { echo "missing tool: $1 ($2)"; exit 1; }; }
want() { local t; for t in "${targets[@]}"; do [ "$t" = "$1" ] && return 0; done; return 1; }
need gh "brew install gh, then gh auth login"
need shasum "part of macOS"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "==> Downloading the assets of ${tag}"
gh release download "$tag" --repo "$repo" --dir "$work"
cd "$work"
echo "==> Verifying SHA256SUMS"
shasum -a 256 -c SHA256SUMS
if gh attestation verify --help >/dev/null 2>&1; then
  for f in *; do
    [ "$f" = SHA256SUMS ] && continue
    gh attestation verify "$f" --repo "$repo" >/dev/null && echo "provenance ok: $f"
  done
fi

if want npm; then
  need npm "install Node.js 22"
  echo "==> npm"
  tgz=$(ls ckey-*.tgz | head -1)
  # A prerelease needs an explicit dist-tag; until 1.0 every build is "latest".
  npm publish "$tgz" --access public --tag latest
fi

if want nuget; then
  need dotnet "install the .NET 8 SDK"
  : "${NUGET_API_KEY:?set NUGET_API_KEY (docs/PUBLISHING.md)}"
  echo "==> NuGet"
  for pkg in *.nupkg; do
    dotnet nuget push "$pkg" --source https://api.nuget.org/v3/index.json --api-key "$NUGET_API_KEY" --skip-duplicate
  done
fi

if want maven; then
  need gpg "brew install gnupg"
  : "${MAVEN_CENTRAL_TOKEN:?set MAVEN_CENTRAL_TOKEN to base64 of user:password from central.sonatype.com (docs/PUBLISHING.md)}"
  echo "==> Maven Central"
  mkdir bundle
  (cd bundle && unzip -q "../ckey-android-maven-${version}.zip")
  # Central requires a -javadoc.jar beside every artifact. The Android modules
  # ship sources jars; where no javadoc jar exists, add the placeholder Central
  # accepts (a jar holding a README that points to the sources and docs).
  find bundle -name '*.pom' | while read -r pom; do
    base="$PWD/${pom%.pom}"
    if [ ! -f "${base}-javadoc.jar" ]; then
      tmp="$(mktemp -d)"
      printf 'API documentation: https://github.com/clearkeysolutions/ckey/tree/Stable-Release/sdk/android\nThe sources jar beside this file holds the documented Kotlin sources.\n' > "$tmp/README.txt"
      (cd "$tmp" && zip -q "${base}-javadoc.jar" README.txt)
      rm -rf "$tmp"
    fi
  done
  # Central requires a signature and MD5/SHA-1 checksums for every file.
  find bundle -type f ! -name '*.asc' ! -name '*.md5' ! -name '*.sha1' ! -name '*.sha256' ! -name '*.sha512' \
    ! -name 'maven-metadata*' | while read -r f; do
      gpg --batch --yes --armor --detach-sign ${GPG_KEY_ID:+--local-user "$GPG_KEY_ID"} "$f"
      md5 -q "$f" > "$f.md5"
      shasum -a 1 "$f" | cut -d' ' -f1 > "$f.sha1"
    done
  find bundle -name 'maven-metadata*' -delete
  (cd bundle && zip -qr ../central-bundle.zip .)
  curl -fsS -X POST "https://central.sonatype.com/api/v1/publisher/upload?name=ckey-${version}&publishingType=USER_MANAGED" \
    -H "Authorization: Bearer ${MAVEN_CENTRAL_TOKEN}" -F "bundle=@central-bundle.zip"
  echo
  echo "Uploaded. Review and press Publish at https://central.sonatype.com/publishing/deployments"
fi

if want homebrew; then
  need git "part of the Xcode command line tools"
  echo "==> Homebrew tap"
  git clone -q "https://github.com/${tap_repo}.git" tap
  mkdir -p tap/Formula
  cp ckey.rb tap/Formula/ckey.rb
  (cd tap && git add Formula/ckey.rb && git commit -qm "ckey ${version}" && git push -q origin HEAD)
  echo "Formula published: brew install clearkeysolutions/ckey/ckey"
fi

if want release; then
  echo "==> Publishing the GitHub Release"
  gh release edit "$tag" --repo "$repo" --draft=false --latest
fi

echo "==> Done: ${targets[*]}"
