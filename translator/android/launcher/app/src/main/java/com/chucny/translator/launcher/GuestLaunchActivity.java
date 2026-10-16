package com.chucny.translator.launcher;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import com.chucny.translator.core.ZBridge;

/**
 * Entry point of a plugin (library tap or pinned shortcut), running in :guest. Loads the plugin
 * and starts its launcher activity through a stub; if the plugin is already on screen, finishing
 * simply brings its task to the front.
 */
public class GuestLaunchActivity extends Activity {
    private static final String TAG = "zb-launcher";
    static final String EXTRA_PACKAGE = "com.chucny.translator.launcher.PACKAGE";

    /**
     * The intent that opens a plugin. The data names the plugin, because Android matches an
     * existing task against the intent that created it and ignores extras: with no data, a launch
     * of one plugin matches the task of another and revives that one instead. {@code clearTask}
     * discards a task left by a different plugin; without it a task whose process is gone is
     * recreated from its own old intent, which is the previous game.
     */
    static Intent intent(Context context, String packageName, boolean clearTask) {
        Intent intent = new Intent(Intent.ACTION_MAIN)
                .setClass(context, GuestLaunchActivity.class)
                .setData(Uri.parse("zbguest://" + packageName))
                .putExtra(EXTRA_PACKAGE, packageName)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (clearTask) intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK);
        return intent;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        String packageName = getIntent().getStringExtra(EXTRA_PACKAGE);
        try {
            launch(packageName);
        } catch (Throwable t) {
            // One plugin per :guest process (the guest runtime is process-lifetime). A game that
            // never exits keeps the process, so switching games means restarting it.
            if (t.toString().contains("already runs plugin")) {
                startActivity(PluginSwitchActivity.intent(this, packageName));
                finish();
                return;
            }
            String bridgeError = null;
            try {
                bridgeError = ZBridge.lastLoadError();
            } catch (Throwable ignored) {
                // Preserve the original launch failure if the bridge itself cannot be queried.
            }
            Throwable report = bridgeError != null ? new IllegalStateException(bridgeError, t) : t;
            Diagnostics.report(this, "cannot launch " + packageName, report, true);
            String detail = bridgeError != null ? bridgeError : t.toString();
            Toast.makeText(this, "Cannot launch " + packageName + ": " + detail
                            + "\nFull error copied to the clipboard",
                    Toast.LENGTH_LONG).show();
            if (bridgeError != null) {
                // A failed proxy path and the process-lifetime guest runtime cannot be retried.
                new Handler(Looper.getMainLooper()).postDelayed(
                        () -> android.os.Process.killProcess(android.os.Process.myPid()), 2000);
            }
        }
        finish();
    }

    private void launch(String packageName) throws Exception {
        GuestRuntime runtime = GuestRuntime.get();
        if (!runtime.isInstalled()) throw new IllegalStateException("guest runtime is not installed");
        if (runtime.isRunning(packageName)) return;

        PluginRecord record = PluginStore.find(this, packageName);
        if (record == null) throw new IllegalStateException("not imported (remove this shortcut)");
        if (!record.isLaunchable()) throw new IllegalStateException(record.status());

        runtime.load(packageName);
        Intent target = new Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER)
                .setClassName(packageName, record.launcherActivity);
        Intent routed = runtime.route(target);
        if (routed == target) throw new IllegalStateException(record.launcherActivity + " is not a plugin activity");
        startActivity(routed);
    }
}
