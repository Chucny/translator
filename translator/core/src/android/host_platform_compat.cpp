#include "zb/host_platform_compat.h"

#include <cerrno>

#include "zb/platform_compat_hostcalls.h"
#include "zb/runtime_report.h"

namespace zb {

namespace {

struct Fallback {
    std::uint32_t index;
    const char* library;
    const char* function;
    std::int32_t result;
};

constexpr Fallback kFallbacks[] = {
    {ZB_COMPAT_HC_ANativeWindow_lock, "libandroid.so", "ANativeWindow_lock", -ENOSYS},
    {ZB_COMPAT_HC_ANativeWindow_unlockAndPost, "libandroid.so",
     "ANativeWindow_unlockAndPost", -ENOSYS},
    {ZB_COMPAT_HC_eglCreateImageKHR, "libEGL.so", "eglCreateImageKHR", 0},
    {ZB_COMPAT_HC_eglDestroyImageKHR, "libEGL.so", "eglDestroyImageKHR", 0},
    {ZB_COMPAT_HC_glEGLImageTargetTexture2DOES, "libGLESv2.so",
     "glEGLImageTargetTexture2DOES", 0},
    {ZB_COMPAT_HC_AndroidBitmap_getInfo, "libjnigraphics.so", "AndroidBitmap_getInfo", -1},
    {ZB_COMPAT_HC_AndroidBitmap_lockPixels, "libjnigraphics.so", "AndroidBitmap_lockPixels", -1},
    {ZB_COMPAT_HC_AndroidBitmap_unlockPixels, "libjnigraphics.so", "AndroidBitmap_unlockPixels", -1},
};

}  // namespace

bool HostPlatformCompat::handle_host_call(std::uint32_t index, GuestThread& thread) {
    for (const Fallback& fallback : kFallbacks) {
        if (fallback.index != index) continue;
        runtime_report().note_unimplemented_host_call(index, fallback.library, fallback.function);
        thread.regs()[0] = static_cast<std::uint32_t>(fallback.result);
        thread.regs()[1] = 0;
        return true;
    }
    return false;
}

}  // namespace zb
