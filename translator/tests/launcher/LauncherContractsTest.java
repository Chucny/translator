package com.chucny.translator.launcher;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class LauncherContractsTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void write(File file, String text) throws Exception {
        File parent = file.getParentFile();
        if (parent != null) check(parent.isDirectory() || parent.mkdirs(), "mkdir " + parent);
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String read(File file) throws Exception {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    private static File apk(File root, String... entries) throws Exception {
        File apk = new File(root, "test.apk");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(apk))) {
            for (int i = 0; i < entries.length; i += 2) {
                out.putNextEntry(new ZipEntry(entries[i]));
                out.write(entries[i + 1].getBytes(StandardCharsets.UTF_8));
                out.closeEntry();
            }
        }
        return apk;
    }

    private static void testAbiPriority() {
        Set<String> all = new HashSet<>(Arrays.asList("x86", "armeabi", "armeabi-v7a", "arm64-v8a"));
        check("arm64-v8a".equals(PluginFiles.selectAbi(all)), "arm64 must win");
        all.remove("arm64-v8a");
        check("armeabi-v7a".equals(PluginFiles.selectAbi(all)), "v7a must win over armeabi");
        all.remove("armeabi-v7a");
        check("armeabi".equals(PluginFiles.selectAbi(all)), "armeabi fallback");
        all.remove("armeabi");
        check(PluginFiles.selectAbi(all) == null, "unsupported ABI must not be selected");
        check(PluginFiles.selectAbi(new HashSet<>()) == null, "Java-only APK has no selected ABI");
    }

    private static void testNamesAndDelegation() {
        check("libexample.so".equals(PluginFiles.libraryFileName("example")), "mapped library name");
        for (String bad : new String[] {"", ".", "..", "a/b", "a\\b", "bad\0name"}) {
            check(PluginFiles.libraryFileName(bad) == null, "unsafe library name: " + bad);
        }
        check(PluginFiles.safeRelativePath("system/lib/libc.so"), "safe asset path");
        for (String bad : new String[] {"", "/absolute", "../escape", "a/../escape", "a//b", "a\\b"}) {
            check(!PluginFiles.safeRelativePath(bad), "unsafe asset path: " + bad);
        }
        check(PluginClassLoader.delegateToBridge("com.chucny.translator.core.ZBridge"), "bridge delegation");
        check(PluginClassLoader.delegateToBridge("com.chucny.translator.core.internal.Api"), "bridge subtree");
        check(!PluginClassLoader.delegateToBridge("com.chucny.translator.launcher.LibraryActivity"),
                "launcher classes stay hidden");
        check(!PluginClassLoader.delegateToBridge("com.example.Plugin"), "plugin classes stay child-first");
    }

    /**
     * A build that ships its own name for an app must never rename the app's real package: that
     * package is the store directory and the launch key. Only the displayed name may change.
     */
    private static void testDisplayIdentity(File root) throws Exception {
        File dir = new File(root, "display");
        check(dir.isDirectory() || dir.mkdirs(), "mkdir " + dir);
        PluginRecord record = new PluginRecord(dir);
        record.packageName = "com.example.original";
        record.label = "Example App";
        record.targetSdk = 24;
        record.selectedAbi = "armeabi-v7a";
        record.abis.add("armeabi-v7a");
        record.launcherActivity = "com.example.Main";
        record.save();
        check("com.example.original".equals(PluginRecord.load(dir).displayName()),
                "without an override the real package is shown");

        record.displayPackage = "com.chucny.translator.display";
        record.save();
        PluginRecord renamed = PluginRecord.load(dir);
        check("com.chucny.translator.display".equals(renamed.displayName()), "display override round-trips");
        check("com.example.original".equals(renamed.packageName), "import key is not renamed");
        check("Example App".equals(renamed.label), "label round-trips");
        check(renamed.isLaunchable() && "32-bit armeabi-v7a: ready".equals(renamed.status()),
                "a renamed app is still launchable");

        record.displayPackage = "";
        record.save();
        check("com.example.original".equals(PluginRecord.load(dir).displayName()),
                "an empty override falls back instead of blanking the name");
    }

    private static void testExtractionAndMetadata(File root) throws Exception {
        File archive = apk(root,
                "lib/arm64-v8a/libboth.so", "64",
                "lib/armeabi-v7a/libboth.so", "32v7",
                "lib/armeabi-v7a/libonly.so", "only",
                "lib/x86/libignored.so", "x86",
                "assets/not-a-library.so", "asset");
        Set<String> abis = PluginFiles.scanAbis(archive);
        check(abis.equals(new HashSet<>(Arrays.asList("arm64-v8a", "armeabi-v7a", "x86"))), "ABI scan");

        File lib = new File(root, "plugin/lib");
        write(new File(lib, "stale.so"), "stale");
        final int[] fixed = {0};
        PluginFiles.extractLibraries(archive, lib, "armeabi-v7a", file -> {
            fixed[0]++;
            check(file.getName().startsWith("lib"), "fixer receives a library");
        });
        check(!new File(lib, "stale.so").exists(), "reimport removes stale libraries");
        check("32v7".equals(read(new File(lib, "libboth.so"))), "selected ABI extracted");
        check("only".equals(read(new File(lib, "libonly.so"))), "second library extracted");
        check(fixed[0] == 2, "all arm32 libraries fixed before completion");

        File failed = new File(root, "failed/lib");
        boolean rejected = false;
        try {
            PluginFiles.extractLibraries(archive, failed, "armeabi-v7a", file -> {
                throw new java.io.IOException("injected fixup failure");
            });
        } catch (java.io.IOException expected) {
            rejected = true;
        }
        check(rejected && !failed.exists(), "failed fixup leaves no completed library directory");

        PluginRecord record = new PluginRecord(new File(root, "plugin"));
        record.packageName = "com.example.plugin";
        record.label = "Plugin";
        record.launcherActivity = "com.example.Main";
        record.targetSdk = 16;
        record.selectedAbi = "armeabi-v7a";
        record.abis.addAll(abis);
        record.save();
        PluginRecord loaded = PluginRecord.load(record.dir);
        check(loaded != null && "armeabi-v7a".equals(loaded.selectedAbi), "selected ABI metadata");
        check(loaded.isTranslated() && loaded.isLaunchable(), "arm32 plugin ready");
        check("32-bit armeabi-v7a: ready".equals(loaded.status()), "arm32 status");
    }

    private static void testProxy(File root) throws Exception {
        File template = new File(root, "runtime/libtranslatorproxy.so");
        File guest = new File(root, "plugin/lib/libexample.so");
        File proxyDir = new File(root, "plugin/proxy");
        write(template, "proxy-v1");
        write(guest, "guest");
        File proxy = PluginFiles.resolveLibrary(new File(root, "plugin/lib"), proxyDir, template, true, "example");
        check(proxy != null && proxy.getName().equals("libexample.so"), "proxy path");
        check("proxy-v1".equals(read(proxy)), "proxy copied atomically");
        write(template, "proxy-v2");
        check(proxy.equals(PluginFiles.resolveLibrary(new File(root, "plugin/lib"), proxyDir, template, true, "example")),
                "same proxy path reused");
        check("proxy-v2".equals(read(proxy)), "stale proxy is refreshed from the updated template");
        check(PluginFiles.resolveLibrary(new File(root, "plugin/lib"), proxyDir, template, true, "missing") == null,
                "missing guest library falls back");
        check(PluginFiles.resolveLibrary(new File(root, "plugin/lib"), proxyDir, template, false, "example").equals(guest),
                "arm64 uses the real library");
    }

    private static void testDirectoryPublication(File root) throws Exception {
        File target = new File(root, "publish/plugin");
        File staging = new File(root, "publish/staging");
        write(new File(target, "data/save.dat"), "save");
        write(new File(target, "lib/stale.so"), "old");
        write(new File(staging, "lib/current.so"), "new");
        write(new File(staging, "meta.properties"), "complete");
        PluginFiles.replaceDirectoryKeeping(staging, target, "data");
        check("save".equals(read(new File(target, "data/save.dat"))), "reimport preserves plugin data");
        check(!new File(target, "lib/stale.so").exists(), "reimport drops stale runtime files");
        check("new".equals(read(new File(target, "lib/current.so"))), "reimport publishes staged files");
    }

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("zb-launcher-contracts-").toFile();
        try {
            testAbiPriority();
            testDisplayIdentity(root);
            testNamesAndDelegation();
            testExtractionAndMetadata(root);
            testProxy(root);
            testDirectoryPublication(root);
        } finally {
            PluginFiles.deleteRecursive(root);
        }
        System.out.println("LauncherContractsTest PASS");
    }
}
