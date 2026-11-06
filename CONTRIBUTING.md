# Contributing

Thank you for helping with CKey.

## Where changes go

| Branch | What it is |
|---|---|
| `Stable-Release` | The supported source. Releases are tagged only here. |
| `community` | Experimental community work. Open pull requests against this branch. |
| `scratch` | The maintainer's working branch. |

1. Fork the repository and branch from `Stable-Release`.
2. Make your change, with tests.
3. Open a pull request against `community`.

The maintainer promotes reviewed work towards `Stable-Release`. See [docs/BRANCHES-AND-RELEASES.md](docs/BRANCHES-AND-RELEASES.md).

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
python3 tools/check_version.py
```

The SDKs have their own builds and tests; see each `sdk/*/README.md`.

## Rules

- **Versioning.** Every change that ships bumps the build number: `python3 tools/bump_version.py`, then add a `CHANGELOG.md` entry. The full rules are in [CONTRIBUTING_VERSIONING.md](CONTRIBUTING_VERSIONING.md).
- **Formats.** A change to geometry, framing, error correction or decoding needs a format-version change and a specification update under `spec/`.
- **Evidence.** Do not claim robustness, speed, capacity or interoperability without results from the benchmark and fixture procedures in `docs/benchmarks/`.
- **Licence.** Contributions are accepted under the MIT licence in [LICENSE](LICENSE). Do not copy code whose licence you have not checked.
