#!/bin/sh
# Packs the Android build of zbrun with the sysroot and guest tests into a tarball that runs in
# Termux (or any shell on the phone): build/zb-termux.tar.gz, and a copy in $OUT_DIR if given.
# Prerequisites: ninja -C build/android-arm64 zbrun, tools/build_guest.sh, tools/run_guest_tests.sh.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE="$ROOT/build/termux/zb"
ZBRUN="$ROOT/build/android-arm64/cli/zbrun/zbrun"
TESTS="hello_static hello_dynamic threads_dynamic kuser_dynamic signals_dynamic syscalls_dynamic log_dynamic cxx_dynamic or_dlopen_dynamic"

for required in "$ZBRUN" "$ROOT/sysroot/system/bin/linker" "$ROOT/build/or/libApplicationMain.so"; do
    if [ ! -e "$required" ]; then
        echo "missing $required (see the prerequisites at the top of this script)"
        exit 1
    fi
done

rm -rf "$ROOT/build/termux"
mkdir -p "$STAGE/guest/lib" "$STAGE/or" "$STAGE/expected"
cp "$ZBRUN" "$STAGE/zbrun"
cp -r "$ROOT/sysroot" "$STAGE/sysroot"
for t in $TESTS; do
    cp "$ROOT/build/guest/$t" "$STAGE/guest/"
    cp "$ROOT/guest/tests/expected/$t.out" "$STAGE/expected/"
done
cp "$ROOT"/build/guest/lib/*.so "$STAGE/guest/lib/"
cp "$ROOT"/build/or/*.so "$STAGE/or/"
cp "$ROOT/tools/termux_run_tests.sh" "$STAGE/run_tests.sh"
chmod 755 "$STAGE/zbrun" "$STAGE/run_tests.sh"

tar -C "$ROOT/build/termux" -czf "$ROOT/build/zb-termux.tar.gz" zb
ls -la "$ROOT/build/zb-termux.tar.gz"
if [ -n "${OUT_DIR:-}" ]; then
    cp "$ROOT/build/zb-termux.tar.gz" "$OUT_DIR/"
    echo "copied to $OUT_DIR/zb-termux.tar.gz"
fi
