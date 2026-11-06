# Security

CKey reads untrusted images and untrusted bytes, so parser and decoder bugs are security bugs.

## Reporting a problem

Report a suspected vulnerability privately through GitHub: **Security → Report a vulnerability** on this repository. Please include:

- the CKey version (`VERSION`, or `ckey_software_version()`),
- the platform and package (C library, npm `ckey`, NuGet `CKey`, Maven `org.openckey:ckey`, Swift package, CLI),
- the smallest input that shows the problem (an image, a `.ckey` file or the payload bytes).

Do not open a public issue for a vulnerability. You will get a reply within seven days.

## Scope

In scope: the C core (`src/`, `include/`), the `.ckey` reader, the SDKs under `sdk/`, the CLI, and the release artifacts built from `Stable-Release`.

Out of scope: openckey.org, which is a separate service with its own reporting route at <https://openckey.org/report>.

## Supported versions

Only the newest release tag on `Stable-Release` receives fixes. `scratch` and `community` are unreleased work.
