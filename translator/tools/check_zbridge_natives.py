#!/usr/bin/env python3
"""Checks that com.chucny.translator.core.ZBridge and the native bridge agree.

Every `public static native` method of ZBridge.java must have exactly one
`Java_com_chucny_translator_core_ZBridge_<name>` definition in core/android/zbridge_jni.cpp, and the
other way round. A mismatch is invisible on this machine and only shows on the device as an
UnsatisfiedLinkError, which the ROM then hides with the rest of our logcat output.
"""

import re
import sys
from pathlib import Path

JAVA = "android/launcher/app/src/main/java/com/chucny/translator/core/ZBridge.java"
NATIVE = "core/android/zbridge_jni.cpp"

JAVA_NATIVE = re.compile(r"public\s+static\s+native\s+[\w\[\]<>., ]+?\s+(\w+)\s*\(")
JNI_EXPORT = re.compile(r"\bJava_com_chucny_translator_core_ZBridge_(\w+)\s*\(")


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    java_text = (root / JAVA).read_text(encoding="utf-8")
    native_text = (root / NATIVE).read_text(encoding="utf-8")

    declared = sorted(set(JAVA_NATIVE.findall(java_text)))
    defined = sorted(set(JNI_EXPORT.findall(native_text)))
    if not declared:
        print("no native methods found in " + JAVA, file=sys.stderr)
        return 1

    missing = [name for name in declared if name not in defined]
    extra = [name for name in defined if name not in declared]
    for name in missing:
        print("ZBridge." + name + " is declared native but not defined in " + NATIVE, file=sys.stderr)
    for name in extra:
        print(NATIVE + " defines " + name + ", which ZBridge.java does not declare", file=sys.stderr)
    if missing or extra:
        return 1

    print("translator JNI exports OK: " + ", ".join(declared))
    return 0


if __name__ == "__main__":
    sys.exit(main())
