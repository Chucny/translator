#pragma once

#include <EGL/egl.h>

#include "zb/egl_backend.h"

namespace zb {

// Phase 7a Task 8: a thin passthrough EglBackend that calls the real device driver
// (libEGL.so). All marshaling (handle translation, bounds checks) already happened in HostEgl
// before a call reaches here, so every override below is a direct forward with no logic of its
// own. This and egl_driver_backend.cpp are the only files in the tree that include EGL/egl.h
// for real, aside from the ANativeWindow half in native_window_driver_backend.cpp.
class EglDriverBackend final : public EglBackend {
public:
    // Generated from core/include/zb/egl_backend.h by a one-off script (see
    // docs/superpowers/plans/2026-09-16-phase5-gles.md, Task 8, reused for EGL): every
    // EglBackend virtual, forwarding straight to the ::egl* driver entry point of the same name.
#include "egl_driver_backend_overrides.inc"

protected:
    // Never called: every method above is overridden directly, matching the design ("The real
    // EglBackend calls the driver directly").
    std::uint64_t invoke(const char* name, std::initializer_list<std::uint64_t> arguments) override;
};

}  // namespace zb
