package com.chucny.translator.launcher;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Installs the generated arm32 runtime assets into the launcher's private files directory. */
final class RuntimeBundle {
    private static final String FILE_LIST = "zb-files.txt";
    private static final String VERSION = "zb-version.txt";
    private static final String MARKER = ".bundle-version";

    private RuntimeBundle() {}

    static synchronized void install(Context context) throws IOException {
        File target = new File(context.getFilesDir(), "zb");
        String version = readAssetText(context.getAssets(), VERSION).trim();
        File marker = new File(target, MARKER);
        if (!version.isEmpty() && marker.isFile()
                && version.equals(new String(Files.readAllBytes(marker.toPath()), StandardCharsets.US_ASCII).trim())
                && requiredFilesPresent(target)) {
            return;
        }

        File staging = new File(context.getFilesDir(), "zb.installing");
        PluginFiles.deleteRecursive(staging);
        if (!staging.mkdirs()) throw new IOException("cannot create runtime staging directory " + staging);
        boolean complete = false;
        try (BufferedReader list = new BufferedReader(new InputStreamReader(
                context.getAssets().open(FILE_LIST, AssetManager.ACCESS_STREAMING), StandardCharsets.UTF_8))) {
            String asset;
            while ((asset = list.readLine()) != null) {
                if (!asset.startsWith("zb/") || !PluginFiles.safeRelativePath(asset)) {
                    throw new IOException("unsafe runtime asset path: " + asset);
                }
                String relative = asset.substring(3);
                if (!PluginFiles.safeRelativePath(relative)) throw new IOException("unsafe runtime file: " + asset);
                File out = new File(staging, relative);
                try (InputStream in = context.getAssets().open(asset, AssetManager.ACCESS_STREAMING)) {
                    PluginFiles.copyAtomic(in, out);
                }
                if (!out.setReadOnly()) throw new IOException("cannot make runtime file read-only: " + out);
                if (relative.equals("guest/zbhost") || relative.equals("sysroot/system/bin/linker")) {
                    if (!out.setExecutable(true, true)) throw new IOException("cannot make runtime file executable: " + out);
                }
            }
            try (InputStream in = new ByteArrayInputStream((version + "\n").getBytes(StandardCharsets.US_ASCII))) {
                PluginFiles.copyAtomic(in, new File(staging, MARKER));
            }
            if (!requiredFilesPresent(staging)) throw new IOException("generated runtime bundle is incomplete");
            PluginFiles.replaceDirectory(staging, target);
            complete = true;
        } finally {
            if (!complete) PluginFiles.deleteRecursive(staging);
        }
    }

    static File proxyLibrary(Context context) {
        return new File(context.getFilesDir(), "zb/host/libzbproxy.so");
    }

    private static boolean requiredFilesPresent(File root) {
        return new File(root, "sysroot/system/bin/linker").isFile()
                && new File(root, "guest/zbhost").isFile()
                && new File(root, "guest/lib/libzbcompat.so").isFile()
                && new File(root, "guest/lib/libzbjni.so").isFile()
                && new File(root, "guest/lib/libGLESv2.so").isFile()
                && new File(root, "guest/lib/libEGL.so").isFile()
                && new File(root, "guest/lib/libandroid.so").isFile()
                && new File(root, "guest/lib/libjnigraphics.so").isFile()
                && new File(root, "host/libzbproxy.so").isFile();
    }

    private static String readAssetText(AssetManager assets, String path) throws IOException {
        try (InputStream in = assets.open(path, AssetManager.ACCESS_STREAMING)) {
            byte[] data = new byte[256];
            int used = 0;
            for (;;) {
                if (used == data.length) throw new IOException("runtime version is too long");
                int count = in.read(data, used, data.length - used);
                if (count == -1) break;
                used += count;
            }
            return new String(data, 0, used, StandardCharsets.US_ASCII);
        }
    }
}
