/* Library-mode guest service. Usage: zbhost <target_sdk> [<preload>]
 * Sets the linker target SDK, preloads libzbcompat.so and the optional library with
 * RTLD_GLOBAL, publishes zb_service_api through READY, and then serves host requests inside
 * that host call. Carriers are guest pthreads that park in PARK until a host thread has
 * borrowed and released them. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "zb/library_protocol.h"

_Static_assert(ZB_GUEST_RTLD_NOW == RTLD_NOW, "guest RTLD_NOW");
_Static_assert(ZB_GUEST_RTLD_LAZY == RTLD_LAZY, "guest RTLD_LAZY");
_Static_assert(ZB_GUEST_RTLD_GLOBAL == RTLD_GLOBAL, "guest RTLD_GLOBAL");
_Static_assert(ZB_GUEST_RTLD_NOLOAD == RTLD_NOLOAD, "guest RTLD_NOLOAD");
_Static_assert(ZB_GUEST_RTLD_NODELETE == RTLD_NODELETE, "guest RTLD_NODELETE");

static char scratch[ZB_SERVICE_SCRATCH_SIZE];

static uint32_t host_call(uint32_t index, uint32_t arg) {
    register uint32_t r0 __asm__("r0") = arg;
    if (index == ZB_SERVICE_READY_INDEX) {
        __asm__ volatile("svc #0x5afe00" : "+r"(r0) : : "memory");
    } else {
        __asm__ volatile("svc #0x5afe01" : "+r"(r0) : : "memory");
    }
    return r0;
}

static uint32_t service_dlopen(const char* path, uint32_t flags) {
    return (uint32_t)(uintptr_t)dlopen(path, (int)flags);
}

static uint32_t service_dlsym(uint32_t handle, const char* name) {
    return (uint32_t)(uintptr_t)dlsym((void*)(uintptr_t)handle, name);
}

static uint32_t service_dlerror(void) {
    return (uint32_t)(uintptr_t)dlerror();
}

static uint32_t service_malloc(uint32_t size) {
    return (uint32_t)(uintptr_t)malloc(size);
}

static void service_free(uint32_t pointer) {
    free((void*)(uintptr_t)pointer);
}

static void* carrier_main(void* unused) {
    (void)unused;
    while (host_call(ZB_CARRIER_PARK_INDEX, 0) == ZB_SERVICE_AGAIN) {
    }
    return NULL;
}

static uint32_t spawn_carrier(void) {
    pthread_t thread;
    const int rc = pthread_create(&thread, NULL, carrier_main, NULL);
    if (rc == 0) pthread_detach(thread);
    return (uint32_t)rc;
}

static int preload(const char* path) {
    if (dlopen(path, RTLD_NOW | RTLD_GLOBAL) != NULL) return 1;
    const char* error = dlerror();
    fprintf(stderr, "zbhost: cannot preload %s: %s\n", path, error != NULL ? error : "unknown error");
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return ZB_HOST_EXIT_USAGE;
    void* dl_android = dlopen("libdl_android.so", RTLD_NOW);
    void (*set_target_sdk)(unsigned) = dl_android != NULL
        ? (void (*)(unsigned))dlsym(dl_android, "android_set_application_target_sdk_version")
        : NULL;
    if (set_target_sdk == NULL) return ZB_HOST_EXIT_TARGET_SDK;
    set_target_sdk((unsigned)strtoul(argv[1], NULL, 10));
    if (!preload("libzbcompat.so")) return ZB_HOST_EXIT_PRELOAD;
    if (argc == 3 && !preload(argv[2])) return ZB_HOST_EXIT_PRELOAD;
    const struct zb_service_api api = {
        sizeof(api), ZB_SERVICE_PROTOCOL_VERSION,
        (uint32_t)(uintptr_t)service_dlopen,
        (uint32_t)(uintptr_t)service_dlsym,
        (uint32_t)(uintptr_t)service_dlerror,
        (uint32_t)(uintptr_t)spawn_carrier,
        (uint32_t)(uintptr_t)service_malloc,
        (uint32_t)(uintptr_t)service_free,
        (uint32_t)(uintptr_t)scratch, sizeof(scratch),
    };
    uint32_t status;
    do {
        status = host_call(ZB_SERVICE_READY_INDEX, (uint32_t)(uintptr_t)&api);
    } while (status == ZB_SERVICE_AGAIN);
    return (int)status;
}
