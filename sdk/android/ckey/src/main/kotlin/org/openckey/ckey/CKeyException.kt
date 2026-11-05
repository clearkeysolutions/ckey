package org.openckey.ckey

/**
 * A refusal or failure reported by the CKey core.
 *
 * [status] is the core's `ckey_status` (1 invalid argument, 2 unsupported
 * profile, 3 insufficient buffer, 4 internal error, 5 uncorrectable,
 * 6 integrity failure); the message is the core's diagnostic.
 *
 * The (int, String) constructor is called from JNI; keep its signature.
 */
class CKeyException(val status: Int, message: String) : RuntimeException(message) {
    companion object {
        const val INVALID_ARGUMENT = 1
        const val UNSUPPORTED_PROFILE = 2
        const val INSUFFICIENT_BUFFER = 3
        const val INTERNAL_ERROR = 4
        const val UNCORRECTABLE = 5
        const val INTEGRITY_FAILURE = 6
    }
}
