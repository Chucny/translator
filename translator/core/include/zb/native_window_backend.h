#pragma once

#include <cstdint>

namespace zb {

// Portable seam behind ANativeWindow_* (Phase 7a Task 5). HostNativeWindow marshals guest
// arguments and 32-bit handles; this interface deals only in opaque host ANativeWindow*
// pointers (nullptr is invalid/none). Implementations: the real Android NDK native window
// (core/android/native_window_driver_backend.*, Task 8) and an in-memory mock for host tests
// (tests/host/mock_native_window.h).
class NativeWindowBackend {
public:
    virtual ~NativeWindowBackend() = default;

    enum class Query { Width, Height, Format };

    // ANativeWindow_fromSurface. env/surface are host values (HostJni has already resolved the
    // guest JNIEnv/jobject handles on the calling thread). Returns an opaque host ANativeWindow*,
    // or nullptr on failure.
    virtual void* from_surface(void* env, void* surface) = 0;

    // ANativeWindow_acquire. A no-op for a null window.
    virtual void acquire(void* window) = 0;

    // ANativeWindow_release. A no-op for a null window.
    virtual void release(void* window) = 0;

    // ANativeWindow_getWidth/getHeight/getFormat. -1 on failure.
    virtual std::int32_t query(void* window, Query which) = 0;

    // ANativeWindow_setBuffersGeometry. Returns the NDK status (0 on success, negative on error).
    virtual std::int32_t set_buffers_geometry(void* window, std::int32_t width, std::int32_t height,
                                              std::int32_t format) = 0;
};

}  // namespace zb
