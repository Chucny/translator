# Phase 4 / T7: real ART JNI bridge test

T7 exercises the complete arm64 proxy -> Chucny's Translator -> translated arm32 JNI path against the
device's real ART. It does not depend on the Phase 0 launcher. The project is
`android/t7/project/`; generated inputs are under ignored `build/t7/`.

## Build locally

```sh
tools/build_guest.sh
ninja -C build/android-arm64 zbridge zbproxy
tools/make_t7_bundle.sh
ANDROID_HOME="$HOME/android-sdk" ANDROID_SDK_ROOT="$HOME/android-sdk" \
  bash /sdcard/AndroidIDEProjects/Chucny's Translator/gradlew \
  -p "$PWD/android/t7/project" :app:assembleDebug --no-daemon
```

`make_t7_bundle.sh` checks the guest probe exports, ELF class/machine, compiles all Java against
Android API 36, and assembles the 32-bit runtime, test plugin, arm64 proxy and `libzbridge.so`.
The Gradle project consumes only `build/t7/{java,assets,jniLibs}`. The command above reuses the
AndroidIDE wrapper; a normal Gradle 8.11.1 installation also works. On this arm64 host, Gradle must
use the arm64 `aapt2` configured by `$HOME/.gradle/gradle.properties`. The output is
`android/t7/project/app/build/outputs/apk/debug/app-debug.apk`.

## Run on the OnePlus 13

1. Install the generated debug APK. If it was copied by the local workflow, it is
   `/sdcard/AndroidIDEProjects/Chucny's Translator/ZBridgeT7-debug.apk`.
2. Open **ZBridge T7** and wait for the final line.
3. On failure, the full result is copied to the clipboard and written to
   `/sdcard/Android/data/com.chucny.translator.t7/files/t7-result.txt`. Send that first failure before
   retrying; force-stop ZBridge T7 before every retry because a failed proxy load requires a fresh
   app process. Force-stop before intentionally running a successful test a second time as well.

Expected on-screen checkpoints:

```text
PASS runtime activation
PASS Call* methods and all value types
PASS fields
PASS strings
PASS arrays and release modes
PASS global/weak/local refs and monitors
PASS direct buffers
PASS exceptions guest -> Java -> guest -> Java
PASS RegisterNatives from JNI_OnLoad + nested calls
PASS JavaVM GetEnv
PASS guest pthread attach/call/detach
PASS two concurrent Java callers
T7 PASS
```

Device acceptance: `T7 PASS` on the OnePlus 13 on 2026-09-16 with APK SHA-256
`de344ba365acf12eee9e740ef6fb1ce71de1bc5cc182c7093ea516af2677612d`.

The guest probe intentionally excludes invalid-handle, oversized-buffer and invalid direct-buffer
calls: those failure paths remain covered by the mock-backed host suite, while real ART CheckJNI is
allowed to abort on invalid JNI use.

The string checkpoint follows ART's longstanding behavior for supplementary characters:
`GetStringUTFRegion` returns a four-byte UTF-8 sequence even though the JNI specification describes
the six-byte Modified UTF-8 surrogate form.
