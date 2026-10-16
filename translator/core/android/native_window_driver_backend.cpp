#include "native_window_driver_backend.h"

#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

namespace zb {

void* AndroidNativeWindowBackend::from_surface(void* env, void* surface) {
    return ANativeWindow_fromSurface(static_cast<JNIEnv*>(env), static_cast<jobject>(surface));
}

void AndroidNativeWindowBackend::acquire(void* window) {
    if (window == nullptr) return;
    ANativeWindow_acquire(static_cast<ANativeWindow*>(window));
}

void AndroidNativeWindowBackend::release(void* window) {
    if (window == nullptr) return;
    ANativeWindow_release(static_cast<ANativeWindow*>(window));
}

std::int32_t AndroidNativeWindowBackend::query(void* window, Query which) {
    if (window == nullptr) return -1;
    auto* native = static_cast<ANativeWindow*>(window);
    switch (which) {
    case Query::Width: return ANativeWindow_getWidth(native);
    case Query::Height: return ANativeWindow_getHeight(native);
    case Query::Format: return ANativeWindow_getFormat(native);
    }
    return -1;
}

std::int32_t AndroidNativeWindowBackend::set_buffers_geometry(void* window, std::int32_t width,
                                                               std::int32_t height,
                                                               std::int32_t format) {
    if (window == nullptr) return -1;
    return ANativeWindow_setBuffersGeometry(static_cast<ANativeWindow*>(window), width, height,
                                            format);
}

}  // namespace zb
