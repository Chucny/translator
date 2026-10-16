#include "gl_driver_backend.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <string>
#include <type_traits>

#include "zb/runtime_report.h"

namespace zb {

namespace {

// Distinguishes "resolved to nothing" from "not resolved yet" in the entry cache without a
// second array: any non-null value that can never be a real entry point.
void* const kExtensionMissing = reinterpret_cast<void*>(std::uintptr_t{1});

// What an entry point the driver does not have returns: nothing for void, a zeroed value
// otherwise (GL_FALSE, NULL). `void*()` is not a cast expression, so this cannot be written
// inline in the macro below.
template <typename T>
T extension_default() {
    if constexpr (std::is_void_v<T>) {
        return;
    } else {
        return T{};
    }
}

}  // namespace

void GlDriverBackend::set_error(GLenum error) {
    if (pending_error_ == GL_NO_ERROR) pending_error_ = error;
}

GLenum GlDriverBackend::glGetError(void) {
    if (pending_error_ != GL_NO_ERROR) {
        const GLenum error = pending_error_;
        pending_error_ = GL_NO_ERROR;
        return error;
    }
    return ::glGetError();
}

void* GlDriverBackend::extension_entry(unsigned slot, const char* name) {
    void* cached = extension_entries_[slot].load(std::memory_order_acquire);
    if (cached == nullptr) {
        cached = reinterpret_cast<void*>(::eglGetProcAddress(name));
        if (cached == nullptr) {
            // One line per missing name, written once (overwrite=false), so a guest that
            // called an entry point this driver does not have leaves a trace even though
            // there is no logcat on the device.
            runtime_report().note_gl_detail(std::string("missing-") + name, "no driver entry",
                                            false);
            cached = kExtensionMissing;
        }
        extension_entries_[slot].store(cached, std::memory_order_release);
    }
    return cached == kExtensionMissing ? nullptr : cached;
}

// One definition per ZB_GL_EXT_ENTRY row: a missing entry point queues GL_INVALID_OPERATION and
// returns a zeroed value, exactly as a driver without the extension would.
#define ZB_GL_EXT_ENTRY(slot, name, result, declaration, types, arguments)          \
    result GlDriverBackend::name declaration {                                      \
        using Entry = result(GL_APIENTRY*) types;                                   \
        void* entry = extension_entry(slot, #name);                                 \
        if (entry == nullptr) {                                                     \
            set_error(GL_INVALID_OPERATION);                                        \
            return extension_default<result>();                                     \
        }                                                                           \
        return reinterpret_cast<Entry>(entry) arguments;                            \
    }
#include "zb/gl_ext_entries.inc"
#undef ZB_GL_EXT_ENTRY

std::uint64_t GlDriverBackend::invoke(const char*, std::initializer_list<std::uint64_t>) {
    // Every typed virtual is overridden above; the portable invoke() seam (used by MockGles in
    // host tests) is never reached in production.
    return 0;
}

std::uintptr_t gl_egl_context() {
    return reinterpret_cast<std::uintptr_t>(eglGetCurrentContext());
}

}  // namespace zb
