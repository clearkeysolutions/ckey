/* Forwarding header: makes the repository's include/ckey/ckey.h reachable as
 * "ckey/ckey.h" through this target's public header directory (SwiftPM puts
 * it on the include path of this target and every target that depends on
 * it). The core's own sources include "ckey/ckey.h" and resolve here. */
#include "../../../../../../include/ckey/ckey.h"
