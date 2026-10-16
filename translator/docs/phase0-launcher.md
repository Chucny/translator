# Phase 0: launcher shell (plugin skeleton, no translation)

Project: `android/launcher/` (AGP 8.7.3, Gradle 8.11.1, compileSdk 35, minSdk 26, targetSdk 35,
plain Java, UI built in code, no AndroidX). Phone copy: `/sdcard/AndroidIDEProjects/Chucny's Translator`.

Acceptance: a normal **arm64** game launches from a pinned shortcut, renders and takes touch input,
while running as a plugin inside the launcher's `:guest` process. Nothing is installed.

## Architecture

```
main process                         :guest process
------------                         --------------
LibraryActivity                      ZbApplication.onCreate -> GuestRuntime.install()
  import (SAF) -> PluginStore          HiddenApi.exemptAll()        (LSPosed HiddenApiBypass)
  tap / shortcut ------------------->  ActivityThread.mInstrumentation = GuestInstrumentation
                                     GuestLaunchActivity (Theme.NoDisplay)
                                       GuestRuntime.load(pkg) -> LoadedPlugin
                                       startActivity(route(launcher intent)) -> Stubs$X intent
                                     GuestInstrumentation.newActivity(stub intent)
                                       -> plugin Activity from the plugin DexClassLoader
                                     GuestInstrumentation.callActivityOnCreate
                                       -> prepareActivity(): plugin context/resources/theme/intent
```

On-disk layout, `filesDir/plugins/<package>/`:
- `base.apk`: read-only copy. Android 14+ refuses dex code from writable files.
- `lib/`: `lib/arm64-v8a/*.so` extracted at import.
- `data/`: the plugin's private files, cache, databases and app_* dirs (see PluginContext).
- `icon.png`, `meta.properties`: label, launcher activity, version, targetSdk, ABI list.

### Import (main process)

- `PackageManager.getPackageArchiveInfo` provides the package, activities, application info and
  label/icon (after pointing `sourceDir` at the copy).
- The launcher activity comes from the binary manifest through the public
  `AssetManager.openXmlResourceParser(cookie, "AndroidManifest.xml")`. Archive parsing drops intent
  filters; each cookie is tried until the manifest's package matches, so framework-res is skipped.
  `activity-alias` resolves to its `targetActivity`.
- **ABI status:**
  - `arm64: ready`;
  - `Java only: ready`;
  - `32-bit: needs translator (not yet)`: `armeabi`/`armeabi-v7a` only;
  - otherwise unsupported.
- Import and delete kill the `:guest` process first, so a running plugin never keeps a stale APK
  mapped.

### Loading (`LoadedPlugin`, :guest)

- **Class loader.** `DexClassLoader(base.apk, codeCache/plugins/<pkg>, lib/, BootClassLoader)`.
  Its parent is the boot loader, so plugin classes never resolve against launcher classes. The
  library search path makes the plugin's `System.loadLibrary` find `lib/*.so` in its own linker
  namespace.
- **Resources.** `PackageManager.getResourcesForApplication(appInfo)` with `sourceDir` pointing
  at the APK. This is public API; no `AssetManager.addAssetPath` reflection is needed.
- **Application.** `Instrumentation.newApplication(pluginAppClass, PluginContext)` followed by
  `onCreate()`.

### Activity lifecycle: Instrumentation hook

**Chosen:** replace `ActivityThread.mInstrumentation` with `GuestInstrumentation`, which delegates to
the original instance. This is the RePlugin/VirtualApp "stub + Instrumentation" approach, and the
real framework does all the work:
- `startActivity`: the hidden `execStartActivity`/`execStartActivities` are overridden and rewrite
  an intent naming a plugin activity to a manifest stub. The original intent travels as an extra.
  Plugins build intents with the launcher's package (`new Intent(this, Foo.class)`), so matching
  is by class name against the plugin's activity list.
- `newActivity`: for a stub intent, instantiates the plugin class instead. ActivityThread then
  attaches it with the stub's real token, window and configuration.
- `callActivityOnCreate`: before the plugin's `onCreate`:
  - swaps `ContextWrapper.mBase` to a `PluginContext`;
  - clears the resources, theme and inflater cached by `ContextThemeWrapper`, plus
    `Window.mWindowStyle`, which `attach()` already filled with launcher values;
  - sets `Activity.mApplication` to the plugin Application;
  - applies the plugin activity/app theme;
  - restores the original intent (extras class loader = plugin);
  - applies orientation, soft input mode, title and task description (name/icon in recents).
- `callActivityOnDestroy`: keeps a live-activity count, so a shortcut tap on a running game just
  brings its task forward.

**Why not the alternatives:**
- **Hooking `ActivityThread.mH` / `ClientTransaction` (`LaunchActivityItem`).** Android 9, 12, 14
  and 16 each reshaped these internals, while the `Instrumentation` methods used here have been
  stable since API 18.
- **A hand-driven proxy Activity that calls the plugin's lifecycle methods.** Many plugin classes
  (window, fragments, GL surfaces) break when not attached by the framework.

**Stub pool** (all in `:guest`, affinity `com.chucny.translator.launcher.guest`, hardware accelerated,
broad `configChanges`):
- `Stubs$Standard`, `$Landscape` and `$Portrait` (by the plugin's `screenOrientation`);
- `$SingleTop`, `$SingleTask`, `$SingleInstance` (by `launchMode`).

### PluginContext

- **Plugin-owned:** resources, assets, class loader, ApplicationInfo, code path and storage.
  Storage covers files, cache, no_backup, databases, `getDir`, SharedPreferences (name prefixed
  with the package), and external files/cache/obb subdirectories.
- **Launcher-owned identity:** `getPackageName`/`getOpPackageName` stay the launcher's.
  System services (AppOps, notifications, window manager) check the caller's package against its
  uid, so reporting the plugin's package would throw SecurityException.

## Hidden API approach

`org.lsposed.hiddenapibypass:hiddenapibypass:6.1` (Apache-2.0; compatible with a closed-source
release). `HiddenApiBypass.addHiddenApiExemptions("L")` runs once in `:guest` before any
reflection.
- **Why this library:** meta-reflection (double reflection) was closed in Android 11. The
  library's Unsafe-based method works without root through Android 16 and needs neither a
  `targetSdk` downgrade nor a global setting.
- **Fallback if a future release breaks it:** most members used here (`currentActivityThread`,
  `mInstrumentation`, `execStartActivity`, `ContextWrapper.mBase`, `Activity.mApplication`) are on
  the "unsupported" greylist and may keep working. On this rooted phone,
  `su -c 'settings put global hidden_api_policy 1'` disables enforcement device-wide.
- **Failures are logged:** logcat tag `zb-launcher`.

## Known limits (Phase 0)

- **Components.** Only activities. Services, broadcast receivers, content providers (including
  auto-init providers such as Firebase/WorkManager/AndroidX Startup), `PendingIntent`s and
  implicit intents resolved to plugin activities are not supported.
- **APK format.** Single `base.apk` only; split bundles (.apks/.xapk, `config.arm64_v8a.apk`) are
  rejected at import.
- **Permissions.** The plugin's own permissions are not granted; the launcher declares INTERNET,
  ACCESS_NETWORK_STATE, VIBRATE and WAKE_LOCK. Runtime permission requests go to the launcher
  identity.
- **Stubs and processes.**
  - One stub per launch mode: two different singleTask plugin activities alive at once would
    share it.
  - All plugins share the single `:guest` process; launch one at a time for now.
- **Meta-data and component names.**
  - `getPackageManager().getActivityInfo(getComponentName(), ...)` sees the stub, not the plugin
    activity.
  - `NativeActivity`-based games take `android.app.lib_name` from that lookup, so they fall back to
    `libmain.so`. Unity/GLSurfaceView games that call `System.loadLibrary` are unaffected.
- **Configuration and window.**
  - Broad `configChanges` on the stubs: activities that expect recreation on rotation get
    `onConfigurationChanged` instead.
  - targetSdk 35 means edge-to-edge is enforced on Android 15+ for plugin windows too.
- **Architecture.** 32-bit plugins are imported and flagged but not run; that is Phases 3-6
  (`libzbridge.so`).
- **Signing.** Google Play Games/licensing checks that rely on the installer or signature fail.

## Device test

1. Open `/sdcard/AndroidIDEProjects/Chucny's Translator` in AndroidIDE, build and install. The launcher
   icon is labelled "Chucny's Translator".
2. Get a simple free **arm64** game as a single APK (not a split bundle). The choice is yours;
   start with a simple offline 2D game (GLSurfaceView or Unity without Play Services). Check with
   `unzip -l game.apk | grep lib/`: `lib/arm64-v8a/` must be present and there must be no
   `config.*.apk` splits.
3. In Chucny's Translator tap **Import APK** and pick the file. The row should show `arm64: ready`.
4. Tap the row: the game should start. Check that it renders and responds to touch.
5. Long-press the row -> **Pin shortcut** -> confirm on the home screen. Close the game, launch it
   from the shortcut, check again that it renders and takes touch input. That is the Phase 0
   acceptance.
6. Recents should show the game's name. Pressing the shortcut again while the game runs should
   bring it back rather than start a second copy.
7. If something fails, send the output of
   `su -c 'logcat -d -s zb-launcher AndroidRuntime DEBUG'`.
