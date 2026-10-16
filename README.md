# Chucny's Translator

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)

An Android launcher and ARM32-to-ARM64 translation runtime for running compatible 32-bit Android applications on 64-bit ARM devices. This repository contains the launcher, native runtime, build tools, tests, and the AOSP-derived guest sysroot used by the runtime.

> **Status:** experimental. Application compatibility varies; review the documented limitations and test on the target device before relying on it.

## Repository layout

```text
.
├── build/                   Payload generation and APK verification tooling
├── docs/                    Build, architecture, testing, and attribution notes
├── launcher/                Android launcher app (Java Gradle project)
├── runtime/                 Prebuilt translator runtime and JNI bridge inputs
├── tests/                   Launcher contract and stability tests
└── translator/              Native ARM32 guest runtime and tools
    ├── android/launcher/     Standalone launcher project for runtime bundle inputs
    ├── core/                ARM-to-ARM runtime, JNI and Android host bridges
    ├── guest/               Guest-side libraries and test programs
    ├── sysroot/             AOSP-derived 32-bit system libraries and linker
    ├── third_party/         Vendored dependencies with their own license notices
    └── tools/                Runtime build and staging scripts
```

## Requirements

- JDK 17
- Android SDK Platform 35 and Android NDK r29
- Gradle 8.9 (the repository's Gradle wrapper is also available)
- CMake 3.22 or newer and Ninja
- Python 3.10+; the payload packer also requires `cryptography`
- Boost headers for native translation builds
- `adb` for device installation and smoke testing

Use `local.properties` only for machine-local SDK configuration; it is intentionally not committed.

## Build a debuggable Android APK

The repository includes the matching native runtime inputs under `runtime/`. To package them and build the standalone Android launcher:

```sh
./translator/tools/make_launcher_bundle.sh
cd translator/android/launcher
./gradlew assembleDebug
```

The resulting APK is `translator/android/launcher/app/build/outputs/apk/debug/app-debug.apk`. The root `launcher/` project is the separate hardened/obfuscated variant; see [`README-OBFUSCATION.md`](README-OBFUSCATION.md).

On Windows, run the shell scripts from Git Bash and use the corresponding `.bat` Android SDK tools. `Dockerfile` and `docker-compose.yml` provide a JDK 17 container environment for consistent Java/Gradle builds; Android SDK/NDK installation and device access remain host responsibilities.

Alternatively, build the runtime-only hardened APK with `./build-obfuscated.sh` after installing the Android NDK and Boost headers; this packages the runtime from `runtime/` and never bundles a third-party app.

Install and launch on an attached Android device:

```sh
adb install -r translator/android/launcher/app/build/outputs/apk/debug/app-debug.apk
adb shell monkey -p com.chucny.translator.bridge 1
```

## Tests

```sh
./translator/tests/launcher/run_tests.sh
./tests/launcher/run_stability_tests.sh
```

The native CMake project contains host unit tests. Configure with `-DZB_BUILD_TESTS=ON` and run `ctest --test-dir translator/build/host --output-on-failure` on a supported host.

## Runtime inputs and sysroot

The checked-in `translator/sysroot/` binaries are extracted from an Android Open Source Project (AOSP) Generic System Image. Their provenance and license attribution are documented in [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md). The public launcher build uses the verified files under `runtime/`; maintainers can regenerate runtime binaries from the native source and toolchain. No Android application package is included in this repository.

## License

Unless a file or directory says otherwise, project code and documentation are licensed under the GNU Affero General Public License version 3.0; see [`LICENSE`](LICENSE). Third-party components retain their own licenses, documented alongside the dependency and in [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md).

## Credits

- **AOSP** — Android guest sysroot (linker and Bionic system libraries).
- **ZailoxTT** — original translator/runtime code, substantially modified for this project.
- **Dynarmic contributors** — ARM dynamic recompilation library; see `translator/third_party/dynarmic/LICENSE.txt`.

See the in-app Settings → About → Credits section for the same acknowledgements.

## Repository hygiene

Generated APKs, third-party application packages, signing material, build caches, local machine configuration, and Windows executable tools are intentionally excluded.
