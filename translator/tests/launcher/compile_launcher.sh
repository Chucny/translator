#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ANDROID_JAR=${ANDROID_JAR:-$HOME/android-sdk/platforms/android-36/android.jar}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -f "$ANDROID_JAR" ]; then
    echo "missing Android API jar: $ANDROID_JAR" >&2
    exit 1
fi

find "$ROOT/android/launcher/app/src/main/java" "$ROOT/tests/launcher/stubs" -name '*.java' -print \
    | LC_ALL=C sort > "$OUT/sources.txt"
javac -encoding UTF-8 -source 17 -target 17 -cp "$ANDROID_JAR" -d "$OUT/classes" @"$OUT/sources.txt"
echo "launcher Java compile PASS"
