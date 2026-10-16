#!/bin/sh
# Runs the zbrun guest tests on the phone (Termux). Unpacked by tools/make_termux_bundle.sh as
# zb/run_tests.sh. Usage: cd zb; sh run_tests.sh
# Diagnostics: out/<test>.stderr, plus logcat tag zbridge.
cd "$(dirname "$0")" || exit 1
HERE=$(pwd)
mkdir -p out
failures=0

run_case() {
    name=$1
    expected_exit=$2
    shift 2
    "$HERE/zbrun" --sysroot "$HERE/sysroot" $ZB_EXTRA "$HERE/guest/$name" "$@" >"out/$name.stdout" 2>"out/$name.stderr"
    code=$?
    if [ "$code" != "$expected_exit" ]; then
        echo "FAIL $name: exit $code, expected $expected_exit (see out/$name.stderr)"
        failures=$((failures + 1))
        return
    fi
    if ! cmp -s "out/$name.stdout" "expected/$name.out"; then
        echo "FAIL $name: stdout differs (compare out/$name.stdout with expected/$name.out)"
        failures=$((failures + 1))
        return
    fi
    echo "PASS $name"
}

ZB_EXTRA=""
run_case hello_static 7 world
run_case hello_dynamic 3 "$HERE/out/io.tmp"
run_case threads_dynamic 0
run_case kuser_dynamic 0
run_case signals_dynamic 134
run_case syscalls_dynamic 0 "$HERE/out/syscalls_tmp"
run_case log_dynamic 0
ZB_EXTRA="--env LD_LIBRARY_PATH=$HERE/guest/lib"
run_case cxx_dynamic 0
run_case or_dlopen_dynamic 0 "$HERE/or"

if [ "$failures" -ne 0 ]; then
    echo "$failures guest test(s) failed"
    exit 1
fi
echo "all guest tests passed"
echo "Phase 3 check: the guest log line must appear in: su -c 'logcat -d -s zbguest'"
