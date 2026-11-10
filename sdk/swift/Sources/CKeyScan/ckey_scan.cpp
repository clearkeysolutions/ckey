// SwiftPM shim for the CKeyScan target. It compiles the repository's existing
// translation unit in place (src/ckey_scan.cpp); no core source is copied.
// The path is relative to this file, so it needs no compiler flags.
#include "../../../../src/ckey_scan.cpp"
