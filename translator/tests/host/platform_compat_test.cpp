#include <cerrno>
#include <cstdint>
#include <cstdio>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "zb/host_platform_compat.h"
#include "zb/library_runtime.h"
#include "zb/platform_compat_hostcalls.h"
#include "zb/runtime_report.h"

namespace {

std::uint32_t call(zb::HostPlatformCompat& compat, zb::GuestThread& thread,
                   std::uint32_t index, std::uint32_t arg0 = 0x11111111u) {
    thread.regs()[0] = arg0;
    thread.regs()[1] = 0x22222222u;
    CHECK(compat.handle_host_call(index, thread));
    CHECK(thread.regs()[1] == 0);
    return thread.regs()[0];
}

}  // namespace

int main() {
    zb::runtime_report().clear();
    zb::LibraryRuntime runtime;
    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);
    zb::HostPlatformCompat compat;

    CHECK(!compat.handle_host_call(0, thread));
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_ANativeWindow_lock) ==
          static_cast<std::uint32_t>(-ENOSYS));
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_ANativeWindow_unlockAndPost) ==
          static_cast<std::uint32_t>(-ENOSYS));
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_eglCreateImageKHR) == 0);
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_eglDestroyImageKHR) == 0);
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_glEGLImageTargetTexture2DOES) == 0);
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_AndroidBitmap_getInfo) ==
          static_cast<std::uint32_t>(-1));
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_AndroidBitmap_lockPixels) ==
          static_cast<std::uint32_t>(-1));
    CHECK(call(compat, thread, zb::ZB_COMPAT_HC_AndroidBitmap_unlockPixels) ==
          static_cast<std::uint32_t>(-1));
    CHECK(!compat.handle_host_call(zb::ZB_COMPAT_HC_ALooper_prepare, thread));

    CHECK(zb::runtime_report().unimplemented_host_calls() == 8);
    CHECK(zb::runtime_report().first_unimplemented_host_call() ==
          "libandroid.so ANativeWindow_lock");

    std::puts("platform_compat_test PASS");
    return 0;
}
