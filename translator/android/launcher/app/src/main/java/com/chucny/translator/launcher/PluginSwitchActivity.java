package com.chucny.translator.launcher;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

/**
 * Routes every plugin launch, in the launcher's own process.
 *
 * The guest runtime is process-lifetime: guest threads cannot be torn down, so one :guest process
 * serves exactly one plugin. A game that exits frees the process by itself; one that keeps
 * running (Flutter does) holds it, and the next game cannot start there. The decision has to be
 * made here rather than in :guest, because starting a guest activity while a game is on screen
 * only brings that game's task to the front: Android matches tasks by intent, and intent extras
 * are not part of that match, so the guest entry point would never run.
 */
public class PluginSwitchActivity extends Activity {
    private static final String EXTRA_PACKAGE = "zb.package";
    private static final int WAIT_STEPS = 40;
    private static final long WAIT_STEP_MS = 50;
    private static final int REQUEST_PERMISSIONS = 0x5A57;

    static Intent intent(Context context, String packageName) {
        return new Intent(Intent.ACTION_MAIN)
                .setClass(context, PluginSwitchActivity.class)
                // Distinct per plugin, so Android never mistakes one launch for another.
                .setData(Uri.parse("zbplugin://" + packageName))
                .putExtra(EXTRA_PACKAGE, packageName)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        final String packageName = getIntent().getStringExtra(EXTRA_PACKAGE);
        if (packageName == null) {
            finish();
            return;
        }
        // The guest runs under the launcher's uid, so the launcher must hold every runtime
        // permission the guest uses. Ask for the proxied ones before the guest starts; the
        // launch continues in the permission callback, or immediately if all are granted.
        GuestPermissions.ensure(this, REQUEST_PERMISSIONS, () -> proceed(packageName));
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        GuestPermissions.onResult(requestCode);
    }

    private void proceed(String packageName) {
        String active = ActivePlugin.of(this);
        if (packageName.equals(active)) {
            // The guest process already holds this plugin: launching brings its task to the front.
            startGuest(packageName, false);
            return;
        }
        if (active == null) {
            // No guest process. A task may still be left over from a game that ended, so it has
            // to go: a stale task is otherwise recreated from its own intent, the previous game.
            startGuest(packageName, true);
            return;
        }
        Toast.makeText(this, "Closing the previous game...", Toast.LENGTH_SHORT).show();
        ActivePlugin.endGuestProcess(this);
        waitForGuestToGo(0, packageName);
    }

    /** The new process must not start before the old one is gone, or it inherits the old plugin. */
    private void waitForGuestToGo(int step, String packageName) {
        if (step < WAIT_STEPS && ActivePlugin.guestIsRunning(this)) {
            new Handler(Looper.getMainLooper()).postDelayed(
                    () -> waitForGuestToGo(step + 1, packageName), WAIT_STEP_MS);
            return;
        }
        startGuest(packageName, true);
    }

    private void startGuest(String packageName, boolean clearTask) {
        startActivity(GuestLaunchActivity.intent(this, packageName, clearTask));
        finish();
    }
}
