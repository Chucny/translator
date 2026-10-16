package com.chucny.translator.t7;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;

import com.chucny.translator.core.ZBridge;

import dalvik.system.DexClassLoader;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.function.Consumer;

/** On-device Phase 4 JNI diagnostics. Results stay on screen, clipboard and external files. */
public final class T7Activity extends Activity {
    private static final String TAG = "zbridge-t7";
    private static final String PLUGIN_PACKAGE = "com.chucny.translator.t7probe";

    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView output;
    private final StringBuilder transcript = new StringBuilder();

    private static final class PluginLoader extends DexClassLoader {
        private final ClassLoader bridge;
        private final String proxy;

        PluginLoader(String apk, String optimized, ClassLoader boot, ClassLoader bridge, String proxy) {
            super(apk, optimized, null, boot);
            this.bridge = bridge;
            this.proxy = proxy;
        }

        private static boolean bridgeClass(String name) {
            return name.startsWith("com.chucny.translator.core.");
        }

        private static boolean platformClass(String name) {
            return name.startsWith("java.") || name.startsWith("javax.") || name.startsWith("android.")
                    || name.startsWith("dalvik.") || name.startsWith("sun.");
        }

        @Override
        protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
            synchronized (this) {
                Class<?> found = findLoadedClass(name);
                if (found == null) {
                    if (bridgeClass(name)) {
                        found = bridge.loadClass(name);
                    } else if (platformClass(name)) {
                        found = super.loadClass(name, false);
                    } else {
                        try {
                            found = findClass(name);
                        } catch (ClassNotFoundException missing) {
                            found = super.loadClass(name, false);
                        }
                    }
                }
                if (resolve) resolveClass(found);
                return found;
            }
        }

        @Override
        public String findLibrary(String name) {
            return "zbt7probe".equals(name) ? proxy : null;
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        output = new TextView(this);
        output.setTextIsSelectable(true);
        output.setTypeface(android.graphics.Typeface.MONOSPACE);
        int padding = (int) (12 * getResources().getDisplayMetrics().density);
        output.setPadding(padding, padding, padding, padding);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(output);
        setContentView(scroll);
        new Thread(this::runT7, "zbridge-t7-runner").start();
    }

    private synchronized void append(String line) {
        Log.i(TAG, line);
        transcript.append(line).append('\n');
        ui.post(() -> output.append(line + "\n"));
    }

    private void runT7() {
        try {
            append("Installing T7 assets...");
            extractAssets();
            File plugin = new File(getFilesDir(), "plugins/" + PLUGIN_PACKAGE);
            File proxy = new File(plugin, "proxy/libzbt7probe.so");
            File optimized = new File(getCodeCacheDir(), "t7-plugin");
            if (!optimized.isDirectory() && !optimized.mkdirs()) throw new IOException("cannot create " + optimized);
            ClassLoader loader = new PluginLoader(getApplicationInfo().sourceDir, optimized.getPath(),
                    Context.class.getClassLoader(), ZBridge.class.getClassLoader(), proxy.getCanonicalPath());
            ZBridge.activatePlugin(plugin.getCanonicalPath(), 16, loader);
            append("PASS runtime activation");

            Class<?> runner = loader.loadClass("zb.T7Runner");
            Method run = runner.getMethod("run", Consumer.class);
            Object result;
            try {
                result = run.invoke(null, (Consumer<String>) this::append);
            } catch (InvocationTargetException e) {
                throw e.getCause() != null ? e.getCause() : e;
            }
            if (!"T7 PASS".equals(result)) throw new AssertionError("unexpected result " + result);
            writeResult();
        } catch (Throwable failure) {
            String bridge = null;
            try {
                bridge = ZBridge.lastLoadError();
            } catch (Throwable ignored) {
                // Keep the original error.
            }
            String detail = "T7 FAIL: " + failure + (bridge != null ? "\nBridge: " + bridge : "")
                    + "\n" + Log.getStackTraceString(failure);
            append(detail);
            copy(detail);
            writeResult();
        }
    }

    private void extractAssets() throws IOException {
        AssetManager assets = getAssets();
        try (BufferedReader list = new BufferedReader(new InputStreamReader(assets.open("t7-files.txt"),
                StandardCharsets.UTF_8))) {
            String name;
            byte[] buffer = new byte[1 << 16];
            while ((name = list.readLine()) != null) {
                if (name.isEmpty() || name.startsWith("/") || name.contains("..") || name.contains("\\")) {
                    throw new IOException("unsafe asset path " + name);
                }
                File target = new File(getFilesDir(), name);
                File parent = target.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new IOException("cannot create " + parent);
                }
                if (target.exists() && !target.setWritable(true, true)) {
                    throw new IOException("cannot replace old asset " + target);
                }
                try (InputStream in = assets.open(name); OutputStream out = new FileOutputStream(target)) {
                    int count;
                    while ((count = in.read(buffer)) != -1) if (count != 0) out.write(buffer, 0, count);
                }
                if (!target.setReadOnly()) throw new IOException("cannot make read-only: " + target);
                if (name.endsWith("/proxy/libzbt7probe.so") && !target.setExecutable(true, true)) {
                    throw new IOException("cannot make proxy executable: " + target);
                }
            }
        }
    }

    private synchronized void writeResult() {
        try {
            File dir = getExternalFilesDir(null);
            if (dir == null) return;
            try (OutputStream out = new FileOutputStream(new File(dir, "t7-result.txt"))) {
                out.write(transcript.toString().getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException e) {
            Log.e(TAG, "cannot write T7 result", e);
        }
    }

    private void copy(String text) {
        ui.post(() -> {
            ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (clipboard != null) clipboard.setPrimaryClip(ClipData.newPlainText("Chucny's Translator T7", text));
        });
    }
}
