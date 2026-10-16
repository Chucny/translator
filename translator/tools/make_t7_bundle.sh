#!/bin/sh
# Builds the generated inputs consumed by android/t7/project.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/t7"
PLUGIN="com.chucny.translator.t7probe"
ANDROID_JAR=${ANDROID_JAR:-$HOME/android-sdk/platforms/android-36/android.jar}

"$ROOT/tools/make_launcher_bundle.sh" >/dev/null
for required in "$ROOT/build/guest/lib/libzbt7probe.so" \
        "$ROOT/build/android-arm64/core/libzbproxy.so" "$ROOT/build/android-arm64/core/libzbridge.so"; do
    if [ ! -f "$required" ]; then
        echo "missing $required" >&2
        exit 1
    fi
done
if [ ! -f "$ANDROID_JAR" ]; then
    echo "missing Android API jar: $ANDROID_JAR" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/assets/plugins/$PLUGIN/lib" "$OUT/assets/plugins/$PLUGIN/proxy" \
    "$OUT/jniLibs/arm64-v8a" "$OUT/java/com/chucny/translator/core"
cp -r "$ROOT/build/launcher/assets/zb" "$OUT/assets/zb"
cp "$ROOT/build/guest/lib/libzbt7probe.so" "$OUT/assets/plugins/$PLUGIN/lib/"
cp "$ROOT/build/android-arm64/core/libzbproxy.so" "$OUT/assets/plugins/$PLUGIN/proxy/libzbt7probe.so"
cp "$ROOT/build/android-arm64/core/libzbridge.so" "$OUT/jniLibs/arm64-v8a/"
cp -r "$ROOT/android/t7/java/." "$OUT/java/"
cp "$ROOT/android/launcher/app/src/main/java/com/chucny/translator/core/ZBridge.java" \
    "$OUT/java/com/chucny/translator/core/ZBridge.java"
(cd "$OUT/assets" && find zb plugins -type f | LC_ALL=C sort) > "$OUT/assets/t7-files.txt"

CLASS=$(od -An -tu1 -j4 -N1 "$OUT/assets/plugins/$PLUGIN/lib/libzbt7probe.so" | tr -d ' ')
MACHINE=$(od -An -tu2 -j18 -N2 "$OUT/assets/plugins/$PLUGIN/lib/libzbt7probe.so" | tr -d ' ')
if [ "$CLASS" != 1 ] || [ "$MACHINE" != 40 ]; then
    echo "libzbt7probe.so is not ELF32 ARM" >&2
    exit 1
fi
CLASS=$(od -An -tu1 -j4 -N1 "$OUT/assets/plugins/$PLUGIN/proxy/libzbt7probe.so" | tr -d ' ')
MACHINE=$(od -An -tu2 -j18 -N2 "$OUT/assets/plugins/$PLUGIN/proxy/libzbt7probe.so" | tr -d ' ')
if [ "$CLASS" != 2 ] || [ "$MACHINE" != 183 ]; then
    echo "T7 proxy is not ELF64 AArch64" >&2
    exit 1
fi
for symbol in JNI_OnLoad Java_zb_T7_calls Java_zb_T7_fields Java_zb_T7_exceptions \
        Java_zb_T7_strings Java_zb_T7_arrays Java_zb_T7_references Java_zb_T7_directBuffers \
        Java_zb_T7_vm Java_zb_T7_attach; do
    if ! readelf -Ws "$OUT/assets/plugins/$PLUGIN/lib/libzbt7probe.so" | grep -q " $symbol$"; then
        echo "libzbt7probe.so is missing $symbol" >&2
        exit 1
    fi
done

find "$OUT/java" -name '*.java' -print | LC_ALL=C sort > "$OUT/java-sources.txt"
mkdir -p "$OUT/javac"
javac -encoding UTF-8 -source 17 -target 17 -cp "$ANDROID_JAR" -d "$OUT/javac" @"$OUT/java-sources.txt"
rm -rf "$OUT/javac" "$OUT/java-sources.txt"

echo "T7 bundle PASS"
du -sh "$OUT"
