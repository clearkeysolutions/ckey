package android.app;

import android.content.Context;
import android.content.DialogInterface;

public class AlertDialog implements DialogInterface {
    public void show() {}
    public static class Builder {
        public Builder(Context context) {}
        public Builder setTitle(CharSequence title) { return this; }
        public Builder setMessage(CharSequence message) { return this; }
        public Builder setPositiveButton(CharSequence text, DialogInterface.OnClickListener listener) { return this; }
        public Builder setNegativeButton(CharSequence text, DialogInterface.OnClickListener listener) { return this; }
        public Builder setOnCancelListener(DialogInterface.OnCancelListener listener) { return this; }
        public AlertDialog create() { return new AlertDialog(); }
    }
}
