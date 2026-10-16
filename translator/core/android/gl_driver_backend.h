#pragma once

#include <GLES3/gl3.h>

#include <array>
#include <atomic>
#include <cstdint>

#include "zb/gl_backend.h"
#include "zb/gl_hostcalls.h"

namespace zb {

// Phase 5 Task 8: a thin passthrough GlBackend that calls the real device driver
// (libGLESv2.so). All marshaling (pointer translation, bounds checks, client-array
// materialization) already happened in HostGl before a call reaches here, so every override
// below is a direct forward with no logic of its own. This and gl_driver_backend.cpp are the
// only files in the tree that include GLES3/gl3.h (which includes GLES2/gl2.h) for real.
class GlDriverBackend final : public GlBackend {
public:
    // There is no real API to inject an error into the driver's own error queue, so a rejection
    // from HostGl's bounds checks (a bad guest pointer, never reaching the driver) is queued here
    // instead and returned by the next glGetError(), ahead of whatever the driver itself queued.
    // GL error state is sticky until read: only the first rejection between two glGetError()
    // calls is kept.
    void set_error(GLenum error) override;
    GLenum glGetError(void) override;

    // Generated from core/include/zb/gl_backend.h by a one-off script (see
    // docs/superpowers/plans/2026-09-16-phase5-gles.md, Task 8): every other GlBackend virtual,
    // forwarding straight to the ::gl* driver entry point of the same name.
#include "gl_driver_backend_overrides.inc"

    // The extension entry points (tools/gen_stubs.py's GLES_EXTENSIONS). None of them is a
    // linkable NDK symbol, so each is resolved lazily through the real eglGetProcAddress on
    // first use and cached in extension_entries_. A driver that does not have one answers the
    // way it would itself: GL_INVALID_OPERATION and no call through a null pointer.
#define ZB_GL_EXT_ENTRY(slot, name, result, declaration, types, arguments) \
    result name declaration override;
#include "zb/gl_ext_entries.inc"
#undef ZB_GL_EXT_ENTRY

protected:
    // Never called: every method above is overridden directly, matching the design ("The real
    // GlBackend calls the driver directly").
    std::uint64_t invoke(const char* name, std::initializer_list<std::uint64_t> arguments) override;

private:
    // The cached eglGetProcAddress result for extension slot `slot`, or nullptr when the driver
    // does not have it. Safe from any thread; resolving twice in a race is harmless.
    void* extension_entry(unsigned slot, const char* name);

    GLenum pending_error_ = 0;  // GL_NO_ERROR
    std::array<std::atomic<void*>, kGlHostCallExtCount> extension_entries_{};
};

// The current EGLContext as an opaque integer. HostGl uses zero/nonzero for its top-level check
// and the identity for bounded mapped-buffer diagnostics.
std::uintptr_t gl_egl_context();

}  // namespace zb
