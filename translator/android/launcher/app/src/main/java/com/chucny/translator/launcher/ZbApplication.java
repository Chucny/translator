package com.chucny.translator.launcher;

import android.app.Application;
import android.os.Build;

import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Installs the plugin runtime when running as the :guest process. */
public class ZbApplication extends Application {
    static final String GUEST_SUFFIX = ":guest";

    @Override
    public void onCreate() {
        super.onCreate();
        if (processName().endsWith(GUEST_SUFFIX)) {
            Diagnostics.installCrashRecorder(this);
            MainLooperGuard.install(this);
            // Before any plugin code runs: a :guest process that dies silently must still leave
            // its runtime report on disk.
            Diagnostics.startRuntimeReport(this);
            GuestRuntime.get().install(this);
        }
    }

    static String processName() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) return Application.getProcessName();
        try (FileInputStream in = new FileInputStream("/proc/self/cmdline")) {
            byte[] buf = new byte[256];
            int n = in.read(buf);
            int end = 0;
            while (end < n && buf[end] != 0) end++;
            return new String(buf, 0, end, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }
}
