#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "zb/guest_memory.h"

namespace zb {

struct AuxEntry {
    std::uint32_t type;
    std::uint32_t value;
};

// Builds a kernel-style initial process stack below stack_top (which must be mapped RW below
// it). Appends AT_RANDOM, AT_PLATFORM ("v8l"), AT_EXECFN and AT_NULL to auxv.
// Layout from sp upward: argc, argv[], NULL, envp[], NULL, auxv pairs, strings.
// Returns sp (8-byte aligned) or 0 if the data does not fit.
std::uint32_t build_initial_stack(GuestMemory& mem, std::uint32_t stack_top, const std::vector<std::string>& argv,
                                  const std::vector<std::string>& envp, std::vector<AuxEntry> auxv,
                                  const std::string& execfn);

}  // namespace zb
