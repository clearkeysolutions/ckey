package android.app;

public class Application extends android.content.Context {
    public interface ActivityLifecycleCallbacks {
        void onActivityCreated(Activity activity, android.os.Bundle savedInstanceState);
        void onActivityStarted(Activity activity);
        void onActivityResumed(Activity activity);
        void onActivityPaused(Activity activity);
        void onActivityStopped(Activity activity);
        void onActivitySaveInstanceState(Activity activity, android.os.Bundle outState);
        void onActivityDestroyed(Activity activity);
    }
    public void registerActivityLifecycleCallbacks(ActivityLifecycleCallbacks callback) {}
    @Override public android.content.Context getApplicationContext() { return this; }
}
