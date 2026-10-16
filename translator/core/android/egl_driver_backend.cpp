#include "egl_driver_backend.h"

#include <EGL/egl.h>

namespace zb {

std::uint64_t EglDriverBackend::invoke(const char*, std::initializer_list<std::uint64_t>) {
    // Every typed virtual is overridden above; the portable invoke() seam (used by MockEgl in
    // host tests) is never reached in production.
    return 0;
}

}  // namespace zb
