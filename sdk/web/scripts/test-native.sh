#!/usr/bin/env bash
# Compiles the Web SDK bridge natively (system C++ compiler, no Emscripten)
# with the same core sources as the wasm build and runs test/native/bridge_test.cpp.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${TMPDIR:-/tmp}/ckey-web-native"
mkdir -p "$out"
CXX="${CXX:-c++}"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wno-unused-function ${CXXFLAGS:-} \
  -I"$root/include" -I"$root/src" \
  "$root/src/ckey_codec.cpp" "$root/src/ckey_scan.cpp" "$root/src/ckey_render.cpp" $( [ -f "$root/src/ckey_certify.cpp" ] && echo "$root/src/ckey_certify.cpp" ) "$root/src/telemetry.cpp" \
  "$here/src/ckey_web.cpp" "$here/test/native/bridge_test.cpp" \
  -pthread -o "$out/bridge_test"
"$out/bridge_test"
