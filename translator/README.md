# Chucny's Translator runtime

Translate compatible 32-bit ARM Android native code to AArch64 on ARM64 Android devices, with a launcher and host bridges for Android APIs.

> **Status: experimental.** Application compatibility, graphics and performance vary by device. No root or custom ROM is required; this project does not bypass application integrity controls.

## Architecture

```text
Android launcher (arm64)
└── guest process
    ├── imported app Java on the device's ART
    ├── libzbridge.so (arm64)
    │   ├── Dynarmic A32/T32 → AArch64 JIT
    │   ├── Linux syscall and guest memory/thread support
    │   └── JNI, EGL/GLES, input, looper and native-window bridges
    └── AOSP-derived arm32 linker and Bionic sysroot
```

The JNI bridge class is `com.chucny.translator.core.ZBridge`. Its JNI exports and Java declarations must remain in sync; `tools/check_zbridge_natives.py` validates them.

## Repository components

- `core/` — ARM translation core, guest process and Android host integration.
- `guest/` — guest compatibility libraries and test programs.
- `android/` — Android launcher/runtime builds and test applications.
- `sysroot/` — AOSP-derived arm32 linker and system libraries.
- `third_party/` — Dynarmic, Khronos registries and vendored dependencies. Each dependency retains its own license.
- `tools/` and `tests/` — generation, staging, verification and regression tests.

## Build requirements

The top-level [`README.md`](../README.md) documents the JDK 17 launcher build, runtime-only packaging policy, and Docker workflow.


- JDK 17 for the launcher
- Android SDK Platform 35 and Android NDK r29
- CMake 3.22+, Ninja, Clang, Python 3
- Boost headers (1.57 or newer; tested with project toolchain versions)

### Native Android runtime

From this directory, configure with the NDK toolchain and a Boost header directory:

```sh
cmake -S . -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DZB_BUILD_TESTS=OFF \
  -DBoost_INCLUDE_DIR="$BOOST_ROOT"
ninja -C build/android-arm64 zbridge zbproxy
```

The sysroot binaries used by the current project are included in `sysroot/` and were extracted from an AOSP GSI by `tools/extract_sysroot.sh`. Their attribution and licensing notes are in `../docs/THIRD_PARTY_NOTICES.md`.

### Host tests

On a supported Linux host with required dependencies:

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Guest integration tests require an AArch64 Linux host and the corresponding built guest artifacts.

## Credits

- **AOSP contributors** — Android guest sysroot and Bionic system libraries.
- **ZailoxTT** — original translator/runtime code, substantially modified for this project.
- **Dynarmic contributors** — ARM dynamic recompiler (0BSD; see `third_party/dynarmic/LICENSE.txt`).

More licensing detail is in the root `LICENSE`, `../docs/THIRD_PARTY_NOTICES.md`, and the notices shipped beside each dependency. Unless a source component is explicitly identified as third-party, the project is distributed under the GNU Affero General Public License version 3.0.
