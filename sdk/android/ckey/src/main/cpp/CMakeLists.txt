# Native part of org.openckey:ckey: libckey_jni.so.
#
# Compiles the repository's core sources in place (by relative path; nothing
# is copied) plus the JNI bridge, into one shared library that exports only
# JNI_OnLoad and the Java_org_openckey_ckey_CKeyNative_* functions.
#
# Module switches (Gradle properties ckey.native.scan / .render / .telemetry,
# all true for the published AAR):
#   CKEY_ANDROID_SCAN       ckey_scan.cpp      (decoding; scan-only apps keep this)
#   CKEY_ANDROID_RENDER     ckey_render.cpp    (render-only apps keep this)
#   CKEY_ANDROID_TELEMETRY  telemetry.cpp      (OS-vendor builds may drop it)
# ckey_codec.cpp is always built. Certification (ckey_certify.cpp) is added
# automatically when the core has it and both scan and render are on.
cmake_minimum_required(VERSION 3.22.1)
project(ckey_android LANGUAGES CXX)

option(CKEY_ANDROID_SCAN "Include the scanner" ON)
option(CKEY_ANDROID_RENDER "Include the renderer" ON)
option(CKEY_ANDROID_TELEMETRY "Include the telemetry core" ON)

# src/main/cpp -> repository root (six levels up).
get_filename_component(CKEY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)
set(CKEY_SRC "${CKEY_REPO_ROOT}/src")
if(NOT EXISTS "${CKEY_SRC}/ckey_codec.cpp")
  message(FATAL_ERROR "CKey core sources not found at ${CKEY_SRC}; build from the repository checkout.")
endif()

set(CKEY_SOURCES "${CKEY_SRC}/ckey_codec.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ckey_jni.cpp")
set(CKEY_DEFS "")
if(CKEY_ANDROID_SCAN)
  list(APPEND CKEY_SOURCES "${CKEY_SRC}/ckey_scan.cpp")
  list(APPEND CKEY_DEFS CKEY_JNI_SCAN=1)
else()
  list(APPEND CKEY_DEFS CKEY_JNI_SCAN=0)
endif()
if(CKEY_ANDROID_RENDER)
  list(APPEND CKEY_SOURCES "${CKEY_SRC}/ckey_render.cpp")
  list(APPEND CKEY_DEFS CKEY_JNI_RENDER=1)
else()
  list(APPEND CKEY_DEFS CKEY_JNI_RENDER=0)
endif()
if(CKEY_ANDROID_TELEMETRY)
  list(APPEND CKEY_SOURCES "${CKEY_SRC}/telemetry.cpp")
  list(APPEND CKEY_DEFS CKEY_JNI_TELEMETRY=1)
else()
  list(APPEND CKEY_DEFS CKEY_JNI_TELEMETRY=0)
endif()
if(CKEY_ANDROID_SCAN AND CKEY_ANDROID_RENDER AND EXISTS "${CKEY_SRC}/ckey_certify.cpp")
  list(APPEND CKEY_SOURCES "${CKEY_SRC}/ckey_certify.cpp")
  list(APPEND CKEY_DEFS CKEY_JNI_CERTIFY=1)
else()
  list(APPEND CKEY_DEFS CKEY_JNI_CERTIFY=0)
endif()

add_library(ckey_jni SHARED ${CKEY_SOURCES})
target_compile_features(ckey_jni PRIVATE cxx_std_20)
target_include_directories(ckey_jni PRIVATE "${CKEY_REPO_ROOT}/include" "${CKEY_SRC}")
target_compile_definitions(ckey_jni PRIVATE ${CKEY_DEFS})
set_target_properties(ckey_jni PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_options(ckey_jni PRIVATE -Wno-unused-function -ffunction-sections -fdata-sections
  $<$<NOT:$<CONFIG:Debug>>:-O2>)
target_link_options(ckey_jni PRIVATE
  -Wl,--gc-sections
  -Wl,--exclude-libs,ALL
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/ckey_jni.map"
  # 16 KB page alignment (required for Android 15+ devices with 16 KB pages).
  -Wl,-z,max-page-size=16384)
