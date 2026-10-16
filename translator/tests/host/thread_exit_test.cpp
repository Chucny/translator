// A cloned guest thread stops naming itself as the host thread's current guest thread before its
// GuestThread is retired and freed; otherwise a host signal landing on that host thread in the
// window would take the forwarding fast path with a dangling pointer. Retirement is observed at
// clear_process_signal_target(), which every thread exit calls just before freeing.
// Usage: thread_exit_test <sysroot> <build/guest/threads_dynamic>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <string>

#include "check.h"
#include "zb/process.h"

namespace {

std::atomic<long> run_host_tid{0};
std::atomic<int> clone_retirements{0};
std::atomic<int> violations{0};

}  // namespace

extern "C" void real_clear_target(zb::GuestThread*) asm("__real__ZN2zb7Process27clear_process_signal_targetEPNS_11GuestThreadE");
extern "C" void wrapped_clear_target(zb::GuestThread*) asm("__wrap__ZN2zb7Process27clear_process_signal_targetEPNS_11GuestThreadE");

extern "C" void wrapped_clear_target(zb::GuestThread* thread) {
    // The Process::run() thread retires main_ while still naming it (by design); every other
    // host thread here is a cloned guest thread that is about to be freed.
    if (::syscall(SYS_gettid) != run_host_tid.load()) {
        ++clone_retirements;
        if (zb::Process::current_thread() != nullptr) ++violations;
    }
    real_clear_target(thread);
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    run_host_tid.store(::syscall(SYS_gettid));
    zb::Process process;
    process.set_sysroot(argv[1]);
    CHECK(process.run(argv[2], {argv[2]}, {}) == 0);
    std::printf("cloned thread retirements: %d, still current: %d\n", clone_retirements.load(), violations.load());
    CHECK(clone_retirements.load() >= 8);
    CHECK(violations.load() == 0);
    std::puts("thread_exit_test PASS");
    return 0;
}
