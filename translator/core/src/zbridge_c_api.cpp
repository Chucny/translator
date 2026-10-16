#include "zb/zbridge.h"

#include <string>
#include <vector>

#include "zb/process.h"

extern char** environ;

int zb_run_executable(const char* sysroot, int argc, const char* const* argv, const char* const* envp) {
    if (argc < 1 || argv == nullptr || argv[0] == nullptr) return 2;

    std::vector<std::string> guest_argv(argv, argv + argc);
    std::vector<std::string> guest_envp;
    if (envp != nullptr) {
        for (const char* const* e = envp; *e != nullptr; ++e) guest_envp.emplace_back(*e);
    } else {
        // Host loader variables describe 64-bit host libraries and would break the guest linker.
        for (char** e = environ; *e != nullptr; ++e) {
            const std::string entry = *e;
            if (entry.rfind("LD_PRELOAD=", 0) == 0 || entry.rfind("LD_LIBRARY_PATH=", 0) == 0) continue;
            guest_envp.push_back(entry);
        }
    }

    zb::Process process;
    if (sysroot != nullptr) process.set_sysroot(sysroot);
    return process.run(guest_argv[0], guest_argv, guest_envp);
}
