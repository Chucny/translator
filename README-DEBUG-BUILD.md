# Debuggable Android build

The debug variant preserves class/method names and line numbers, sets `android:debuggable=true`, disables R8, and includes the verbose `DebugLog` implementation. The public build carries only the runtime payload; it does not bundle third-party application APKs.

## Build and install

With JDK 17, Android SDK/NDK, Gradle, Python 3 and `cryptography` available, the script stages the checked-in runtime inputs and builds a runtime-only APK:

```sh
./build-debug.sh
adb install -r build/outputs/chucnys-translator-debug.apk
adb shell monkey -p com.chucny.translator.bridge 1
```

The script checks the built APK's manifest and DEX markers. It does not require a bundled application; import an app through the UI if you want to test translated guest execution.

## Collect logs

```sh
adb logcat -c
adb logcat -s Translator
```

On-device diagnostic files (after launching the app) are under:

```text
/sdcard/Android/data/com.chucny.translator.bridge/files/zb-app-log.txt
/sdcard/Android/data/com.chucny.translator.bridge/files/zb-runtime-report.txt
/sdcard/Android/data/com.chucny.translator.bridge/files/zb-errors.txt
```

Pull the app log when permitted by the device OS:

```sh
adb pull /sdcard/Android/data/com.chucny.translator.bridge/files/zb-app-log.txt
```

The Settings → Diagnose section also copies reports and device details to the clipboard. Treat logs as potentially sensitive; review them before sharing.
