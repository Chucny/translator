# Launcher stability and recovery

The launcher includes a bounded guest-process supervisor and retry/recovery policy. Platform-specific behavior still varies by Android release and OEM.

## Run host-side tests

```sh
./tests/launcher/run_stability_tests.sh
./translator/tests/launcher/run_tests.sh
```

## Device smoke test

Install the debuggable build from `build/outputs/chucnys-translator-debug.apk`, open the launcher, and confirm the Apps, Add apps, and Settings tabs render. Settings → Diagnose can copy the app log, runtime report, and device details for a test report.

To observe launcher process output:

```sh
adb logcat -c
adb logcat -s Translator
```

For translated-app recovery tests, import an application you are authorized to test and exercise a normal close, background/resume, and process-restart scenario. Do not use a copyrighted APK supplied outside this repository as a test fixture.
