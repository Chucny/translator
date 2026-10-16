#include <elf.h>
#include <sys/mman.h>

#include <cstring>
#include <string>

#include "check.h"
#include "zb/elf_loader.h"
#include "zb/guest_memory.h"
#include "zb/initial_stack.h"

namespace {

std::uint32_t read32(zb::GuestMemory& m, std::uint32_t addr) {
    std::uint32_t v;
    std::memcpy(&v, m.base() + addr, 4);
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    zb::GuestMemory mem;
    CHECK(mem.ok());

    zb::LoadedElf elf;
    std::string error;
    const bool loaded = zb::load_elf(mem, argv[1], 0xFE000000, elf, error);
    if (!loaded) std::fprintf(stderr, "load_elf: %s\n", error.c_str());
    CHECK(loaded);
    CHECK(!elf.is_dyn);
    CHECK(elf.interp.empty());
    CHECK(elf.entry >= elf.load_start && elf.entry < elf.load_end);
    CHECK(mem.accessible(elf.entry & ~1u, 4, zb::kPageExec));
    CHECK(elf.phnum > 0);
    CHECK(mem.accessible(elf.phdr, elf.phnum * sizeof(Elf32_Phdr), zb::kPageRead));

    // The loader must refuse to load an ET_EXEC over itself.
    zb::LoadedElf again;
    CHECK(!zb::load_elf(mem, argv[1], 0xFE000000, again, error));

    const std::uint32_t stack_top = 0xFF000000;
    CHECK(mem.map_anon(stack_top - 0x10000, 0x10000, PROT_READ | PROT_WRITE));
    const std::uint32_t sp = zb::build_initial_stack(mem, stack_top, {"prog", "a"}, {"X=1"}, {{AT_PAGESZ, 4096}}, "prog");
    CHECK(sp != 0);
    CHECK((sp & 7) == 0);
    CHECK(read32(mem, sp) == 2);
    const std::uint32_t argv1 = read32(mem, sp + 8);
    CHECK(std::strcmp(reinterpret_cast<const char*>(mem.base() + argv1), "a") == 0);
    CHECK(read32(mem, sp + 12) == 0);
    const std::uint32_t env0 = read32(mem, sp + 16);
    CHECK(std::strcmp(reinterpret_cast<const char*>(mem.base() + env0), "X=1") == 0);
    CHECK(read32(mem, sp + 20) == 0);

    bool saw_random = false;
    bool saw_pagesz = false;
    for (std::uint32_t p = sp + 24;; p += 8) {
        const std::uint32_t type = read32(mem, p);
        const std::uint32_t value = read32(mem, p + 4);
        if (type == AT_NULL) break;
        if (type == AT_PAGESZ) saw_pagesz = value == 4096;
        if (type == AT_RANDOM) saw_random = mem.accessible(value, 16, zb::kPageRead);
    }
    CHECK(saw_pagesz);
    CHECK(saw_random);

    std::puts("elf_loader_test PASS");
    return 0;
}
