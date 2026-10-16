package com.chucny.translator.launcher;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

/** Optional build-time seed descriptor; public builds omit it and import apps chosen by users. */
final class SeedPlugin {
    /** Optional metadata key for build-specific app seeding. */
    static final String META = "zb-docs/bundled-app.txt";
    static final String APK_ASSET = "app/base.apk";
    private static final String TAG = "zb-launcher";
    private static final int MAX_META_BYTES = 8 * 1024;

    private SeedPlugin() {}

    /** Reads and closes an optional descriptor, or returns null when there is none. */
    static Properties read(InputStream in) {
        if (in == null) return null;
        try (InputStream stream = in) {
            byte[] data = new byte[MAX_META_BYTES];
            int used = 0;
            for (;;) {
                if (used == data.length) return null;
                int count = stream.read(data, used, data.length - used);
                if (count == -1) break;
                used += count;
            }
            Properties properties = new Properties();
            properties.load(new ByteArrayInputStream(data, 0, used));
            return properties.getProperty("package") != null ? properties : null;
        } catch (IOException e) {
            return null;
        }
    }

    /** Reads an optional descriptor, or returns null when this build seeds no app. */
    static Properties read(Context context) {
        try {
            return read(context.getAssets().open(META, AssetManager.ACCESS_STREAMING));
        } catch (IOException e) {
            return null;
        }
    }

    /** The record named by an optional seed descriptor, or null if it is not installed. */
    static PluginRecord installed(Context context) {
        Properties properties = read(context);
        if (properties == null) return null;
        return PluginRecord.load(new File(PluginStore.root(context), properties.getProperty("package")));
    }

    /**
     * Installs the configured seed app when missing. Synchronized because two refreshes can race;
     * the caller must keep this method off the UI thread because importing copies and fixes an APK.
     */
    static synchronized PluginRecord installIfMissing(Context context) throws IOException {
        Properties properties = read(context);
        if (properties == null) return null;
        String packageName = properties.getProperty("package");
        File installed = new File(PluginStore.root(context), packageName);
        if (!installed.isDirectory()) {
            try (InputStream apk = context.getAssets().open(APK_ASSET, AssetManager.ACCESS_STREAMING)) {
                PluginStore.importApk(context, apk, properties.getProperty("label", packageName));
            }
        }
        reconcile(context);
        return PluginRecord.load(installed);
    }

    /** Applies optional display metadata to its installed record without re-importing it. */
    static synchronized boolean reconcile(Context context) {
        Properties properties = read(context);
        if (properties == null) return false;
        PluginRecord record = PluginRecord.load(
                new File(PluginStore.root(context), properties.getProperty("package")));
        if (record == null) return false;
        String label = properties.getProperty("label");
        String display = properties.getProperty("displayPackage");
        boolean changed = false;
        if (label != null && !label.isEmpty() && !label.equals(record.label)) {
            record.label = label;
            changed = true;
        }
        if (display != null && !display.isEmpty() && !display.equals(record.displayPackage)) {
            record.displayPackage = display;
            changed = true;
        }
        if (!changed) return false;
        try {
            record.save();
        } catch (IOException e) {
            // The name is cosmetic; failure to update it does not affect the imported app.
            Log.w(TAG, "cannot update optional seed metadata: " + e);
            return false;
        }
        return true;
    }
}
