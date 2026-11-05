# CKey Web SDK: the C core plus the bridge (src/ckey_web.cpp) compiled to
# WebAssembly with Emscripten. Configure with emcmake:
#   emcmake cmake -S sdk/web -B sdk/web/build/wasm -DCMAKE_BUILD_TYPE=Release
#   cmake --build sdk/web/build/wasm
# then scripts/assemble.mjs copies ckey-core.mjs/.wasm and the JS wrapper into
# dist/ (scripts/build.sh does all three steps).
cmake_minimum_required(VERSION 3.20)
project(ckey_web LANGUAGES CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "sdk/web must be configured with emcmake (Emscripten); see scripts/build.sh")
endif()

set(CKEY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(CKEY_WEB_SOURCES
  "${CKEY_ROOT}/src/ckey_codec.cpp"
  "${CKEY_ROOT}/src/ckey_scan.cpp"
  "${CKEY_ROOT}/src/ckey_render.cpp"
  "${CKEY_ROOT}/src/telemetry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ckey_web.cpp")
# Optical certification (ABI 4 cores) is bound when the core ships it.
if(EXISTS "${CKEY_ROOT}/src/ckey_certify.cpp")
  list(APPEND CKEY_WEB_SOURCES "${CKEY_ROOT}/src/ckey_certify.cpp")
endif()

add_executable(ckey-core ${CKEY_WEB_SOURCES})
target_compile_features(ckey-core PRIVATE cxx_std_20)
target_include_directories(ckey-core PRIVATE "${CKEY_ROOT}/include" "${CKEY_ROOT}/src")
# Each core library compiles only the shared internals it calls.
target_compile_options(ckey-core PRIVATE -O3 -Wno-unused-function)
set_target_properties(ckey-core PROPERTIES SUFFIX ".mjs")
target_link_options(ckey-core PRIVATE
  -O3
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createCKeyCore
  -sFILESYSTEM=0
  -sALLOW_MEMORY_GROWTH=1
  -sENVIRONMENT=web,worker,node
  -sEXPORTED_RUNTIME_METHODS=HEAPU8
  -sDYNAMIC_EXECUTION=0
  -sSTACK_SIZE=2097152
  -sINITIAL_MEMORY=33554432)
