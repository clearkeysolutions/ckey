package android.app;

public class Activity extends android.content.Context {
    public boolean isChangingConfigurations() { return false; }
    @Override public android.content.Context getApplicationContext() { return null; }
}
