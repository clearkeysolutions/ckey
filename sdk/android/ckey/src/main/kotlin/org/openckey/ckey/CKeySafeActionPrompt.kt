package org.openckey.ckey

import android.app.AlertDialog
import android.content.Context

/**
 * The SDK's confirmation for a cross-device Safe Action (spec/CKey-Safe-Actions.md,
 * "What a reader must do", step 6).
 *
 * This is the only way to approve a [CKeySafeActionRequest]: it names the
 * verified issuer and the issuer's account of who is asking (device, place,
 * time), and runs the registered handler only when the person taps Continue.
 * Cancel, the back button and tapping outside all decline. Call on the main thread.
 */
object CKeySafeActionPrompt {
    /** The dialog's body text; public so apps can match it in UI tests. */
    @JvmStatic
    fun message(request: CKeySafeActionRequest): String = buildString {
        append("Requested by ").append(request.details.issuer).append(" (verified)\n")
        request.requestInfo.device?.let { append("Device: ").append(it).append('\n') }
        request.requestInfo.location?.let { append("Location: ").append(it).append('\n') }
        request.requestInfo.requestedAt?.let { append("Requested: ").append(it).append('\n') }
        append('\n')
        append("Only continue if this is the device and place you are using right now. ")
        append("Someone may be trying to use a code meant for their own session.")
    }

    /**
     * Shows the confirmation. [onFinish] receives true when the person approved
     * and the handler ran, false otherwise (including when [request] was already decided).
     */
    @JvmStatic
    @JvmOverloads
    fun show(context: Context, request: CKeySafeActionRequest, onFinish: (approved: Boolean) -> Unit = {}) {
        if (request.isDecided) { onFinish(false); return }
        AlertDialog.Builder(context)
            .setTitle(request.title)
            .setMessage(message(request))
            .setNegativeButton("Cancel") { _, _ -> onFinish(request.decide(false)) }
            .setPositiveButton("Continue") { _, _ -> onFinish(request.decide(true)) }
            .setOnCancelListener { onFinish(request.decide(false)) }
            .create()
            .show()
    }
}
