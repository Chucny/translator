#!/bin/sh
# Builds the debuggable APK with the verbose app log.
#
#   sh build-debug.sh
#
# This is the same launcher as build-obfuscated.sh ships, built for diagnosis instead of hiding:
# real class and method names, real line numbers, android:debuggable="true", no R8, and the
# DebugLog from app/src/debug, which writes everything to logcat *and* to
# /sdcard/Android/data/com.chucny.translator.bridge/files/zb-app-log.txt (see
# README-DEBUG-BUILD.md for how to read it and what it says).
#
# It uses the debug signing key and applicationId, so an existing debug install can be upgraded:
# `adb install -r build/outputs/chucnys-translator-debug.apk`.
# The imported plugin and everything under /data/data/com.chucny.translator.bridge survive, which
# is the point - the crash being investigated stays reproducible.
#
# The runtime-only payload is regenerated each time; APK outputs stay under ignored build/outputs/.
export ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
export ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT:-$ANDROID_NDK_HOME}
PYTHON=${PYTHON:-py}
PYTHON_ARGS="-3"

# Git Bash on Windows may not have the py launcher installed even when Python is available.
if ! command -v "$PYTHON" >/dev/null 2>&1 && [ -x "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}/Programs/Python/Python313/python.exe" ]; then
    PYTHON="${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}/Programs/Python/Python313/python.exe"
    PYTHON_ARGS=""
fi
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE"
LAUNCHER="$HERE/launcher"

# Use JDK 17, the version supported by the Android Gradle Plugin and this project's Gradle build.
if [ -z "${JAVA_HOME:-}" ]; then
    echo "Set JAVA_HOME to a JDK 17 installation (or run inside the JDK Docker container)." >&2
    exit 1
fi
export JAVA_HOME
GRADLE=${GRADLE:-gradle}

# Stage the versioned runtime inputs, strip the generated build copy, and package runtime only.
# Third-party application APKs are never bundled in public builds.
sh "$HERE/translator/tools/make_launcher_bundle.sh"
sh "$HERE/strip-runtime-binaries.sh"
BUNDLED_APP=none "$PYTHON" $PYTHON_ARGS "$HERE/build/pack_payloads.py"

# assembleDebug only: no R8, no shrinking, no lintVital. Fast on purpose.
"$GRADLE" -p "$LAUNCHER" assembleDebug --offline

BUILT="$LAUNCHER/app/build/outputs/apk/debug/app-debug.apk"
mkdir -p "$HERE/translator/build/outputs"
cp "$BUILT" "$HERE/translator/build/outputs/chucnys-translator-debug.apk"

echo
echo "built $HERE/translator/build/outputs/chucnys-translator-debug.apk"
wc -c < "$HERE/translator/build/outputs/chucnys-translator-debug.apk"

# Read back what was actually packaged, rather than trusting the variant name: the debuggable flag
# is the whole point of this artifact, and the verbose logger has to be in the dex.
#
"$PYTHON" $PYTHON_ARGS "$HERE/build/check_debug_build.py" "$HERE/translator/build/outputs/chucnys-translator-debug.apk"
