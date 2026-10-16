#!/bin/sh
# Generates the native/assets inputs consumed by android/launcher.
# A public checkout includes the verified runtime inputs. Rebuild them from source first when needed.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/launcher"
ASSETS="$OUT/assets"
ZB="$ASSETS/zb"
JNI="$OUT/jniLibs/arm64-v8a"
ANDROID="$ROOT/build/android-arm64/core"
GUEST="$ROOT/build/guest"
PREBUILT="$ROOT/../runtime"

SYSROOT_FILES="
system/bin/linker
system/lib/ld-android.so
system/lib/libc++.so
system/lib/libc.so
system/lib/libdl_android.so
system/lib/libdl.so
system/lib/liblog.so
system/lib/libm.so
system/lib/libstdc++.so
system/lib/libz.so
"

# Prefer a complete source-built runtime when present. Otherwise use the checked-in runtime bundle.
if [ -f "$ANDROID/libzbridge.so" ] && [ -f "$ANDROID/libzbproxy.so" ] && [ -f "$GUEST/zbhost" ]; then
    SOURCE="$GUEST"
    BRIDGE="$ANDROID/libzbridge.so"
    PROXY="$ANDROID/libzbproxy.so"
elif [ -f "$PREBUILT/jniLibs/arm64-v8a/libzbridge.so" ] && \
        [ -f "$PREBUILT/host/libzbproxy.so" ] && [ -f "$PREBUILT/guest/zbhost" ]; then
    SOURCE="$PREBUILT/guest"
    BRIDGE="$PREBUILT/jniLibs/arm64-v8a/libzbridge.so"
    PROXY="$PREBUILT/host/libzbproxy.so"
else
    echo "missing a complete built runtime or checked-in runtime/ directory" >&2
    exit 1
fi

for relative in $SYSROOT_FILES; do
    if [ ! -f "$ROOT/sysroot/$relative" ]; then
        echo "missing $ROOT/sysroot/$relative" >&2
        exit 1
    fi
done
for library in libzbcompat.so libzbjni.so libGLESv2.so libGLESv1_CM.so libEGL.so libandroid.so libjnigraphics.so; do
    if [ ! -f "$SOURCE/lib/$library" ]; then
        echo "missing runtime library: $SOURCE/lib/$library" >&2
        exit 1
    fi
done

rm -rf "$OUT"
mkdir -p "$ZB/sysroot" "$ZB/guest/lib" "$ZB/host" "$JNI"
for relative in $SYSROOT_FILES; do
    mkdir -p "$ZB/sysroot/$(dirname "$relative")"
    cp "$ROOT/sysroot/$relative" "$ZB/sysroot/$relative"
done
cp "$SOURCE/zbhost" "$ZB/guest/zbhost"
cp "$SOURCE"/lib/*.so "$ZB/guest/lib/"
cp "$PROXY" "$ZB/host/libzbproxy.so"
cp "$BRIDGE" "$JNI/libzbridge.so"

(cd "$ASSETS" && find zb -type f | LC_ALL=C sort) > "$ASSETS/zb-files.txt"
VERSION=$(cd "$ASSETS" && while IFS= read -r file; do sha256sum "$file"; done < zb-files.txt | sha256sum | cut -d' ' -f1)
printf '%s\n' "$VERSION" > "$ASSETS/zb-version.txt"

py -3 "$ROOT/tools/check_launcher_bundle.py" "$OUT"
du -sh "$OUT"
