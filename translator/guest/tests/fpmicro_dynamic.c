/* Floating-point micro benchmark: one operation per loop, to locate translation overhead.
 * Built with the guest tests, not run by the suite; see docs/perf-notes.md. */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
#define N 50000000u
__attribute__((noinline)) static double d_add(void) { double s = 0.0; for (uint32_t i = 0; i < N; ++i) s += 1.25; return s; }
__attribute__((noinline)) static double d_mul(void) { double s = 1.0; for (uint32_t i = 0; i < N; ++i) s = s * 1.0000001; return s; }
__attribute__((noinline)) static double d_div(void) { double s = 1e300; for (uint32_t i = 0; i < N; ++i) s = s / 1.0000001; return s; }
__attribute__((noinline)) static double d_cvt(void) { double s = 0.0; for (uint32_t i = 0; i < N; ++i) s += (double)(int32_t)i; return s; }
__attribute__((noinline)) static float f_add(void) { float s = 0.0f; for (uint32_t i = 0; i < N; ++i) s += 1.25f; return s; }
__attribute__((noinline)) static float f_mul(void) { float s = 1.0f; for (uint32_t i = 0; i < N; ++i) s = s * 1.0000001f; return s; }
__attribute__((noinline)) static float f_div(void) { float s = 1e30f; for (uint32_t i = 0; i < N; ++i) s = s / 1.0000001f; return s; }
__attribute__((noinline)) static int32_t f_toint(void) { int32_t s = 0; float v = 1.5f; for (uint32_t i = 0; i < N; ++i) s += (int32_t)(v * 3.0f); return s; }
__attribute__((noinline)) static uint32_t i_ref(void) { uint32_t s = 0; for (uint32_t i = 0; i < N; ++i) s += i * 3u; return s; }
int main(void) {
    struct { const char* name; double (*d)(void); float (*f)(void); int32_t (*i)(void); uint32_t (*u)(void); } t[] = {
        {"int ref", 0, 0, 0, i_ref}, {"d add", d_add}, {"d mul", d_mul}, {"d div", d_div}, {"d cvt", d_cvt},
        {"f add", 0, f_add}, {"f mul", 0, f_mul}, {"f div", 0, f_div}, {"f toint", 0, 0, f_toint}};
    for (unsigned k = 0; k < sizeof t / sizeof t[0]; ++k) {
        double t0 = now();
        if (t[k].d) t[k].d(); else if (t[k].f) t[k].f(); else if (t[k].i) t[k].i(); else t[k].u();
        printf("%-8s %.3fs\n", t[k].name, now() - t0);
    }
    return 0;
}
