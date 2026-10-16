#pragma once

#include <cstdint>

#include "zb/guest_thread.h"

namespace zb {

// Safe loader-compatibility fallbacks for platform entry points whose full pointer bridges are
// deferred. Every handled call is reported as unimplemented; pointer-returning or output-writing
// APIs return an explicit failure instead of letting the generic r0=0 path claim success.
class HostPlatformCompat {
public:
    bool handle_host_call(std::uint32_t index, GuestThread& thread);
};

}  // namespace zb
