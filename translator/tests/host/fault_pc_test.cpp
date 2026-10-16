// Memory faults are precise: the Stop reports the PC of the faulting instruction, and nothing
// after it in the same translated block has run. Guest SIGSEGV handlers that fix the cause
// and return depend on this.
#include <sys/mman.h>

#include <cstdio>
#include <cstring>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "zb/guest_memory.h"
#include "zb/guest_thread.h"

namespace {

void put32(zb::GuestMemory& m, std::uint32_t addr, std::uint32_t value) {
    std::memcpy(m.base() + addr, &value, 4);
}

void put16(zb::GuestMemory& m, std::uint32_t addr, std::uint16_t value) {
    std::memcpy(m.base() + addr, &value, 2);
}

}  // namespace

int main() {
    zb::GuestMemory mem;
    CHECK(mem.ok());
    Dynarmic::ExclusiveMonitor monitor(1);
    CHECK(mem.map_anon(0x10000, 0x1000, PROT_READ | PROT_WRITE));

    // ARM: a few instructions in the same block before and after the faulting load.
    put32(mem, 0x10000, 0xE3A01000);  // mov r1, #0
    put32(mem, 0x10004, 0xE3A02005);  // mov r2, #5
    put32(mem, 0x10008, 0xE5910000);  // ldr r0, [r1]      <- faults
    put32(mem, 0x1000C, 0xE3A02007);  // mov r2, #7
    put32(mem, 0x10010, 0xEF5AFFFF);  // svc #0x5affff

    // Thumb: movs r1, #0; movs r2, #5; ldr r0, [r1]; movs r2, #7; svc #0xff
    put16(mem, 0x10100, 0x2100);
    put16(mem, 0x10102, 0x2205);
    put16(mem, 0x10104, 0x6808);  // <- faults
    put16(mem, 0x10106, 0x2207);
    put16(mem, 0x10108, 0xDFFF);
    CHECK(mem.protect(0x10000, 0x1000, PROT_READ | PROT_EXEC));

    zb::GuestThread arm(mem, &monitor, 0, /*precise_faults=*/true);
    arm.set_cpsr(0x10);
    arm.regs()[15] = 0x10000;
    zb::Stop s = arm.run();
    std::printf("arm:   kind=%d fault_addr=0x%x stop.pc=0x%x r2=%u\n", static_cast<int>(s.kind), s.fault_addr, s.pc,
                arm.regs()[2]);
    CHECK(s.kind == zb::StopKind::MemoryFault);
    CHECK(s.fault_addr == 0);
    CHECK(s.pc == 0x10008);
    CHECK(arm.regs()[2] == 5);

    zb::GuestThread thumb(mem, &monitor, 0, /*precise_faults=*/true);
    thumb.set_cpsr(0x30);
    thumb.regs()[15] = 0x10100;
    s = thumb.run();
    std::printf("thumb: kind=%d fault_addr=0x%x stop.pc=0x%x r2=%u\n", static_cast<int>(s.kind), s.fault_addr, s.pc,
                thumb.regs()[2]);
    CHECK(s.kind == zb::StopKind::MemoryFault);
    CHECK(s.pc == 0x10104);
    CHECK(thumb.regs()[2] == 5);

    std::puts("fault_pc_test PASS");
    return 0;
}
