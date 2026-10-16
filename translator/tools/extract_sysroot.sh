#!/bin/sh
# Extracts the arm32 bionic sysroot used by zbrun from an AOSP aosp_arm64 GSI (Android 17 QPR2).
# Needs: curl, unzip, sha256sum, debugfs (e2fsprogs). The image is ext4.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://dl.google.com/developers/android/cinnamonbun/images/gsi/aosp_arm64-exp-CP41.260814.003.B1-16166531-e6cb3bc5.zip
SHA=e6cb3bc521fb4a8b4c8e62f8557c6ae0ff10662a6838cfb346a32ec9c9134e22
CACHE="$ROOT/.cache/gsi"
OUT="$ROOT/sysroot"
IMG="$CACHE/system.img"

mkdir -p "$CACHE" "$OUT/system/lib" "$OUT/system/bin"
if [ ! -f "$IMG" ]; then
    [ -f "$CACHE/gsi.zip" ] || curl -fL -o "$CACHE/gsi.zip" "$URL"
    echo "$SHA  $CACHE/gsi.zip" | sha256sum -c
    unzip -o "$CACHE/gsi.zip" system.img -d "$CACHE"
fi

dump() {
    rm -f "$2"
    debugfs -R "dump $1 $2" "$IMG" >/dev/null 2>&1
    if [ ! -s "$2" ]; then
        echo "missing $1 in image"
        exit 1
    fi
}

# /system/lib/{libc,libm,libdl}.so are symlinks into the runtime APEX; the bootstrap
# copies are regular files built from the same bionic.
dump /system/bin/bootstrap/linker "$OUT/system/bin/linker"
for f in libc.so libm.so libdl.so libdl_android.so; do
    dump "/system/lib/bootstrap/$f" "$OUT/system/lib/$f"
done
for f in ld-android.so liblog.so libz.so libc++.so libstdc++.so; do
    dump "/system/lib/$f" "$OUT/system/lib/$f"
done

echo "sysroot ready in $OUT"
echo "to free space: rm $CACHE/gsi.zip $IMG"
