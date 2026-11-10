// CKey core amalgamation: every public function in one translation unit.
// Apps that compile a single file (the Xcode projects) and the white-box
// tests use this; CMake builds the separate ckey_codec, ckey_scan and
// ckey_render libraries from the same parts (see src/README.md).
#include "core_internal.inc"
#include "api_codec.inc"
#include "api_interchange.inc"
#include "api_safe_actions.inc"
#include "api_scan.inc"
#include "api_render.inc"
#include "api_certify.inc"
