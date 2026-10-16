#pragma once

/* JNI bridge ABI between the host (core/src/jni/host_jni.cpp) and the guest JNIEnv library
 * (guest/zbjni/zbjni.c -> libzbjni.so). C-compatible: the arm32 guest includes this header. */

#include <stdint.h>

#define ZB_JNI_PROTOCOL_VERSION 1u

/* Host-call indices. 0xFC00-0xFCFF are the flat JNI host calls listed in zb/jni_hostcalls.h,
 * which tools/gen_jni.py generates. 0xFB00-0xFBFF are JNINativeInterface slots configured as
 * host stubs (0xFB00 + slot number). tools/gen_stubs.py keeps its indices below 0xFB00. */
#define ZB_JNI_SLOT_STUB_FIRST 0xFB00u
#define ZB_JNI_SLOT_STUB_LAST 0xFBFFu
#define ZB_JNI_HOST_CALL_FIRST 0xFC00u
#define ZB_JNI_HOST_CALL_LAST 0xFCFFu

/* A shorty buffer: the return type, up to 255 parameters, and the NUL. */
#define ZB_JNI_SHORTY_SIZE 257u

/* kind argument of the CallMethodA host call. */
#define ZB_JNI_CALL_VIRTUAL 0u
#define ZB_JNI_CALL_NONVIRTUAL 1u
#define ZB_JNI_CALL_STATIC 2u
#define ZB_JNI_CALL_NEW_OBJECT 3u

/* Published by the constructor of libzbjni.so through the Register host call. */
struct zb_jni_guest_api {
    uint32_t size;
    uint32_t version;
    uint32_t new_env_fn;  /* uint32_t (void): a new guest JNIEnv*, or 0 */
    uint32_t free_env_fn; /* void (uint32_t env) */
    uint32_t java_vm;     /* the guest JavaVM* */
};

/* Guest layouts the host reads: JNINativeMethod and JavaVMAttachArgs. */
struct zb_jni_native_method {
    uint32_t name;
    uint32_t signature;
    uint32_t fn;
};

struct zb_jni_attach_args {
    int32_t version;
    uint32_t name;
    uint32_t group;
};
