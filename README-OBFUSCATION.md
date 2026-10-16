# Release APK hardening

The release variant uses R8 to shrink and rename the launcher, then the build scripts inspect the packaged artifact. The runtime is staged from the checked-in `runtime/` inputs to `build/launcher/`; `build/pack_payloads.py` creates an encrypted runtime payload and generated JNI library name before Gradle packaging.

## Public build policy

This repository builds **without a bundled application APK**. `build-obfuscated.sh` sets `BUNDLED_APP=none`, so the launcher starts with an empty library and users import a compatible APK from their own device. Third-party application packages do not belong in this repository or its public build artifacts. The helper stages the checked-in runtime inputs before encryption and Android packaging.

```sh
./build-obfuscated.sh
```

The output is `build/outputs/chucnys-translator-obfuscated.apk`. Generated payloads, signing keys, maps, and APKs are ignored by Git. Keep mapping files private if you need them to symbolicate a particular local build.

## What the APK verification checks

- the debug and release variants have the expected `android:debuggable` state;
- debug-only logger markers do not leak into the release build;
- the native/JNI bridge names agree with `com.chucny.translator.core.ZBridge`;
- the staged runtime payload round-trips byte-for-byte;
- the APK has no mapping artifacts, native symbol tables, debug sections, or compiler banners that the audit flags;
- no third-party client APK is present in the payload.

Run the checks with:

```sh
py -3 build/check_debug_build.py build/outputs/chucnys-translator-debug.apk
py -3 build/verify_payloads.py build/outputs/chucnys-translator-obfuscated.apk
py -3 build/audit_apk.py build/outputs/chucnys-translator-obfuscated.apk
```

Hardening is not a security boundary: code and runtime data delivered to a device can be inspected. Do not store credentials or signing secrets in generated APKs or source control.
