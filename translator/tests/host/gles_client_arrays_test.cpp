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

void call(zb::HostGl& host, zb::LibraryRuntime& runtime, zb::GuestThread& thread,
          std::uint32_t index, const std::array<std::uint32_t, 8>& words) {
    for (unsigned i = 0; i < 4; ++i) thread.regs()[i] = words[i];
    thread.regs()[13] = kStack;
    std::memcpy(runtime.memory().base() + kStack, words.data() + 4,
                (words.size() - 4) * sizeof(words[0]));
    CHECK(host.handle_host_call(index, thread));
}

const MockGles::Call* last(const MockGles& backend, const char* name) {
    for (auto it = backend.calls().rbegin(); it != backend.calls().rend(); ++it) {
        if (it->name == name) return &*it;
    }
    return nullptr;
}

}  // namespace

int main() {
    zb::LibraryRuntime runtime;
    CHECK(runtime.memory().map_anon(kStack, 0x1000, PROT_READ | PROT_WRITE));
    CHECK(runtime.memory().map_anon(kData, 0x2000, PROT_READ | PROT_WRITE));
    MockGles backend;
    zb::HostGl host(runtime, backend);
    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);

    // Client attribute: pointer setup is deferred, and non-zero first advances by stride.
    call(host, runtime, thread, zb::ZB_GL_HC_glBindBuffer, {0x8892, 0});
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glVertexAttribPointer,
         {0, 2, 0x1406, 0, 0, kData + 0x100});
    CHECK(backend.calls().empty());
    call(host, runtime, thread, zb::ZB_GL_HC_glEnableVertexAttribArray, {0});
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawArrays, {4, 1, 2});
    const MockGles::Call* pointer = last(backend, "glVertexAttribPointer");
    CHECK(pointer != nullptr);
    CHECK(pointer->arguments[5] ==
          reinterpret_cast<std::uintptr_t>(runtime.memory().base() + kData + 0x100));
    CHECK(last(backend, "glDrawArrays") != nullptr);

    // An explicit stride larger than the element is used for the last-element bound.
    call(host, runtime, thread, zb::ZB_GL_HC_glVertexAttribPointer,
         {0, 2, 0x1406, 0, 16, kData + 0x200});
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawArrays, {4, 2, 3});
    pointer = last(backend, "glVertexAttribPointer");
    CHECK(pointer != nullptr);
    CHECK(pointer->arguments[5] ==
          reinterpret_cast<std::uintptr_t>(runtime.memory().base() + kData + 0x200));

    // A buffer-backed attribute is forwarded immediately and coexists with the client one.
    call(host, runtime, thread, zb::ZB_GL_HC_glBindBuffer, {0x8892, 7});
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glVertexAttribPointer,
         {1, 3, 0x1406, 0, 12, 32});
    CHECK(backend.calls().size() == 1 && backend.calls()[0].name == "glVertexAttribPointer");
    call(host, runtime, thread, zb::ZB_GL_HC_glEnableVertexAttribArray, {1});
    call(host, runtime, thread, zb::ZB_GL_HC_glBindBuffer, {0x8892, 0});

    // Client U8 and U16 indices are scanned for their maximum and translated for the driver.
    const std::uint8_t indices8[] = {2, 0, 1};
    std::memcpy(runtime.memory().base() + kData + 0x20, indices8, sizeof(indices8));
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawElements,
         {4, 3, 0x1401, kData + 0x20});
    const MockGles::Call* draw = last(backend, "glDrawElements");
    CHECK(draw != nullptr);
    CHECK(draw->arguments[3] ==
          reinterpret_cast<std::uintptr_t>(runtime.memory().base() + kData + 0x20));

    const std::uint16_t indices16[] = {1, 3, 2};
    std::memcpy(runtime.memory().base() + kData + 0x40, indices16, sizeof(indices16));
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawElements,
         {4, 3, 0x1403, kData + 0x40});
    CHECK(last(backend, "glDrawElements") != nullptr);

    // With an element buffer bound, the guest value is an offset and is not dereferenced.
    call(host, runtime, thread, zb::ZB_GL_HC_glBindBuffer, {0x8893, 9});
    backend.clear_calls();
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawElements, {4, 3, 0x1403, 32});
    draw = last(backend, "glDrawElements");
    CHECK(draw != nullptr && draw->arguments[3] == 32);
    call(host, runtime, thread, zb::ZB_GL_HC_glBindBuffer, {0x8893, 0});

    // The query returns the original guest address, never the materialized host pointer.
    call(host, runtime, thread, zb::ZB_GL_HC_glGetVertexAttribPointerv,
         {0, 0x8645, kData + 0x80});
    std::uint32_t queried = 0;
    std::memcpy(&queried, runtime.memory().base() + kData + 0x80, sizeof(queried));
    CHECK(queried == kData + 0x200);

    backend.set_error(0);
    call(host, runtime, thread, zb::ZB_GL_HC_glGetVertexAttribPointerv, {0, 0x8645, 0});
    CHECK(backend.error() == zb::kGlInvalidValue);

    // A client index that selects outside the mapped attribute range drops the draw.
    call(host, runtime, thread, zb::ZB_GL_HC_glVertexAttribPointer,
         {0, 4, 0x1406, 0, 0, kData + 0x1FF0});
    const std::uint8_t bad_index[] = {1};
    std::memcpy(runtime.memory().base() + kData + 0x60, bad_index, sizeof(bad_index));
    backend.clear_calls();
    backend.set_error(0);
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawElements,
         {4, 1, 0x1401, kData + 0x60});
    CHECK(last(backend, "glDrawElements") == nullptr);
    CHECK(backend.error() == zb::kGlInvalidOperation);

    // An enabled client attribute whose draw range crosses guest memory drops the draw.
    call(host, runtime, thread, zb::ZB_GL_HC_glVertexAttribPointer,
         {0, 4, 0x1406, 0, 0, kData + 0x1FF8});
    backend.clear_calls();
    backend.set_error(0);
    call(host, runtime, thread, zb::ZB_GL_HC_glDrawArrays, {4, 0, 2});
    CHECK(last(backend, "glDrawArrays") == nullptr);
    CHECK(backend.error() == zb::kGlInvalidOperation);

    std::puts("gles_client_arrays_test PASS");
    return 0;
}
