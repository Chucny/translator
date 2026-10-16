#pragma once

#include "zb/native_window_backend.h"

namespace zb {

// Phase 7a Task 8: NativeWindowBackend over the real NDK ANativeWindow_* API
// (<android/native_window.h>, <android/native_window_jni.h>). Hand-written, unlike
// EglDriverBackend/GlDriverBackend: there are only five entry points and no shared shape with
// the generated backends to script.
class AndroidNativeWindowBackend final : public NativeWindowBackend {
public:
    void* from_surface(void* env, void* surface) override;
    void acquire(void* window) override;
    void release(void* window) override;
    std::int32_t query(void* window, Query which) override;
    std::int32_t set_buffers_geometry(void* window, std::int32_t width, std::int32_t height,
                                      std::int32_t format) override;
};

}  // namespace zb
