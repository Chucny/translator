#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zb {

// Dynamic tag written by tools/fix_guest_lib.py in place of DT_TEXTREL. The guest linker
// ignores it (it is in the OS-specific range); zbrun keeps the library's code pages writable
// so the linker's text relocations succeed.
inline constexpr std::int32_t kDtZbTextrel = 0x60005A42;

enum class ElfFixupStatus {
    Unchanged,
    Changed,
    Skipped,
    Error,
};

struct ElfFixupReport {
    ElfFixupStatus status = ElfFixupStatus::Error;
    std::vector<std::string> changes;
    std::string message;
};

// Applies the import-time fixups required by old little-endian ARM ELF32 shared
// libraries. The file is not written until the complete ELF structure has been
// validated and every requested fixup is known to fit.
ElfFixupReport fix_guest_library(const std::string& path);

// True if the ELF32 file behind fd has a DT_ZB_TEXTREL entry in its dynamic section.
bool elf_has_textrel_marker(int fd);

}  // namespace zb
