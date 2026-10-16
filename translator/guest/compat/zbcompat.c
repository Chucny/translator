/* Symbols that old NDK-era guest libraries import from libc.so but modern bionic no
 * longer exports. Loaded with RTLD_GLOBAL before the guest libraries. */
#include <limits.h>

/* double -> int64, truncating (ARM EABI run-time helper). Written with 32-bit conversions
 * only: a plain (long long) cast would itself compile to a call to __aeabi_d2lz. */
long long __aeabi_d2lz(double value) {
    if (value != value) return 0;
    if (value >= 9223372036854775807.0) return LLONG_MAX;
    if (value <= -9223372036854775808.0) return LLONG_MIN;
    int negative = value < 0.0;
    double magnitude = negative ? -value : value;
    unsigned int high = (unsigned int)(magnitude / 4294967296.0);
    unsigned int low = (unsigned int)(magnitude - (double)high * 4294967296.0);
    unsigned long long result = ((unsigned long long)high << 32) | low;
    return negative ? -(long long)result : (long long)result;
}
