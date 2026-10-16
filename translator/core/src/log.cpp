#include "zb/log.h"

#include <cstdarg>
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace zb {

void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
#if defined(__ANDROID__)
    va_list logcat_ap;
    va_copy(logcat_ap, ap);
    __android_log_vprint(ANDROID_LOG_INFO, "zbridge", fmt, logcat_ap);
    va_end(logcat_ap);
#endif
    std::fputs("[zb] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

}  // namespace zb
