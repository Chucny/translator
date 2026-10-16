# Phase 3 / T6: running the translator inside an app process on the device

Goal (T6 in the spec): the T3-T5 guest tests that pass under `zbrun` on this machine also pass
inside an Android app process on the OnePlus 13, next to ART.

What exists after the night of 2026-09-13/14:
- `libzbridge.so` for arm64-v8a, containing the whole core. It exports `zb_run_executable()`
  (C) and `com.chucny.translator.core.ZBridge.runExecutable()` (JNI).
- The arm32 sysroot from the Android 17 GSI, in `sysroot/`.
- Guest test binaries and libraries: `build/guest/`, `build/guest/lib/`, and the fixed
  Orange Roulette libs in `build/or/`.

Not done and not testable here: nothing has run inside ART yet. This machine is a chroot
without Android's `/system`, so an Android binary cannot run on it.

## Results so far

- **2026-09-14, OnePlus 13, Termux, Android build of `zbrun`: all guest tests PASS.** The
  bundle comes from `tools/make_termux_bundle.sh`; unpack it in the Termux home and run
  `sh run_tests.sh`.
  - First attempt: every dynamic test exited with 1. Termux's `LD_PRELOAD` (a 64-bit
    library) reached the 32-bit guest linker. Fixed by never passing host
    `LD_PRELOAD`/`LD_LIBRARY_PATH` to guests.
- **2026-09-14, T6 app `android/t6/project/` in the `:guest` process: all 9 PASS. Phase 3
  accepted.**
  - The first run failed `syscalls_dynamic`: SIGALRM landed on an ART thread and was
    dropped. Fixed by forwarding to the process signal target.
  - `su -c 'logcat -d -s zbguest'` shows "hello from arm32 guest pid N".
- **Previously open (now done):**
  - the in-app run inside the ART process (below);
  - the logcat check: `su -c 'logcat -d -s zbguest'` (quoted, or su eats the flags) must show
    "hello from arm32 guest" after `log_dynamic`.

## 1. Build the pieces (on this machine)

```
N=$HOME/android-ndk-r29; mkdir -p build/boost-headers; ln -sfn /usr/include/boost build/boost-headers/boost; cmake -S . -B build/android-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$N/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release -DZB_BUILD_TESTS=OFF -DBoost_INCLUDE_DIR=$PWD/build/boost-headers; ninja -C build/android-arm64 zbridge
```
```
tools/build_guest.sh; tools/run_guest_tests.sh
```
The second command also extracts and fixes the Orange Roulette libs into `build/or/`.

## 2. Put them into a test app

**Shortcut.** `tools/make_t6_bundle.sh` assembles everything into `build/t6/`: about 2.5 MB of
native library and 20 MB of assets. Copy its three folders into an AndroidIDE app module
(minSdk 26 or higher):

- `build/t6/jniLibs` -> `app/src/main/jniLibs`
- `build/t6/assets` -> `app/src/main/assets` (includes `zb-files.txt`, the extraction list)
- `build/t6/java` -> `app/src/main/java`: `com.chucny.translator.core.ZBridge` and
  `com.chucny.translator.core.GuestTestActivity`, compile-checked with javac against
  `android.jar` API 36

Declare the activity in its own process and launch it:

```xml
<activity android:name="com.chucny.translator.core.GuestTestActivity" android:process=":guest" android:exported="true" />
```

It extracts the assets to `filesDir/zb/` and runs the table below on a background thread. The
results show on screen and in logcat, tag `zbridge-t6` (translator messages use tag
`zbridge`). Each test's stdout and stderr stay in `filesDir/zb/<test>.stdout|.stderr`.

The manual layout, for reference:

- `app/src/main/jniLibs/arm64-v8a/libzbridge.so` <- `build/android-arm64/core/libzbridge.so`
- assets, keeping this layout; extract to `filesDir/zb/` on first start:
  - `zb/sysroot/system/bin/linker`, `zb/sysroot/system/lib/*.so` <- `sysroot/`
  - `zb/guest/*` <- the test executables in `build/guest/`: `hello_static`,
    `hello_dynamic`, `threads_dynamic`, `kuser_dynamic`, `signals_dynamic`,
    `cxx_dynamic`, `or_dlopen_dynamic`
  - `zb/guest/lib/*.so` <- `build/guest/lib/`
  - `zb/or/*.so` <- `build/or/` (already fixed by `tools/fix_guest_lib.py`)
  - `zb/expected/*.out` <- `guest/tests/expected/`

Files do not need exec permission. zbrun reads the ELF files and maps them itself, and host
memory is never mapped executable for guest code.

Run the tests in their own process, because a guest `exit_group` with live guest threads ends
the process:

```xml
<activity android:name=".GuestTestActivity" android:process=":guest" android:exported="true" />
```

```java
package com.chucny.translator.core;

public final class ZBridge {
    static { System.loadLibrary("zbridge"); }
    private ZBridge() {}
    // Blocks until the guest exits; returns its exit status (128 + signal for a guest crash).
    public static native int runExecutable(String sysroot, String[] argv, String[] envp);
}
```

## 3. The T6 runner

Run on a background thread. Guest stdout and stderr are the process fds 1 and 2, so point
them at a file around each call with `android.system.Os.dup2`.

| test | args | extra env | expected exit |
|---|---|---|---|
| hello_static | `world` | - | 7 |
| hello_dynamic | `<filesDir>/zb/io.tmp` | - | 3 |
| threads_dynamic | - | - | 0 |
| kuser_dynamic | - | - | 0 |
| signals_dynamic | - | - | 134 |
| cxx_dynamic | - | `LD_LIBRARY_PATH=<filesDir>/zb/guest/lib` | 0 |
| or_dlopen_dynamic | `<filesDir>/zb/or` | `LD_LIBRARY_PATH=<filesDir>/zb/guest/lib` | 0 |

`argv[0]` is the absolute path of the test executable. Compare stdout with
`zb/expected/<test>.out`. `hello_static` expects `argc=2 argv1=world`.

Diagnostics:
- **logcat:** `adb logcat -s zbridge`. It shows unimplemented syscalls, crash reports
  with file and offset, and host calls.
- **stderr file:** the guest linker's own messages, for example "unused DT entry"
  warnings for the fixed libs.
- **Syscall trace:** pass `ZB_STRACE=1` in envp.
- **Precise faults:** `ZB_PRECISE_FAULTS=1` in the app process environment (read by the
  translator, not the guest).

## Expected differences from zbrun on Ubuntu (things to watch)

- **Signals.** ART's libsigchain sits in front of every `sigaction`, and zbrun installs
  host handlers for SIGALRM, SIGPIPE, SIGUSR1/2, SIGCHLD and others. ART owns SIGSEGV;
  Dynarmic's fastmem handler must still receive guest faults through the chain. If
  `signals_dynamic` or any fault test crashes the app instead of reporting, look here
  first.
- **SELinux and JIT memory.** Dynarmic allocates executable anonymous memory for its code
  cache. Apps normally may, but if the JIT cannot map its cache the process dies in the
  `GuestThread` constructor.
- **`/dev/__properties__` and logd** exist on the device (not on Ubuntu), so guest bionic
  initialises system properties for real, and guest `__android_log_print` reaches logcat.
- **Paths.** `/system`, `/apex`, `/vendor`, `/odm`, `/product`, `/system_ext` and
  `/linkerconfig` are redirected into the sysroot, so the guest never sees the device's
  64-bit system libraries.
