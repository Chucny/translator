#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ANDROID_JAR=${ANDROID_JAR:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/android-sdk}}/platforms/android-35/android.jar}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -f "$ANDROID_JAR" ]; then
    echo "missing Android API jar: $ANDROID_JAR" >&2
    exit 1
fi

"${JAVAC:-javac}" --release 17 -encoding UTF-8 -cp "$ANDROID_JAR" -d "$OUT" \
    "$ROOT/android/launcher/app/src/main/java/com/chucny/translator/launcher/PluginFiles.java" \
    "$ROOT/android/launcher/app/src/main/java/com/chucny/translator/launcher/PluginClassLoader.java" \
    "$ROOT/android/launcher/app/src/main/java/com/chucny/translator/launcher/PluginRecord.java" \
    "$ROOT/tests/launcher/LauncherContractsTest.java"
"${JAVA:-java}" -ea -cp "$OUT:$ANDROID_JAR" com.chucny.translator.launcher.LauncherContractsTest
