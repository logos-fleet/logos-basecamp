package co.logos.webview;

import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.res.Configuration;
import android.util.Log;

/**
 * WHEN ANDROID ASKS FOR MEMORY BACK, said in C++ (logos-workspace#153).
 *
 * <p>The Android half of what the {@code UIApplicationDidReceiveMemoryWarning}
 * observer is in {@code IosWebPage.mm}, and it needs a Java class for the same
 * reason the page does: {@link ComponentCallbacks2} is an INTERFACE the system
 * calls, and JNI cannot implement one without a class to implement it with.
 *
 * <p>The levels are passed through unchanged and the C++ side decides which
 * ones mean "you are next" -- one policy, in the place where the other
 * platform's signal also lands, rather than two thresholds that can drift.
 *
 * <p>Callable by hand, which is what makes it testable on a device:
 * {@code adb shell am send-trim-memory co.logos.basecamp.shell RUNNING_CRITICAL}
 * delivers a real onTrimMemory to a running app.
 */
public final class LogosMemoryPressure {

    private static final String TAG = "logos-web";

    /** The level Android reported. See ComponentCallbacks2's TRIM_MEMORY_* . */
    private static native void nativeTrimMemory(int level);

    private static ComponentCallbacks2 sCallbacks;

    private LogosMemoryPressure() {}

    /**
     * Start listening. Idempotent: the container installs its factory once per
     * process but install() may be called again, and a second registration
     * would answer one warning twice.
     *
     * <p>ON THE APPLICATION CONTEXT, not the activity: the callback has to
     * outlive a configuration change, and an activity-scoped one would be
     * unregistered under the app the first time the phone was rotated.
     */
    public static synchronized void watch(Context context) {
        if (sCallbacks != null) return;
        sCallbacks = new ComponentCallbacks2() {
            @Override
            public void onTrimMemory(int level) {
                Log.i(TAG, "onTrimMemory(" + level + ")");
                nativeTrimMemory(level);
            }

            @Override
            public void onLowMemory() {
                // The pre-API-14 spelling, still delivered. It carries no level
                // and means the worst one.
                Log.i(TAG, "onLowMemory()");
                nativeTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE);
            }

            @Override
            public void onConfigurationChanged(Configuration newConfig) {}
        };
        context.getApplicationContext().registerComponentCallbacks(sCallbacks);
        Log.i(TAG, "watching Android memory pressure");
    }
}
