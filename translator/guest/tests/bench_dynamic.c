/* Rough CPU benchmark. The same source is built as arm32 (run under zbrun) and as native
 * aarch64, to put a number on translation overhead. Not part of the pass/fail suite. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint32_t integer_mix(uint32_t rounds) {
    uint32_t x = 2463534242u;
    uint32_t acc = 0;
    for (uint32_t i = 0; i < rounds; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        acc += (x % 1000u) * (i & 7u);
    }
    return acc;
}

static uint32_t sieve(uint32_t limit) {
    unsigned char* composite = calloc(limit + 1, 1);
    uint32_t count = 0;
    for (uint32_t i = 2; i <= limit; ++i) {
        if (composite[i]) continue;
        ++count;
        for (uint64_t j = (uint64_t)i * i; j <= limit; j += i) composite[j] = 1;
    }
    free(composite);
    return count;
}

static double floating(uint32_t steps) {
    double sum = 0.0;
    for (uint32_t i = 1; i <= steps; ++i) sum += 1.0 / ((double)i * (double)i);
    return sum;
}

static uint32_t copy_bytes(uint32_t megabytes) {
    const size_t size = 1 << 20;
    char* a = malloc(size);
    char* b = malloc(size);
    memset(a, 0x5a, size);
    uint32_t check = 0;
    for (uint32_t i = 0; i < megabytes; ++i) {
        memcpy(b, a, size);
        check += (unsigned char)b[i % size];
    }
    free(a);
    free(b);
    return check;
}

int main(void) {
    double t0 = now_seconds();
    uint32_t mix = integer_mix(200000000u);
    double t1 = now_seconds();
    uint32_t primes = sieve(20000000u);
    double t2 = now_seconds();
    double pi2 = floating(100000000u);
    double t3 = now_seconds();
    uint32_t copied = copy_bytes(2000u);
    double t4 = now_seconds();
    printf("integer_mix %.3fs (%u)\n", t1 - t0, mix);
    printf("sieve       %.3fs (%u primes)\n", t2 - t1, primes);
    printf("floating    %.3fs (%.9f)\n", t3 - t2, pi2);
    printf("memcpy      %.3fs (%u)\n", t4 - t3, copied);
    printf("total       %.3fs\n", t4 - t0);
    return 0;
}
