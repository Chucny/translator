// A32/T32 Advanced SIMD "add/subtract returning high half" instructions:
// VADDHN, VRADDHN, VSUBHN, VRSUBHN. Each takes two 128-bit sources of 2N-bit
// elements and writes a 64-bit result of N-bit elements:
//
//   VADDHN  Dd[e] = (Qn[e] + Qm[e])<2N-1:N>
//   VSUBHN  Dd[e] = (Qn[e] - Qm[e])<2N-1:N>
//   VRADDHN / VRSUBHN add (1 << (N-1)) to the 2N-bit result before extracting.
//
// libflutter.so premultiplies PNG alpha with vraddhn.i16, so a translator that
// leaves these undecoded dies with SIGILL there. The lanes below cover the
// rounding boundary (exactly 1 << (N-1) in the low half), the carry out of the
// 2N-bit sum, and a borrow.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RUN_NARROW(mnemonic, dst, src_n, src_m)                 \
    __asm__ volatile(".fpu neon\n\t"                            \
                     "vld1.8 {d0, d1}, [%1]\n\t"                \
                     "vld1.8 {d2, d3}, [%2]\n\t"                \
                     mnemonic " d4, q0, q1\n\t"                 \
                     "vst1.8 {d4}, [%0]\n\t"                    \
                     :                                          \
                     : "r"(dst), "r"(src_n), "r"(src_m)         \
                     : "memory", "d0", "d1", "d2", "d3", "d4")

static int failures;

static void check8(const char *name, const uint8_t *got, const uint8_t *want) {
    printf("%s", name);
    for (int i = 0; i < 8; ++i) {
        printf(" %02x", got[i]);
    }
    printf("\n");
    if (memcmp(got, want, 8) != 0) {
        ++failures;
    }
}

static void check16(const char *name, const uint8_t *raw, const uint16_t *want) {
    uint16_t got[4];
    memcpy(got, raw, sizeof(got));
    printf("%s", name);
    for (int i = 0; i < 4; ++i) {
        printf(" %04x", got[i]);
    }
    printf("\n");
    if (memcmp(got, want, sizeof(got)) != 0) {
        ++failures;
    }
}

static void check32(const char *name, const uint8_t *raw, const uint32_t *want) {
    uint32_t got[2];
    memcpy(got, raw, sizeof(got));
    printf("%s", name);
    for (int i = 0; i < 2; ++i) {
        printf(" %08x", got[i]);
    }
    printf("\n");
    if (memcmp(got, want, sizeof(got)) != 0) {
        ++failures;
    }
}

// Operand elements 16 bits wide, result elements 8 bits wide (N = 8).
static const uint16_t a16[8] __attribute__((aligned(8))) = {
    0x0000, 0x0080, 0x007f, 0x1234, 0x12c0, 0xff80, 0x8000, 0xabcd};
static const uint16_t b16[8] __attribute__((aligned(8))) = {
    0x0000, 0x0000, 0x0000, 0x1111, 0x0000, 0x0000, 0x8001, 0x1111};
static const uint8_t add16[8] = {0x00, 0x00, 0x00, 0x23, 0x12, 0xff, 0x00, 0xbc};
static const uint8_t radd16[8] = {0x00, 0x01, 0x00, 0x23, 0x13, 0x00, 0x00, 0xbd};
static const uint8_t sub16[8] = {0x00, 0x00, 0x00, 0x01, 0x12, 0xff, 0xff, 0x9a};
static const uint8_t rsub16[8] = {0x00, 0x01, 0x00, 0x01, 0x13, 0x00, 0x00, 0x9b};

// Operand elements 32 bits wide, result elements 16 bits wide (N = 16).
static const uint32_t a32[4] __attribute__((aligned(8))) = {
    0x00008000u, 0x00007fffu, 0x12345678u, 0xffff8000u};
static const uint32_t b32[4] __attribute__((aligned(8))) = {
    0x00000000u, 0x00010000u, 0x11110000u, 0x00000000u};
static const uint16_t add32[4] = {0x0000, 0x0001, 0x2345, 0xffff};
static const uint16_t radd32[4] = {0x0001, 0x0001, 0x2345, 0x0000};
static const uint16_t sub32[4] = {0x0000, 0xffff, 0x0123, 0xffff};
static const uint16_t rsub32[4] = {0x0001, 0xffff, 0x0123, 0x0000};

// Operand elements 64 bits wide, result elements 32 bits wide (N = 32).
static const uint64_t a64[2] __attribute__((aligned(8))) = {
    0x0000000080000000ull, 0xffffffff80000000ull};
static const uint64_t b64[2] __attribute__((aligned(8))) = {
    0x0000000000000000ull, 0x0000000100000000ull};
static const uint32_t add64[2] = {0x00000000u, 0x00000000u};
static const uint32_t radd64[2] = {0x00000001u, 0x00000001u};
static const uint32_t sub64[2] = {0x00000000u, 0xfffffffeu};
static const uint32_t rsub64[2] = {0x00000001u, 0xffffffffu};

int main(void) {
    uint8_t out[8] __attribute__((aligned(8)));

    RUN_NARROW("vaddhn.i16", out, a16, b16);
    check8("vaddhn.i16", out, add16);
    RUN_NARROW("vraddhn.i16", out, a16, b16);
    check8("vraddhn.i16", out, radd16);
    RUN_NARROW("vsubhn.i16", out, a16, b16);
    check8("vsubhn.i16", out, sub16);
    RUN_NARROW("vrsubhn.i16", out, a16, b16);
    check8("vrsubhn.i16", out, rsub16);

    RUN_NARROW("vaddhn.i32", out, a32, b32);
    check16("vaddhn.i32", out, add32);
    RUN_NARROW("vraddhn.i32", out, a32, b32);
    check16("vraddhn.i32", out, radd32);
    RUN_NARROW("vsubhn.i32", out, a32, b32);
    check16("vsubhn.i32", out, sub32);
    RUN_NARROW("vrsubhn.i32", out, a32, b32);
    check16("vrsubhn.i32", out, rsub32);

    RUN_NARROW("vaddhn.i64", out, a64, b64);
    check32("vaddhn.i64", out, add64);
    RUN_NARROW("vraddhn.i64", out, a64, b64);
    check32("vraddhn.i64", out, radd64);
    RUN_NARROW("vsubhn.i64", out, a64, b64);
    check32("vsubhn.i64", out, sub64);
    RUN_NARROW("vrsubhn.i64", out, a64, b64);
    check32("vrsubhn.i64", out, rsub64);

    if (failures != 0) {
        printf("asimd narrow FAIL %d\n", failures);
        return 1;
    }
    printf("asimd narrow ok\n");
    return 0;
}
