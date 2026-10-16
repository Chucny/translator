#include <sys/mman.h>

#include <array>
#include <cstdint>
#include <cstring>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "mock_gles.h"
#include "zb/gl_hostcalls.h"
#include "zb/host_gl.h"

namespace {

constexpr std::uint32_t kStack = 0x10000;
constexpr std::uint32_t kData = 0x20000;
constexpr std::uint32_t kReadOnly = 0x30000;

void set_words(zb::LibraryRuntime& runtime, zb::GuestThread& thread,
               const std::array<std::uint32_t, 8>& words) {
    for (unsigned i = 0; i < 4; ++i) thread.regs()[i] = words[i];
    thread.regs()[13] = kStack;
    std::memcpy(runtime.memory().base() + kStack, words.data() + 4,
                (words.size() - 4) * sizeof(words[0]));
}

void rejected(zb::HostGl& host, zb::GuestThread& thread, MockGles& backend,
              zb::LibraryRuntime& runtime, std::uint32_t index,
              const std::array<std::uint32_t, 8>& words) {
    backend.clear_calls();
    backend.set_error(0);
    set_words(runtime, thread, words);
    CHECK(host.handle_host_call(index, thread));
    CHECK(backend.calls().empty());
    CHECK(backend.error() == zb::kGlInvalidValue);
}

}  // namespace

int main() {
    zb::LibraryRuntime runtime;
    CHECK(runtime.memory().map_anon(kStack, 0x1000, PROT_READ | PROT_WRITE));
    CHECK(runtime.memory().map_anon(kData, 0x1000, PROT_READ | PROT_WRITE));
    CHECK(runtime.memory().map_anon(kReadOnly, 0x1000, PROT_READ));
    MockGles backend;
    zb::HostGl host(runtime, backend);
    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);

    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glDeleteBuffers,
             {1, 0xFFFFFFFEu});
    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glDeleteBuffers,
             {1, 0x40000});
    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glGenTextures,
             {1, kReadOnly});
    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glUniform4fv,
             {0, 0x40000000u, kData});
    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glUniform4fv,
             {0, 0x40000000u, 0});
    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glGenTextures,
             {0x7FFFFFFFu, kData});
    rejected(host, thread, backend, runtime, zb::ZB_GL_HC_glBufferData,
             {0x8892, 0xFFFFFFFFu, kData, 0x88E4});

    // Null data is legal for buffer allocation and remains null at the host boundary.
    backend.clear_calls();
    backend.set_error(0);
    set_words(runtime, thread, {0x8892, 16, 0, 0x88E4});
    CHECK(host.handle_host_call(zb::ZB_GL_HC_glBufferData, thread));
    CHECK(backend.calls().size() == 1 && backend.calls()[0].name == "glBufferData");
    CHECK(backend.calls()[0].arguments[2] == 0);
    CHECK(backend.error() == 0);

    std::puts("gles_pointer_test PASS");
    return 0;
}
