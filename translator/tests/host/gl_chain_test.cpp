// Proves that GuestJniEngine chains a GlBackend's HostGl into the same graph as HostJni (Phase 5
// Task 8): GL host-call indices reach the GLES dispatcher, and the engine leaves host_gl() null
// when no GlBackend is supplied (the pre-Task-8 host build shape).
#include <sys/mman.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "mock_gles.h"
#include "mock_jvm.h"
#include "zb/gl_hostcalls.h"
#include "zb/host_gl.h"
#include "zb/proxy_runtime.h"
#include "zb/runtime_report.h"

// HostJni aborts if a second one is constructed in the same process (one JNI bridge per
// process), so each case below is its own process, like guest_jni_engine_test.cpp.
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string mode = argv[1];
    CHECK(mode == "no-backend" || mode == "with-backend");

    if (mode == "no-backend") {
        // No GlBackend: the engine still works (host_jni chaining alone), host_gl() is null.
        auto* vm = new zb::mock::MockJvm();
        auto* engine = new zb::GuestJniEngine(*vm);
        CHECK(engine->host_gl() == nullptr);
        std::puts("gl_chain_test no-backend PASS");
        std::fflush(stdout);
        std::_Exit(0);
    }

    // With a GlBackend: host_gl() is wired to it, and a GL host-call index reaches the mock
    // driver through the exact object the engine's chained handler calls.
    auto* vm = new zb::mock::MockJvm();
    auto* backend = new MockGles();
    zb::runtime_report().clear();
    bool probed = false;
    auto* engine = new zb::GuestJniEngine(*vm, backend, [&probed] {
        probed = true;
        return true;
    });
    CHECK(engine->host_gl() != nullptr);
    CHECK(&engine->host_gl()->backend() == backend);

    zb::LibraryRuntime& runtime = engine->runtime();
    CHECK(runtime.memory().map_anon(0x10000, 0x1000, PROT_READ | PROT_WRITE));
    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);
    thread.regs()[13] = 0x10000;

    // glClear(GL_COLOR_BUFFER_BIT): pointerless, reaches the mock backend.
    thread.regs()[0] = 0x00004000;
    CHECK(engine->host_gl()->handle_host_call(zb::ZB_GL_HC_glClear, thread));
    CHECK(backend->calls().size() == 1);
    CHECK(backend->calls().back().name == "glClear");
    CHECK(probed);
    CHECK(zb::runtime_report().gl_calls() == 1);

    // A JNI-range index is not a GL call; HostGl declines it so the JNI half of the chain gets
    // a turn (mirrors what the lambda in the constructor does).
    CHECK(!engine->host_gl()->handle_host_call(0xFC00, thread));

    zb::runtime_report().clear();
    std::puts("gl_chain_test with-backend PASS");
    std::fflush(stdout);
    std::_Exit(0);
}
