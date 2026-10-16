#include <sys/mman.h>

#include <cstring>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "zb/guest_memory.h"
#include "zb/guest_thread.h"

namespace {

void put32(zb::GuestMemory& m, std::uint32_t addr, std::uint32_t value) {
    std::memcpy(m.base() + addr, &value, 4);
}

}  // namespace

int main() {
    zb::GuestMemory mem;
    CHECK(mem.ok());
    Dynarmic::ExclusiveMonitor monitor(1);

    CHECK(mem.map_anon(0x10000, 0x1000, PROT_READ | PROT_WRITE));
    put32(mem, 0x10000, 0xE0800001);  // add r0, r0, r1
    put32(mem, 0x10004, 0xE1A00080);  // mov r0, r0, lsl #1
    put32(mem, 0x10008, 0xEF5AFFFF);  // svc #0x5affff
    put32(mem, 0x1000C, 0xEE1D0F70);  // mrc p15, 0, r0, c13, c0, 3
    put32(mem, 0x10010, 0xEF5AFFFF);  // svc #0x5affff
    put32(mem, 0x10014, 0xE5910000);  // ldr r0, [r1]
    CHECK(mem.protect(0x10000, 0x1000, PROT_READ | PROT_EXEC));

    zb::GuestThread t(mem, &monitor, 0);
    t.set_cpsr(0x10);

    // Translated arithmetic, then a host-return svc.
    t.regs()[0] = 20;
    t.regs()[1] = 1;
    t.regs()[15] = 0x10000;
    zb::Stop s = t.run();
    CHECK(s.kind == zb::StopKind::Svc);
    CHECK(s.swi == zb::kHostReturnSwi);
    CHECK(t.regs()[0] == 42);
    CHECK(s.pc == 0x1000C);

    // Resume after a halt; TLS register read through CP15.
    t.set_tls(0x12345678);
    s = t.run();
    CHECK(s.kind == zb::StopKind::Svc);
    CHECK(t.regs()[0] == 0x12345678);

    // Reading the null page is a memory fault, not a host crash.
    t.regs()[1] = 0;
    s = t.run();
    CHECK(s.kind == zb::StopKind::MemoryFault);
    CHECK(s.fault_addr == 0);
    CHECK(!s.fault_write);

    std::puts("t1_blob_test PASS");
    return 0;
}
