package com.chucny.translator.core;

/** JNI entry point of libzbridge.so: runs an arm32 Android executable in this process. */
public final class ZBridge {
    static {
        System.loadLibrary("zbridge");
    }

    private ZBridge() {}

    /**
     * Blocks until the guest exits.
     *
     * @param sysroot directory holding system/bin/linker and system/lib (arm32 bionic)
     * @param argv argv[0] is the absolute path of the guest executable
     * @param envp guest environment ("NAME=VALUE"), or null to pass this process's environment
     * @return the guest exit status, or 128 + signal for a guest crash
     */
    public static native int runExecutable(String sysroot, String[] argv, String[] envp);
}
