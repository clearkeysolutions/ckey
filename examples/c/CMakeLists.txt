# Example consumer of an installed CKey (find_package). Build with:
#   cmake -S examples/c -B build-example -DCMAKE_PREFIX_PATH=<install prefix>
cmake_minimum_required(VERSION 3.20)
project(ckey_example LANGUAGES CXX)
find_package(CKey 0.1 REQUIRED)
add_executable(render_and_scan main.cpp)
target_link_libraries(render_and_scan PRIVATE CKey::core)
add_executable(scan_only scan_only.cpp)
target_link_libraries(scan_only PRIVATE CKey::scan)
