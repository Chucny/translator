// Process host-call dispatch, plus borrowed JITs: a borrower created from a guest thread parked
// in a host call inherits its identity and emulated state, receives tid-directed signals, and
// hands its signal mask, alternate stack and pending signals back when destroyed.
// Usage: host_call_dispatch_test <build/guest/host_call_static>
#include <signal.h>

#include <string>
#include <thread>

#include "check.h"
#include "zb/process.h"
#include "zb/runtime_report.h"

namespace {

std::uint64_t sig_bit(int sig) {
    return 1ULL << (sig - 1);
}

void check_borrower(zb::Process& process, zb::GuestThread& carrier) {
    const std::uint64_t saved_mask = carrier.sigmask;
    const zb::g::stack32 saved_altstack = carrier.altstack;
    const std::uint32_t saved_fpscr = carrier.fpscr();
    carrier.sigmask = sig_bit(SIGUSR2);
    carrier.altstack = {0x50000, 0, 0x4000};
    carrier.set_fpscr(0x00C00000);

    std::thread([&] {
        std::unique_ptr<zb::GuestThread> borrower = process.create_borrower(carrier);
        CHECK(borrower != nullptr);
        CHECK(borrower->processor_id() != carrier.processor_id());
        CHECK(borrower->tid == carrier.tid && borrower->tls() == carrier.tls());
        CHECK(borrower->regs()[13] == (carrier.regs()[13] & ~7u));
        CHECK(borrower->sigmask == sig_bit(SIGUSR2) && borrower->fpscr() == 0x00C00000);
        CHECK(borrower->altstack.ss_sp == 0x50000 && borrower->altstack.ss_size == 0x4000);
        CHECK(process.find_thread(carrier.tid) == borrower.get());
        CHECK(process.thread_count() == 1);
        process.invalidate(0x10000, 0x1000);

        borrower->sigmask = sig_bit(SIGUSR1);
        borrower->altstack = {0x60000, 0, 0x8000};
        zb::g::siginfo32 info{};
        info.si_signo = SIGUSR1;
        borrower->post_signal(info);
        process.destroy_borrower(std::move(borrower), carrier);
    }).join();

    CHECK(carrier.sigmask == sig_bit(SIGUSR1));
    CHECK(carrier.altstack.ss_sp == 0x60000 && carrier.altstack.ss_size == 0x8000);
    CHECK(carrier.pending_signals() == sig_bit(SIGUSR1));
    CHECK(process.find_thread(carrier.tid) == &carrier);

    zb::g::siginfo32 moved;
    CHECK(carrier.take_signal(0, moved) && moved.si_signo == SIGUSR1);
    carrier.sigmask = saved_mask;
    carrier.altstack = saved_altstack;
    carrier.set_fpscr(saved_fpscr);
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    zb::runtime_report().clear();
    zb::Process process;
    bool called = false;
    process.set_host_call_handler([&](std::uint32_t index, zb::GuestThread& thread) {
        if (index != 0xFE10) return false;
        CHECK(thread.regs()[0] == 7);
        check_borrower(process, thread);
        thread.regs()[0] = 49;
        called = true;
        return true;
    });
    CHECK(process.run(argv[1], {argv[1]}, {}) == 0);
    CHECK(called);

    // The declined host call kept guest semantics (r0 = 0, the guest ran on to exit 0) and is the
    // only record a silent device run leaves behind.
    zb::RuntimeReport& report = zb::runtime_report();
    CHECK(report.first_unimplemented_host_call() == "libGLESv2.so glCreateProgram");
    CHECK(report.unimplemented_host_calls() == 1);
    const std::string text = report.text();
    CHECK(text.find("first-unimplemented: libGLESv2.so glCreateProgram") != std::string::npos);
    CHECK(text.find("unimplemented: libGLESv2.so glCreateProgram x1") != std::string::npos);
    CHECK(text.find("guest-exit: guest exited with status 0") != std::string::npos);

    std::puts("host_call_dispatch_test PASS");
    return 0;
}
