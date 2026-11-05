package org.openckey.ckey.telemetry

import android.app.Activity
import android.app.Application
import android.os.Bundle

/**
 * Tries a telemetry send when the app moves to the background, without
 * androidx.lifecycle: it counts started activities and triggers when the
 * count drops to zero (not on configuration changes, which restart an
 * activity without the app leaving the foreground).
 */
internal object BackgroundWatcher : Application.ActivityLifecycleCallbacks {
    @Volatile private var installed = false
    private var started = 0

    @Synchronized
    fun install(application: Application) {
        if (installed) return
        installed = true
        application.registerActivityLifecycleCallbacks(this)
    }

    override fun onActivityStarted(activity: Activity) {
        synchronized(this) { started++ }
    }

    override fun onActivityStopped(activity: Activity) {
        val background = synchronized(this) {
            started = (started - 1).coerceAtLeast(0)
            started == 0 && !activity.isChangingConfigurations
        }
        if (background) Telemetry.trigger()
    }

    override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) = Unit
    override fun onActivityResumed(activity: Activity) = Unit
    override fun onActivityPaused(activity: Activity) = Unit
    override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit
    override fun onActivityDestroyed(activity: Activity) = Unit
}
