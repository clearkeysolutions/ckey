# Native library for the CKey .NET SDK: one shared library named `ckey`
# (ckeynative.dll / libckeynative.so / libckeynative.dylib) built from the repository's core
# sources (codec, scan, render, certify, telemetry). The NuGet package ships it as runtimes/<rid>/native/ assets.
#
#   cmake -S sdk/dotnet/native -B build-dotnet -DCMAKE_BUILD_TYPE=Release -DCKEY_DOTNET_RID=linux-x64
#   cmake --build build-dotnet --config Release
#
# With CKEY_DOTNET_RID set, the library is written to
# sdk/dotnet/artifacts/native/<rid>/, where CKey.csproj picks it up.
cmake_minimum_required(VERSION 3.20)
project(ckey_dotnet_native LANGUAGES C CXX)

get_filename_component(CKEY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

option(CKEY_DOTNET_TELEMETRY "Include the optional telemetry module (src/telemetry.cpp)" ON)
option(CKEY_DOTNET_LAYOUT_PROBE "Build the struct layout probe (tools/layout_probe.c)" ON)
set(CKEY_DOTNET_RID "" CACHE STRING "NuGet runtime identifier (win-x64, linux-x64, osx-arm64, osx-x64); selects the output folder")

set(CKEY_DOTNET_SOURCES
  ${CKEY_ROOT}/src/ckey_codec.cpp
  ${CKEY_ROOT}/src/ckey_scan.cpp
  ${CKEY_ROOT}/src/ckey_render.cpp)
# Optical certification (renders and decodes; needs scan + render).
if(EXISTS ${CKEY_ROOT}/src/ckey_certify.cpp)
  list(APPEND CKEY_DOTNET_SOURCES ${CKEY_ROOT}/src/ckey_certify.cpp)
endif()
if(CKEY_DOTNET_TELEMETRY)
  list(APPEND CKEY_DOTNET_SOURCES ${CKEY_ROOT}/src/telemetry.cpp)
endif()

add_library(ckey SHARED ${CKEY_DOTNET_SOURCES})
target_compile_features(ckey PRIVATE cxx_std_20)
target_include_directories(ckey PRIVATE ${CKEY_ROOT}/include ${CKEY_ROOT}/src)
# CKEY_SHARED + CKEY_BUILDING: CKEY_API becomes __declspec(dllexport) on
# Windows and visibility("default") with GCC/Clang. Everything else is hidden.
target_compile_definitions(ckey PRIVATE CKEY_SHARED CKEY_BUILDING)
set_target_properties(ckey PROPERTIES
  OUTPUT_NAME ckeynative
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  POSITION_INDEPENDENT_CODE YES)
if(WIN32)
  # ckeynative.dll, also under MinGW (which would otherwise name it libckeynative.dll).
  set_target_properties(ckey PROPERTIES PREFIX "")
endif()
if(MSVC)
  # Static CRT: consumers need no Visual C++ redistributable.
  set_target_properties(ckey PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(ckey PRIVATE /utf-8 /EHsc)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Each core translation unit compiles shared internals it may not call.
  target_compile_options(ckey PRIVATE -Wno-unused-function)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Portable across distributions: no libstdc++/libgcc runtime dependency,
  # and their symbols stay hidden; refuse unresolved symbols at link time.
  target_link_options(ckey PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,--no-undefined)
  # Export the C API and nothing else (see exports.map).
  target_link_options(ckey PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
  set_property(TARGET ckey APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
endif()
if(CKEY_DOTNET_TELEMETRY)
  find_package(Threads REQUIRED)
  target_link_libraries(ckey PRIVATE Threads::Threads)
endif()

if(NOT CKEY_DOTNET_RID STREQUAL "")
  set(CKEY_DOTNET_OUT "${CMAKE_CURRENT_SOURCE_DIR}/../artifacts/native/${CKEY_DOTNET_RID}")
  # $<1:...> stops multi-config generators from appending a Release/ folder.
  set_target_properties(ckey PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CKEY_DOTNET_OUT}>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CKEY_DOTNET_OUT}>")
endif()

if(CKEY_DOTNET_LAYOUT_PROBE)
  # Prints sizeof/offsetof of every struct the C# interop declares; CI diffs
  # it against tools/layout_expected.txt (64-bit ABIs).
  add_executable(ckey_layout_probe tools/layout_probe.c)
  target_include_directories(ckey_layout_probe PRIVATE ${CKEY_ROOT}/include)
  set_target_properties(ckey_layout_probe PROPERTIES C_STANDARD 99
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")
endif()
