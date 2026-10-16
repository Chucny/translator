// Process-directed host signals reach the guest even when they land on a thread that runs no
// guest code. This is the app-process situation: the kernel may hand SIGALRM from setitimer to
// any ART thread. Here the guest runs on a thread with SIGALRM blocked on the host, while a
// helper thread with it unblocked is the only possible receiver.
// Usage: async_signal_test <sysroot> <build/guest dir>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "check.h"
#include "zb/zbridge.h"

int main(int argc, char** argv) {
    CHECK(argc == 3);
    const std::string sysroot = argv[1];
    const std::string guest = std::string(argv[2]) + "/syscalls_dynamic";
    const std::string tmp_dir = std::string(argv[2]) + "/async_signal_tmp";
    const std::string out_path = std::string(argv[2]) + "/async_signal_test.stdout";

    std::atomic<bool> done{false};
    std::thread helper([&] {
        while (!done.load()) usleep(1000);
    });

    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    CHECK(pthread_sigmask(SIG_BLOCK, &block, nullptr) == 0);

    int out = open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(out >= 0);
    std::fflush(stdout);
    const int saved = dup(1);
    dup2(out, 1);
    const char* guest_argv[] = {guest.c_str(), tmp_dir.c_str(), nullptr};
    const int status = zb_run_executable(sysroot.c_str(), 2, guest_argv, nullptr);
    dup2(saved, 1);
    close(saved);
    close(out);

    done.store(true);
    helper.join();

    std::ifstream in(out_path);
    std::stringstream text;
    text << in.rdbuf();
    std::printf("guest exit %d\n%s", status, text.str().c_str());
    CHECK(status == 0);
    CHECK(text.str().find("setitimer=PASS") != std::string::npos);
    std::printf("async_signal_test ok\n");
    return 0;
}
