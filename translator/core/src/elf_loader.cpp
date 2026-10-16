#include "zb/elf_loader.h"

#include <elf.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace zb {

bool load_elf(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedElf& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Elf32_Ehdr eh;
    if (data.size() < sizeof eh) {
        error = "file too small: " + path;
        return false;
    }
    std::memcpy(&eh, data.data(), sizeof eh);
    if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS32 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB || eh.e_machine != EM_ARM) {
        error = "not a little-endian ARM ELF32: " + path;
        return false;
    }
    if (eh.e_type != ET_EXEC && eh.e_type != ET_DYN) {
        error = "unsupported ELF type in " + path;
        return false;
    }
    if (eh.e_phentsize != sizeof(Elf32_Phdr) ||
        static_cast<std::uint64_t>(eh.e_phoff) + static_cast<std::uint64_t>(eh.e_phnum) * sizeof(Elf32_Phdr) > data.size()) {
        error = "bad program header table in " + path;
        return false;
    }
    std::vector<Elf32_Phdr> phdrs(eh.e_phnum);
    std::memcpy(phdrs.data(), data.data() + eh.e_phoff, phdrs.size() * sizeof(Elf32_Phdr));

    std::uint64_t lo = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t hi = 0;
    for (const auto& p : phdrs) {
        if (p.p_type != PT_LOAD) continue;
        if (p.p_filesz > p.p_memsz || static_cast<std::uint64_t>(p.p_offset) + p.p_filesz > data.size()) {
            error = "bad PT_LOAD in " + path;
            return false;
        }
        lo = std::min<std::uint64_t>(lo, page_round_down(p.p_vaddr));
        hi = std::max<std::uint64_t>(hi, page_round_up(static_cast<std::uint64_t>(p.p_vaddr) + p.p_memsz));
    }
    if (hi == 0 || hi > kGuestSpaceSize) {
        error = "no loadable segments in " + path;
        return false;
    }
    const std::uint64_t span = hi - lo;

    std::uint32_t bias = 0;
    if (eh.e_type == ET_DYN) {
        const std::uint32_t at = mem.find_free(span, dyn_limit);
        if (at == 0) {
            error = "no guest address space for " + path;
            return false;
        }
        bias = at - static_cast<std::uint32_t>(lo);
    } else if (!mem.range_free(static_cast<std::uint32_t>(lo), span)) {
        error = "address range of " + path + " is already in use";
        return false;
    }

    const std::uint32_t start = static_cast<std::uint32_t>(lo) + bias;
    if (!mem.map_anon(start, span, PROT_READ | PROT_WRITE)) {
        error = "cannot map " + path;
        return false;
    }

    out = LoadedElf{};
    for (const auto& p : phdrs) {
        if (p.p_type == PT_LOAD) {
            std::memcpy(mem.base() + p.p_vaddr + bias, data.data() + p.p_offset, p.p_filesz);
        } else if (p.p_type == PT_INTERP) {
            if (static_cast<std::uint64_t>(p.p_offset) + p.p_filesz > data.size()) {
                error = "bad PT_INTERP in " + path;
                return false;
            }
            const char* s = data.data() + p.p_offset;
            out.interp.assign(s, strnlen(s, p.p_filesz));
        }
    }
    for (const auto& p : phdrs) {
        if (p.p_type != PT_LOAD) continue;
        const int prot = ((p.p_flags & PF_R) ? PROT_READ : 0) | ((p.p_flags & PF_W) ? PROT_WRITE : 0) |
                         ((p.p_flags & PF_X) ? PROT_EXEC : 0);
        const std::uint32_t seg_start = page_round_down(p.p_vaddr + bias);
        const std::uint64_t seg_end = page_round_up(static_cast<std::uint64_t>(p.p_vaddr) + bias + p.p_memsz);
        mem.protect(seg_start, seg_end - seg_start, prot);
    }

    for (const auto& p : phdrs) {
        if (p.p_type == PT_PHDR) out.phdr = p.p_vaddr + bias;
    }
    if (out.phdr == 0) {
        for (const auto& p : phdrs) {
            if (p.p_type == PT_LOAD && eh.e_phoff >= p.p_offset && eh.e_phoff < p.p_offset + p.p_filesz) {
                out.phdr = p.p_vaddr + (eh.e_phoff - p.p_offset) + bias;
                break;
            }
        }
    }

    out.bias = bias;
    out.entry = eh.e_entry + bias;
    out.phnum = eh.e_phnum;
    out.load_start = start;
    out.load_end = static_cast<std::uint32_t>(hi + bias);
    out.is_dyn = eh.e_type == ET_DYN;
    return true;
}

}  // namespace zb
