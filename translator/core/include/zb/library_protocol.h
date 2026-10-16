#pragma once

/* Library-mode handshake between the host LibraryRuntime and guest/zbhost/zbhost.c.
 * C-compatible: the arm32 guest includes this header too. */

#include <stdint.h>

#define ZB_SERVICE_PROTOCOL_VERSION 2u

/* Host-call indices 0xFE00-0xFEFF belong to the library runtime; tools/gen_stubs.py keeps
 * generated stub indices below this range. */
#define ZB_RUNTIME_HOST_CALL_FIRST 0xFE00u
#define ZB_RUNTIME_HOST_CALL_LAST 0xFEFFu
#define ZB_SERVICE_READY_INDEX 0xFE00u
#define ZB_CARRIER_PARK_INDEX 0xFE01u

/* r0 from READY or PARK: return to guest code so pending signals are delivered, then make
 * the same host call again. */
#define ZB_SERVICE_AGAIN 0xFFFFFFF5u

#define ZB_SERVICE_SCRATCH_SIZE 4096u

/* zbhost exit statuses before READY. */
#define ZB_HOST_EXIT_USAGE 2
#define ZB_HOST_EXIT_TARGET_SDK 3
#define ZB_HOST_EXIT_PRELOAD 4

/* 32-bit bionic dlopen flags and handles. Host <dlfcn.h> values differ (host RTLD_NOW is 2,
 * RTLD_GLOBAL 0x100, RTLD_DEFAULT 0). */
#define ZB_GUEST_RTLD_NOW 0u
#define ZB_GUEST_RTLD_LAZY 1u
#define ZB_GUEST_RTLD_GLOBAL 2u
#define ZB_GUEST_RTLD_NOLOAD 4u
#define ZB_GUEST_RTLD_NODELETE 0x1000u
#define ZB_GUEST_RTLD_DEFAULT 0xffffffffu

struct zb_service_api {
    uint32_t size;
    uint32_t version;
    uint32_t dlopen_fn;        /* uint32_t (const char* path, uint32_t guest_flags) */
    uint32_t dlsym_fn;         /* uint32_t (uint32_t handle, const char* name) */
    uint32_t dlerror_fn;       /* uint32_t (void): const char* or 0, per guest thread */
    uint32_t spawn_carrier_fn; /* uint32_t (void): pthread_create result */
    uint32_t malloc_fn;        /* uint32_t (uint32_t size) */
    uint32_t free_fn;          /* void (uint32_t pointer) */
    uint32_t scratch;          /* service-thread string buffer */
    uint32_t scratch_size;
};
