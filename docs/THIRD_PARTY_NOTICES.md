# Third-party notices and credits

## Android Open Source Project (AOSP) sysroot

`translator/sysroot/system/` contains the arm32 linker and system libraries extracted from an AOSP Generic System Image by `translator/tools/extract_sysroot.sh`.

AOSP and Android are trademarks of Google LLC. The relevant Android platform source components are distributed under the Apache License, Version 2.0. The authoritative license and component notices are available in the corresponding AOSP source tree at <https://android.googlesource.com/>. The sysroot binaries are retained as runtime inputs and are not original works of this project.

**Credit:** AOSP contributors, for the Android guest sysroot and Bionic system libraries.

## Original runtime contributions

**Credit:** ZailoxTT, for original translator/runtime code that has been substantially modified for Chucny's Translator. The project has permission from the relevant copyright holder to distribute the combined project under AGPL-3.0. Existing upstream author attribution is retained where present.

## Dynarmic and vendored dependencies

Dynarmic and its vendored dependencies are in `translator/third_party/dynarmic/`. Dynarmic is available under the 0BSD license; see `translator/third_party/dynarmic/LICENSE.txt`. Each vendored dependency retains its own license file. The Khronos OpenGL/EGL registry inputs in `translator/third_party/registry/` carry their own copyright and Apache-2.0 notices in the source files.

Third-party works keep their applicable license terms; the root AGPL-3.0 license does not replace those terms.
