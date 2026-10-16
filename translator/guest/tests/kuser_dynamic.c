/* ARM kernel user helpers at 0xffff0fxx (used by real armeabi/ARMv5 code for atomics and TLS). */
#include <stdio.h>

typedef int (*kuser_cmpxchg_t)(int oldval, int newval, int* ptr);
typedef void (*kuser_memory_barrier_t)(void);
typedef void* (*kuser_get_tls_t)(void);

#define KUSER_HELPER_VERSION (*(int*)0xffff0ffc)
#define KUSER_CMPXCHG ((kuser_cmpxchg_t)0xffff0fc0)
#define KUSER_MEMORY_BARRIER ((kuser_memory_barrier_t)0xffff0fa0)
#define KUSER_GET_TLS ((kuser_get_tls_t)0xffff0fe0)

__attribute__((naked, target("arm"))) static void* read_tpidruro(void) {
    __asm__("mrc p15, 0, r0, c13, c0, 3\n"
            "bx lr\n");
}

int main(void) {
    printf("version=%s\n", KUSER_HELPER_VERSION >= 2 ? "PASS" : "FAIL");

    int value = 5;
    int ok = KUSER_CMPXCHG(5, 7, &value) == 0 && value == 7;
    ok = ok && KUSER_CMPXCHG(5, 9, &value) != 0 && value == 7;
    printf("cmpxchg=%s\n", ok ? "PASS" : "FAIL");

    KUSER_MEMORY_BARRIER();
    printf("barrier=PASS\n");

    void* tls = KUSER_GET_TLS();
    printf("get_tls=%s\n", tls != NULL && tls == read_tpidruro() ? "PASS" : "FAIL");
    return 0;
}
