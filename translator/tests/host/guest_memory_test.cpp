#include <sys/mman.h>

#include "check.h"
#include "zb/guest_memory.h"

int main() {
    zb::GuestMemory m;
    CHECK(m.ok());
    CHECK(!m.accessible(0, 1, zb::kPageRead));

    CHECK(m.map_anon(0x10000, 5000, PROT_READ | PROT_WRITE));
    CHECK(m.accessible(0x10000, 8192, zb::kPageRead | zb::kPageWrite));
    CHECK(!m.accessible(0x10000, 8193, zb::kPageRead));
    m.base()[0x11fff] = 7;
    CHECK(m.host_ptr(0x11fff, 1, zb::kPageRead)[0] == 7);

    CHECK(m.protect(0x10000, 4096, PROT_READ | PROT_EXEC));
    CHECK(m.host_ptr(0x10000, 1, zb::kPageWrite) == nullptr);
    CHECK(m.accessible(0x10000, 4, zb::kPageExec));
    CHECK(!m.protect(0x20000, 4096, PROT_READ));
    CHECK(!m.map_anon(0x10001, 10, PROT_READ));
    CHECK(!m.accessible(0xFFFFFFF0, 32, 0));

    std::uint32_t f = m.find_free(3 * 4096, 0x40000000);
    CHECK(f != 0 && (f & 0xFFF) == 0 && f + 3 * 4096 <= 0x40000000);
    CHECK(m.range_free(f, 3 * 4096));

    CHECK(m.unmap(0x10000, 8192));
    CHECK(!m.accessible(0x10000, 1, 0));
    CHECK(m.find_free(4096, 0x11000) == 0x10000);
    CHECK(m.find_free(4096, 0x10000) == 0);

    std::puts("guest_memory_test PASS");
    return 0;
}
