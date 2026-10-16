#pragma once

#include <cstdint>
#include <string>

#include "zb/guest_memory.h"

namespace zb {

struct LoadedElf {
    std::uint32_t bias = 0;        // added to every p_vaddr (0 for ET_EXEC)
    std::uint32_t entry = 0;       // e_entry + bias (bit 0 set means Thumb)
    std::uint32_t phdr = 0;        // guest address of the program header table
    std::uint32_t phnum = 0;
    std::uint32_t load_start = 0;  // lowest mapped address, page aligned
    std::uint32_t load_end = 0;    // one past the highest mapped address, page aligned
    bool is_dyn = false;
    std::string interp;            // PT_INTERP, empty if none
};

// Loads an ARM ELF32 ET_EXEC or ET_DYN file into guest memory. ET_EXEC is placed at its own
// addresses; ET_DYN at the highest free range ending at or below dyn_limit.
bool load_elf(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedElf& out, std::string& error);

}  // namespace zb
