# Phase 4 acceptance: Orange Roulette on the OnePlus 13 (2026-09-16)

The production launcher imported the untouched Orange Roulette APK and launched it in the
`:guest` process. The runtime report below is the Task 8 evidence; it was read from the launcher
UI ("Last run report"), because OxygenOS hides third-party logcat output.

## What the run proves

- **All six arm32 libraries loaded through the translator** (`proxy-loads: 6`, `proxy-failures: 0`).
- **Both guest `JNI_OnLoad` entry points ran** (`liblime.so`, `libopenal.so`), each accepted.
- **20 native methods were registered**, matching the 19 in `org.haxe.lime.Lime` plus
  `org.haxe.HXCPP.main`.
- **The game ran its own native logic** up to GLES setup: 45 host calls, 18 distinct, all in
  `libGLESv2.so`.
- **No JNI error and no launcher diagnostic** at any point.

Phase 4 (synthesized 32-bit JNIEnv) is therefore complete: guest native code and Java call each
other correctly on a real device.

## Where it stops, and why that is expected

The generated GLES stubs log the call, return `r0 = 0` and continue, so the game receives 0 for
every object it creates. It crashed with `SIGSEGV read of 0x00000004` in
`libApplicationMain.so+0x26abe4`, the usual result of dereferencing a null GL object.

That is the Phase 5 boundary: no `AAsset*` call was reached, because rendering setup happens
first.

## Report

```text
chucnys-translator-runtime-report 1
plugin: /data/data/com.chucny.translator.launcher/files/plugins/com.heyhouser.OrangeRoulette targetSdk 16
proxy-loads: 6
proxy-failures: 0
proxy-loaded: libstd.so jni=0x00010006
proxy-loaded: libregexp.so jni=0x00010006
proxy-loaded: libzlib.so jni=0x00010006
proxy-loaded: libopenal.so jni=0x00010004
proxy-loaded: liblime.so jni=0x00010004
proxy-loaded: libApplicationMain.so jni=0x00010006
jni-onload-calls: 2
jni-onload: libopenal.so ok jni=0x00010004
jni-onload: liblime.so ok jni=0x00010004
registered-natives: 20
unimplemented-host-calls: 45
unimplemented-distinct: 18
first-unimplemented: libGLESv2.so glGenRenderbuffers
unimplemented: libGLESv2.so glGenRenderbuffers x1
unimplemented: libGLESv2.so glGenFramebuffers x1
unimplemented: libGLESv2.so glGenBuffers x2
unimplemented: libGLESv2.so glCreateShader x1
unimplemented: libGLESv2.so glShaderSource x1
unimplemented: libGLESv2.so glCompileShader x1
unimplemented: libGLESv2.so glGetShaderiv x2
unimplemented: libGLESv2.so glDeleteShader x1
unimplemented: libGLESv2.so glGetUniformLocation x4
unimplemented: libGLESv2.so glGenTextures x3
unimplemented: libGLESv2.so glBindTexture x6
unimplemented: libGLESv2.so glTexParameteri x12
unimplemented: libGLESv2.so glTexImage2D x3
unimplemented: libGLESv2.so glBindRenderbuffer x2
unimplemented: libGLESv2.so glBindFramebuffer x2
unimplemented: libGLESv2.so glRenderbufferStorage x1
unimplemented-more: 2
guest-exit: guest SIGSEGV: read of 0x00000004, pc 0xfcd6ebe4 in .../lib/libApplicationMain.so offset 0x26abe4
```

## What Phase 5 must deliver, from this evidence

- Object creation and binding: `glGen*`, `glCreateShader`, `glCreateProgram`, `glBind*`.
- Shader sources and queries: `glShaderSource`, `glCompileShader`, `glGetShaderiv`,
  `glGetUniformLocation`.
- Texture upload: `glTexParameteri`, `glTexImage2D`.
- Framebuffers and renderbuffers: `glBindFramebuffer`, `glRenderbufferStorage`.
- Then `AAsset*`, which the game reaches only after rendering starts.
