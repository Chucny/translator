// Nested host-to-guest calls: CPU state, Thumb and IT entry, nested calls from stop handlers,
// the return-sp check, and (through a real Process running host_call_static) signals, exit
// and stray returns inside calls.
// Usage: guest_call_test <build/guest/host_call_static>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <functional>
#include <string>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "zb/guest_memory.h"
#include "zb/guest_thread.h"
#include "zb/native_call.h"
#include "zb/process.h"

namespace {

constexpr std::uint32_t kCpsrUser = 0x10;
constexpr std::uint32_t kCpsrThumb = 0x20;
constexpr std::uint32_t kCpsrEndian = 0x200;
// ITSTATE = 0x08: one instruction under condition EQ, which fails with Z clear.
constexpr std::uint32_t kCpsrItEqOne = 0x800;

void put32(zb::GuestMemory& mem, std::uint32_t addr, std::uint32_t value) {
    std::memcpy(mem.base() + addr, &value, sizeof value);
}

void put16(zb::GuestMemory& mem, std::uint32_t addr, std::uint16_t value) {
    std::memcpy(mem.base() + addr, &value, sizeof value);
}

std::uint32_t get32(zb::GuestMemory& mem, std::uint32_t addr) {
    std::uint32_t value;
    std::memcpy(&value, mem.base() + addr, sizeof value);
    return value;
}

void thread_level_tests() {
    zb::GuestMemory mem;
    CHECK(mem.ok());
    CHECK(mem.map_anon(0x10000, 0x1000, PROT_READ | PROT_WRITE));
    CHECK(mem.map_anon(0x20000, 0x2000, PROT_READ | PROT_WRITE));
    CHECK(mem.map_anon(0xFFFF0000, 0x1000, PROT_READ | PROT_WRITE));

    put32(mem, 0x10000, 0xE0800001);  // add r0, r0, r1
    put32(mem, 0x10004, 0xE59D2000);  // ldr r2, [sp]
    put32(mem, 0x10008, 0xE0800002);  // add r0, r0, r2
    put32(mem, 0x1000C, 0xEF5A0010);  // svc #0x5a0010
    put32(mem, 0x10010, 0xE12FFF1E);  // bx lr
    put32(mem, 0x10020, 0xE0800001);  // add r0, r0, r1
    put32(mem, 0x10024, 0xE12FFF1E);  // bx lr
    put16(mem, 0x10040, 0x9A00);      // thumb: ldr r2, [sp]
    put16(mem, 0x10042, 0x1880);      // thumb: adds r0, r0, r2
    put16(mem, 0x10044, 0x4770);      // thumb: bx lr
    put32(mem, 0x10060, 0xE28DD008);  // add sp, sp, #8
    put32(mem, 0x10064, 0xE12FFF1E);  // bx lr
    put32(mem, zb::kHostReturnAddress, 0xEF5AFFFF);  // svc #0x5affff
    CHECK(mem.protect(0x10000, 0x1000, PROT_READ | PROT_EXEC));
    CHECK(mem.protect(0xFFFF0000, 0x1000, PROT_READ | PROT_EXEC));

    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(mem, &monitor, 0, false, zb::kCarrierCodeCacheSize);
    thread.regs().fill(0xA5A5A5A5);
    thread.ext_regs().fill(0x5A5A5A5A);
    thread.regs()[13] = 0x22000;
    thread.regs()[15] = 0x12345678;
    thread.set_cpsr(kCpsrUser);
    thread.set_fpscr(0x01000000);
    const auto saved_regs = thread.regs();
    const auto saved_ext = thread.ext_regs();
    CHECK(thread.call_depth == 0);

    // A stop handler mutates the call registers and makes a nested call of its own.
    zb::GuestCall call;
    call.regs = {10, 20, 0, 0};
    call.stack = {12};
    int host_calls = 0;
    const auto result = thread.call(0x10000, call, [&](const zb::Stop& stop) {
        CHECK(stop.kind == zb::StopKind::Svc && stop.swi == 0x5A0010);
        CHECK(thread.call_depth == 1);
        CHECK(thread.regs()[0] == 42 && thread.regs()[1] == 20);
        ++host_calls;
        zb::GuestCall inner;
        inner.regs = {5, 6, 0, 0};
        const auto nested = thread.call(0x10020, inner, [](const zb::Stop&) { return false; });
        CHECK(nested && nested->r0 == 11);
        CHECK(thread.call_depth == 1);
        CHECK(thread.regs()[0] == 42 && thread.regs()[1] == 20);
        thread.regs()[0] += 100 + nested->r0;
        return true;
    });
    CHECK(result && result->r0 == 153 && result->r1 == 20);
    CHECK(host_calls == 1);
    CHECK(thread.call_depth == 0);
    CHECK(thread.regs() == saved_regs);
    CHECK(thread.ext_regs() == saved_ext);
    CHECK(thread.cpsr() == kCpsrUser && thread.fpscr() == 0x01000000);

    // Thumb target from an ARM caller.
    zb::GuestCall thumb;
    thumb.regs = {3, 0, 0, 0};
    thumb.stack = {4};
    const auto thumb_result = thread.call(0x10041, thumb, [](const zb::Stop&) { return false; });
    CHECK(thumb_result && thumb_result->r0 == 7);
    CHECK(thread.cpsr() == kCpsrUser);

    // Stopped inside a Thumb IT block with the E bit set: the call must start with a clean
    // ITSTATE and little-endian data, then restore the stopped CPSR exactly.
    const std::uint32_t it_cpsr = kCpsrUser | kCpsrThumb | kCpsrItEqOne | kCpsrEndian;
    thread.set_cpsr(it_cpsr);
    const auto it_result = thread.call(0x10041, thumb, [](const zb::Stop&) { return false; });
    CHECK(it_result && it_result->r0 == 7);
    CHECK(thread.cpsr() == it_cpsr);
    thread.set_cpsr(kCpsrUser);

    // A return whose sp differs from the call frame is not a return: the handler sees it.
    bool saw_bad_return = false;
    const auto bad_sp = thread.call(0x10060, zb::GuestCall{}, [&](const zb::Stop& stop) {
        saw_bad_return = stop.kind == zb::StopKind::Svc && stop.swi == zb::kHostReturnSwi;
        return false;
    });
    CHECK(!bad_sp && saw_bad_return);
    CHECK(thread.call_depth == 0);
    CHECK(thread.regs() == saved_regs);

    // A stop handler that fails the call.
    const auto failed = thread.call(0x10000, call, [](const zb::Stop&) { return false; });
    CHECK(!failed && thread.call_depth == 0 && thread.regs() == saved_regs);

    zb::GuestCall too_large;
    too_large.stack.resize(0x1000);
    thread.regs()[13] = 0x20004;
    CHECK(!thread.call(0x10000, too_large, [](const zb::Stop&) { return false; }));
    CHECK(thread.call_depth == 0);
}

// Guest code mapped into a running Process from inside its first host call.
constexpr std::uint32_t kCode = 0x60000000;
constexpr std::uint32_t kData = 0x61000000;
constexpr std::uint32_t kSignalCall = kCode + 0x00;  // svc #0x5afe11 (posts r0); add r0, #1
constexpr std::uint32_t kHandler = kCode + 0x0C;     // *kData = r0
constexpr std::uint32_t kRestorer = kCode + 0x18;    // rt_sigreturn
constexpr std::uint32_t kExitCall = kCode + 0x20;    // exit(r0)
constexpr std::uint32_t kBadSpCall = kCode + 0x28;   // add sp, #8; bx lr

void map_call_code(zb::GuestMemory& mem) {
    CHECK(mem.range_free(kCode, 0x1000) && mem.range_free(kData, 0x1000));
    CHECK(mem.map_anon(kCode, 0x1000, PROT_READ | PROT_WRITE));
    CHECK(mem.map_anon(kData, 0x1000, PROT_READ | PROT_WRITE));
    const std::uint32_t words[] = {
        0xEF5AFE11, 0xE2800001, 0xE12FFF1E,  // signal call
        0xE3A03461, 0xE5830000, 0xE12FFF1E,  // handler: mov r3, #0x61000000; str r0, [r3]
        0xE3A070AD, 0xEF000000,              // restorer: mov r7, #173; svc #0
        0xE3A07001, 0xEF000000,              // exit: mov r7, #1; svc #0
        0xE28DD008, 0xE12FFF1E,              // bad sp
    };
    for (std::size_t i = 0; i < sizeof words / sizeof words[0]; ++i) put32(mem, kCode + 4 * i, words[i]);
    CHECK(mem.protect(kCode, 0x1000, PROT_READ | PROT_EXEC));
}

struct ChildResult {
    int status = -1;  // exit code, or -1 if the child did not exit normally
    std::string err;
};

// Runs `body` in a forked child with stderr captured; the child exits with body's value.
ChildResult run_in_child(const std::function<int()>& body) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 2);
        close(fds[1]);
        const int status = body();
        std::fflush(stderr);
        std::_Exit(status & 0xff);
    }
    close(fds[1]);
    ChildResult result;
    char buffer[1024];
    for (;;) {
        const ssize_t n = read(fds[0], buffer, sizeof buffer);
        if (n <= 0) break;
        result.err.append(buffer, static_cast<std::size_t>(n));
    }
    close(fds[0]);
    int wstatus = 0;
    CHECK(waitpid(pid, &wstatus, 0) == pid);
    if (WIFEXITED(wstatus)) result.status = WEXITSTATUS(wstatus);
    return result;
}

// Runs host_call_static; its host call 0xFE10 runs `body` on the main guest thread, and host
// call 0xFE11 posts the signal in r0 to the calling thread. Returns the Process::run status.
int run_scenario(const char* exe, const std::function<void(zb::Process&, zb::GuestThread&)>& body) {
    zb::Process process;
    process.set_host_call_handler([&](std::uint32_t index, zb::GuestThread& thread) {
        if (index == 0xFE10) {
            map_call_code(process.memory());
            thread.regs()[0] = 49;
            body(process, thread);
            return true;
        }
        if (index == 0xFE11) {
            zb::g::siginfo32 info{};
            info.si_signo = static_cast<std::int32_t>(thread.regs()[0]);
            info.si_code = SI_TKILL;
            thread.post_signal(info);
            return true;
        }
        return false;
    });
    return process.run(exe, {exe}, {});
}

void process_level_tests(const char* exe) {
    // A signal posted during a call runs the guest handler, returns through rt_sigreturn, and
    // the call then completes with the stopped state intact.
    ChildResult signal = run_in_child([&] {
        return run_scenario(exe, [](zb::Process& process, zb::GuestThread& thread) {
            process.sigactions[SIGUSR1] = {kHandler, 0x04000004 /* SA_SIGINFO | SA_RESTORER */, kRestorer, {0, 0}};
            const auto saved = thread.regs();
            zb::GuestCall args;
            args.regs = {static_cast<std::uint32_t>(SIGUSR1), 0, 0, 0};
            const auto result = process.call_guest(thread, kSignalCall, args);
            CHECK(result && result->r0 == static_cast<std::uint32_t>(SIGUSR1) + 1);
            CHECK(get32(process.memory(), kData) == static_cast<std::uint32_t>(SIGUSR1));
            CHECK(thread.regs() == saved && thread.sigmask == 0 && thread.call_depth == 0);
        });
    });
    std::fputs(signal.err.c_str(), stderr);
    CHECK(signal.status == 0);

    // A thread exit inside a call ends the host process: bionic has already torn that thread down.
    ChildResult exited = run_in_child([&] {
        run_scenario(exe, [](zb::Process& process, zb::GuestThread& thread) {
            zb::GuestCall args;
            args.regs = {5, 0, 0, 0};
            (void)process.call_guest(thread, kExitCall, args);
            std::_Exit(3);  // must not be reached
        });
        return 4;
    });
    CHECK(exited.status == 1);
    CHECK(exited.err.find("guest thread exited inside a host-to-guest call") != std::string::npos);

    // The return svc outside any call is an illegal instruction.
    ChildResult stray = run_in_child([&] {
        return run_scenario(exe, [](zb::Process&, zb::GuestThread& thread) {
            thread.regs()[15] = zb::kHostReturnAddress;
        });
    });
    CHECK(stray.status == 128 + SIGILL);
    CHECK(stray.err.find("host return svc outside a host-to-guest call") != std::string::npos);

    // A return with the wrong sp (longjmp or unwind across the frame) is fatal the same way.
    ChildResult bad_sp = run_in_child([&] {
        return run_scenario(exe, [](zb::Process& process, zb::GuestThread& thread) {
            CHECK(!process.call_guest(thread, kBadSpCall, zb::GuestCall{}));
            CHECK(process.exiting() && thread.call_depth == 0);
        });
    });
    CHECK(bad_sp.status == 128 + SIGILL);
    CHECK(bad_sp.err.find("crossed the host-to-guest call frame") != std::string::npos);

    // A fatal signal inside a call on the only guest thread fails the call and ends run() with
    // the signal status instead of ending the host process.
    ChildResult fatal = run_in_child([&] {
        return run_scenario(exe, [](zb::Process& process, zb::GuestThread& thread) {
            zb::GuestCall args;
            args.regs = {static_cast<std::uint32_t>(SIGUSR2), 0, 0, 0};
            CHECK(!process.call_guest(thread, kSignalCall, args));
            CHECK(process.exiting() && process.exit_status() == 128 + SIGUSR2);
        });
    });
    CHECK(fatal.status == 128 + SIGUSR2);
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    thread_level_tests();
    process_level_tests(argv[1]);
    std::puts("guest_call_test PASS");
    return 0;
}
