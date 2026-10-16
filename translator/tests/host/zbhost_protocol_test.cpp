// zbhost publishes a valid service API through READY, and a failed preload ends it with
// ZB_HOST_EXIT_PRELOAD before READY.
// Usage: zbhost_protocol_test <sysroot> <zbhost> <build/guest/lib>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "check.h"
#include "zb/library_protocol.h"
#include "zb/process.h"

int main(int argc, char** argv) {
    CHECK(argc == 4);
    const std::string library_path = std::string("LD_LIBRARY_PATH=") + argv[3];

    std::fflush(stdout);
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        zb::Process process;
        process.set_sysroot(argv[1]);
        const int status = process.run(argv[2], {argv[2], "16", "libzb-does-not-exist.so"}, {library_path});
        std::_Exit(status == ZB_HOST_EXIT_PRELOAD ? 0 : 1);
    }
    int wstatus = 0;
    CHECK(waitpid(child, &wstatus, 0) == child);
    CHECK(WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0);

    zb::Process process;
    process.set_sysroot(argv[1]);
    bool ready = false;
    process.set_host_call_handler([&](std::uint32_t index, zb::GuestThread& thread) {
        if (index != ZB_SERVICE_READY_INDEX) return false;
        const std::uint8_t* source = process.memory().host_ptr(thread.regs()[0], sizeof(zb_service_api), zb::kPageRead);
        CHECK(source != nullptr);
        zb_service_api api;
        std::memcpy(&api, source, sizeof api);
        CHECK(api.size == sizeof api && api.version == ZB_SERVICE_PROTOCOL_VERSION);
        CHECK(api.dlopen_fn != 0 && api.dlsym_fn != 0 && api.dlerror_fn != 0);
        CHECK(api.spawn_carrier_fn != 0 && api.malloc_fn != 0 && api.free_fn != 0);
        CHECK(api.scratch_size == ZB_SERVICE_SCRATCH_SIZE);
        CHECK(process.memory().host_ptr(api.scratch, api.scratch_size, zb::kPageWrite) != nullptr);
        ready = true;
        thread.regs()[0] = 0;
        return true;
    });
    CHECK(process.run(argv[2], {argv[2], "16"}, {library_path}) == 0);
    CHECK(process.thread_count() == 0);
    CHECK(zb::Process::current_thread() == nullptr);
    CHECK(ready);
    std::puts("zbhost_protocol_test PASS");
    return 0;
}
