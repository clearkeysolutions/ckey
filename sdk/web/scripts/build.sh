#!/usr/bin/env bash
# Builds the CKey Web SDK into dist/: the core + bridge compiled with
# Emscripten (emcmake/em++ must be on PATH, e.g. after `source emsdk_env.sh`),
# then the JS wrapper and TypeScript declarations copied next to it.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
build="$here/build/wasm"
if ! command -v emcmake >/dev/null 2>&1; then
  echo "error: emcmake not found. Install and activate emsdk (https://emscripten.org/docs/getting_started/downloads.html)." >&2
  exit 1
fi
emcmake cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel
node "$here/scripts/assemble.mjs" "$build"
