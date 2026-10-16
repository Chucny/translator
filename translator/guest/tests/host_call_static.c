#include <stdint.h>

static uint32_t call_test_host_call(uint32_t value) {
    register uint32_t r0 __asm__("r0") = value;
    __asm__ volatile("svc #0x5afe10" : "+r"(r0) : : "memory");
    return r0;
}

/* Host-call index 25 is libGLESv2.so glCreateProgram (core/src/gen/hostcalls.inc). No test
   implements it, so Process takes its unimplemented path: it records the call in the runtime
   report, writes r0 = 0 and lets the guest run on. */
static uint32_t call_unimplemented_host_call(uint32_t value) {
    register uint32_t r0 __asm__("r0") = value;
    __asm__ volatile("svc #0x5a0019" : "+r"(r0) : : "memory");
    return r0;
}

int main(void) {
    if (call_test_host_call(7) != 49) return 1;
    if (call_unimplemented_host_call(0x1234) != 0) return 2;
    return 0;
}
