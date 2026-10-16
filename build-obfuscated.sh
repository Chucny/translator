#!/bin/sh
# Builds the obfuscated APK, entirely from this folder.
#
#   sh build-obfuscated.sh
#
# This public build always excludes third-party application APKs; users import their own app.
# Nothing outside this repository is written: the launcher source and staged runtime live here, and
# the toolchain (Gradle, JDK, NDK) is only read.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE"
LAUNCHER="$HERE/launcher"
ARTIFACT="$HERE/build/outputs/chucnys-translator-obfuscated.apk"
export BUNDLED_APP=none
PYTHON=${PYTHON:-py}
PYTHON_ARGS="-3"
if ! command -v "$PYTHON" >/dev/null 2>&1 && [ -x "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}/Programs/Python/Python313/python.exe" ]; then
    PYTHON="${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}/Programs/Python/Python313/python.exe"
    PYTHON_ARGS=""
fi

mkdir -p "$HERE/build/outputs"

# Bundle the checked-in native runtime inputs. Rebuild from the current source first if needed.
sh "$HERE/translator/tools/make_launcher_bundle.sh"
sh "$HERE/strip-runtime-binaries.sh"

# Packs the runtime-only payload, copies the bridge library under a generated name, and writes the
# generated Keys class that names them. Names are freshly random per build (PAYLOAD_SEED makes it
# reproducible).
"$PYTHON" $PYTHON_ARGS "$HERE/build/pack_payloads.py"

# Fresh names for every build keeps the renaming from being reproducible between two APKs of the
# same source. Set OBFUSCATION_SEED for a reproducible build instead.
"$PYTHON" $PYTHON_ARGS "$HERE/build/make_dictionary.py" >/dev/null

# Gradle 8.9 predates this machine's default JDK 26 and dies on it with a bare version number, so
# the toolchain JDK is selected explicitly; the Windows gradle launcher needs a Windows path.
if [ -z "${JAVA_HOME:-}" ]; then
    echo "Set JAVA_HOME to JDK 17 (or use the JDK Docker container)." >&2
    exit 1
fi
export JAVA_HOME
GRADLE=${GRADLE:-gradle}
"$GRADLE" -p "$LAUNCHER" clean assembleRelease --offline

BUILT="$LAUNCHER/app/build/outputs/apk/release/app-release.apk"

# The dex post-pass. R8 keeps line numbers and source-file references whatever `-keepattributes` says
# (measured, see README-OBFUSCATION.md), so the packaged dex is rewritten: every code_item loses its
# debug_info_off and every class_def loses its source_file_idx. That changes the archive, so it is
# re-aligned - the repack would otherwise drop the page alignment ART needs for lib/*.so - and
# re-signed with the *same* key Gradle signed with, read from the same place Gradle reads it. An APK
# whose signer changed cannot upgrade the installed one.
STRIPPED="$HERE/build/payloads/final.unsigned.apk"
ALIGNED="$HERE/build/payloads/final.aligned.apk"
"$PYTHON" $PYTHON_ARGS "$HERE/build/strip_dex_debug.py" --in "$BUILT" --out "$STRIPPED"

SIGN_PROPS="$HOME/.chucnys-translator/signing.properties"
if [ -f "$SIGN_PROPS" ]; then
    prop() { sed -n "s/^$1=//p" "$SIGN_PROPS" | tr -d '\r' | head -1; }
    KEYSTORE=$(prop storeFile); KS_PASS=$(prop storePassword)
    KEY_ALIAS=$(prop keyAlias); KEY_PASS=$(prop keyPassword)
else
    KEYSTORE="$HOME/.android/debug.keystore"; KS_PASS=android
    KEY_ALIAS=androiddebugkey; KEY_PASS=android
fi

# `-f`, not `-x`: on the Windows build host the SDK's .exe/.bat files carry no execute bit, so an
# `-x` test rejects tools that run perfectly well. `if`, not `[ ] && ...`: under `set -e` a
# short-circuiting AND-list aborts the script on the first candidate that does not exist.
TOOLS="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}/build-tools/35.0.0"
if [ ! -d "$TOOLS" ]; then TOOLS="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}/build-tools/34.0.0"; fi
ZIPALIGN=""
APKSIGNER=""
for name in zipalign zipalign.exe; do
    if [ -f "$TOOLS/$name" ]; then ZIPALIGN="$TOOLS/$name"; break; fi
done
for name in apksigner apksigner.bat; do
    if [ -f "$TOOLS/$name" ]; then APKSIGNER="$TOOLS/$name"; break; fi
done
if [ -z "$ZIPALIGN" ] || [ -z "$APKSIGNER" ]; then
    echo "missing zipalign or apksigner under $TOOLS" >&2
    exit 1
fi

"$ZIPALIGN" -p -f 4 "$STRIPPED" "$ALIGNED"
# v1 (JAR) signing is off on purpose: minSdk is 26, so v2 is what the platform checks, and v1 would
# add META-INF/MANIFEST.MF, *.SF and *.RSA entries to an archive whose whole point is that its entry
# list says nothing.
"$APKSIGNER" sign --ks "$KEYSTORE" --ks-pass "pass:$KS_PASS" --key-pass "pass:$KEY_PASS" \
    --ks-key-alias "$KEY_ALIAS" --v1-signing-enabled false --v2-signing-enabled true "$ALIGNED"
cp "$ALIGNED" "$ARTIFACT"
rm -f "$STRIPPED" "$ALIGNED"

echo
echo "built $ARTIFACT"
wc -c < "$ARTIFACT"

# Both checkers read the packaged APK, not the build directory.
#
#   verify_payloads.py  decrypts the two payload containers with the key this build generated and
#                       compares every entry against what was staged: a payload that cannot be read
#                       back byte-for-byte means a launcher that cannot install its own runtime.
#   audit_apk.py        fails on a mapping file, a symbol table, DWARF, a compiler banner, a
#                       debug-info residue, a payload that is not opaque, or an entry name that
#                       gives its contents away. See README-OBFUSCATION.md.
"$PYTHON" $PYTHON_ARGS "$HERE/build/verify_payloads.py" "$ARTIFACT"
"$PYTHON" $PYTHON_ARGS "$HERE/build/audit_apk.py" "$ARTIFACT"

echo "R8 mapping (the de-obfuscation key, keep it out of the shipped APK):"
ls -l "$LAUNCHER/app/build/outputs/mapping/release/mapping.txt"
