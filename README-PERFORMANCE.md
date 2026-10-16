# Performance and input stability

This repository includes two performance/input improvements in the Android ARM32 translation path. See `translator/docs/perf-notes.md` for native runtime benchmarks and limitations.

## Turbo mode

Settings → Performance → Turbo mode enables Dynarmic's safe optimization set, increases the JIT code cache, and enables two floating-point approximations for operations whose precision permits it. It does **not** disable precise memory fault handling; that remains separately configurable per application because some runtimes depend on the precise faulting PC.

The setting is wired through `launcher/.../LauncherSettings.java`, `PluginRecord.java`, `LoadedPlugin.java`, `translator/core/android/zbridge_jni.cpp`, and `translator/core/src/process.cpp` / `guest_thread.cpp`. Device-specific performance should be measured with the target workload; behavior and speed depend on the application and hardware.

## Input queue liveness

The host-side Android looper checks pending input on each poll path, so an unrelated ready descriptor cannot indefinitely defer an input event. A guarded input keeper can also finish pending queue events after the guest has stopped polling for the configured interval, avoiding Android input-dispatch timeouts at the cost of dropping input that the guest did not consume.

See `translator/core/src/android/host_looper.cpp`, `translator/core/src/android/host_input.cpp`, and the related tests in `translator/tests/host/` for implementation and coverage.

## Build the native Android bridge

Set `ANDROID_NDK_HOME`, `BOOST_ROOT`, and `CMAKE_TOOLCHAIN_FILE` for the installed toolchain, then run from `translator/`:

```sh
cmake -S . -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DZB_BUILD_TESTS=OFF \
  -DBoost_INCLUDE_DIR="$BOOST_ROOT"
ninja -C build/android-arm64 zbridge
```

Stage the newly built `libzbridge.so` into the launcher inputs using `translator/tools/make_launcher_bundle.sh` after building the guest artifacts. Build a debug APK with `./build-debug.sh` from the repository root. Public builds default to no bundled client APK.

## Device verification

Install the generated debug APK on a connected device with `adb install -r build/outputs/chucnys-translator-debug.apk`. Open Settings → Performance to confirm Turbo mode is present. For a translated guest run, inspect the runtime report with:

```sh
adb shell run-as com.chucny.translator.bridge cat files/zb-runtime-report.txt
```

Run representative applications and capture logs before making performance or compatibility claims.
