// Proves that GuestJniEngine chains an EglBackend's HostEgl and a NativeWindowBackend's
// HostNativeWindow into the same graph as HostGl/HostAssets/HostJni (Phase 7a Task 6): an egl*
// and an ANativeWindow_* host call both reach their handler, an unknown window handle is
// rejected without reaching the backend, and the engine leaves host_egl()/host_native_window()
// null when no backend is supplied. The window-surface mode (Phase 7a Task 9) also runs the real
// guest probe guest/tests/zbeglprobe.c through LibraryRuntime end to end: display/config/context,
// then a window surface created from a handle HostNativeWindow's mock backend hands out, through
// eglMakeCurrent/glClear/eglSwapBuffers.
// Usage: egl_chain_test no-backend
//        egl_chain_test with-backend
//        egl_chain_test window-surface <sysroot> <zbhost> <guest lib dir>
#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "mock_egl.h"
#include "mock_jvm.h"
#include "mock_native_window.h"
#include "zb/egl_backend.h"
#include "zb/egl_hostcalls.h"
#include "zb/host_jni.h"
#include "zb/library_protocol.h"
#include "zb/proxy_runtime.h"
#include "zb/window_hostcalls.h"

namespace {

// Allocates `size` bytes of guest memory through the service thread's real malloc and copies
// `text` (with its NUL) into it. Returns the guest address.
std::uint32_t write_guest_string(zb::LibraryRuntime& runtime, const std::string& text) {
    zb::GuestCall call;
    call.regs[0] = static_cast<std::uint32_t>(text.size() + 1);
    const auto allocated = runtime.call_on_service(runtime.service_api().malloc_fn, call);
    CHECK(allocated && allocated->r0 != 0);
    std::uint8_t* host = runtime.memory().host_ptr(allocated->r0, text.size() + 1, zb::kPageWrite);
    CHECK(host != nullptr);
    std::memcpy(host, text.c_str(), text.size() + 1);
    return allocated->r0;
}

void run_window_surface_mode(int argc, char** argv) {
    CHECK(argc == 5);
    auto* vm = new zb::mock::MockJvm();
    auto* egl_backend = new MockEgl();
    auto* window_backend = new MockNativeWindow();
    // Enough of a driver for the whole probe sequence to succeed: a non-zero display/context/
    // surface handle (0 means EGL_NO_*), a successful init, one config, and true from every
    // boolean EGL call the probe makes.
    egl_backend->set_result("eglGetDisplay", 0x7000);
    egl_backend->set_result("eglInitialize", 1);
    egl_backend->set_configs({reinterpret_cast<void*>(0x1000)});
    egl_backend->set_result("eglCreateContext", 0x3000);
    egl_backend->set_result("eglCreateWindowSurface", 0x4000);
    egl_backend->set_result("eglMakeCurrent", 1);
    egl_backend->set_result("eglSwapBuffers", 1);

    auto* engine = new zb::GuestJniEngine(*vm, /*gl_backend=*/nullptr, /*egl_context_probe=*/{},
                                          /*asset_backend=*/nullptr, egl_backend, window_backend);

    zb::LibraryRuntimeOptions options;
    options.sysroot = argv[2];
    options.zbhost = argv[3];
    options.target_sdk = 16;
    options.guest_environment = {std::string("LD_LIBRARY_PATH=") + argv[4]};
    options.preload = "libzbjni.so";
    std::string error;
    CHECK(engine->start(options, error));

    zb::LibraryRuntime& runtime = engine->runtime();

    // The probe cannot call ANativeWindow_fromSurface itself (it needs a real Java Surface), so
    // this test creates the window handle the same way HostNativeWindow does: a synthetic
    // ANativeWindow_fromSurface host call against the mock backend.
    vm->define_class("android/view/Surface");
    const zb::mock::MockJvm::ObjectId surface_object = vm->new_object("android/view/Surface");
    const std::uint32_t surface_handle = engine->host_jni().new_local_handle(surface_object);
    CHECK(surface_handle != 0);

    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread scratch(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);
    scratch.regs()[0] = 0;
    scratch.regs()[1] = surface_handle;
    CHECK(engine->host_native_window()->handle_host_call(zb::ZB_WINDOW_HC_ANativeWindow_fromSurface, scratch));
    const std::uint32_t window = scratch.regs()[0];
    CHECK(window != 0);
    CHECK(window_backend->last_surface != nullptr);

    const std::uint32_t library =
        runtime.load_library(std::string(argv[4]) + "/libzbeglprobe.so", ZB_GUEST_RTLD_NOW, error);
    CHECK(library != 0);
    const std::uint32_t main_symbol = runtime.find_symbol(library, "main", error);
    CHECK(main_symbol != 0);

    // Build a real argv for the probe's main(argc, argv): argv[0] a program name, argv[1] the
    // window handle in decimal (the probe converts it with strtoul), argv[2] the NULL terminator.
    const std::uint32_t prog_addr = write_guest_string(runtime, "zbeglprobe");
    const std::uint32_t handle_addr = write_guest_string(runtime, std::to_string(window));
    zb::GuestCall alloc_argv;
    alloc_argv.regs[0] = 3 * sizeof(std::uint32_t);
    const auto argv_alloc = runtime.call_on_service(runtime.service_api().malloc_fn, alloc_argv);
    CHECK(argv_alloc && argv_alloc->r0 != 0);
    std::uint8_t* argv_host = runtime.memory().host_ptr(argv_alloc->r0, 3 * sizeof(std::uint32_t), zb::kPageWrite);
    CHECK(argv_host != nullptr);
    const std::uint32_t argv_words[3] = {prog_addr, handle_addr, 0};
    std::memcpy(argv_host, argv_words, sizeof argv_words);

    zb::GuestCall call;
    call.regs[0] = 2;
    call.regs[1] = argv_alloc->r0;
    const auto result = runtime.call_on_service(main_symbol, call);
    CHECK(result.has_value());
    CHECK(result->r0 == 0);

    bool saw_make_current = false;
    bool saw_swap_buffers = false;
    for (const auto& c : egl_backend->calls()) {
        if (c.name == "eglMakeCurrent") saw_make_current = true;
        if (c.name == "eglSwapBuffers") saw_swap_buffers = true;
    }
    CHECK(saw_make_current);
    CHECK(saw_swap_buffers);

    std::puts("egl_chain_test window-surface PASS");
    std::fflush(stdout);
    std::_Exit(0);
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc >= 2);
    const std::string mode = argv[1];
    CHECK(mode == "no-backend" || mode == "with-backend" || mode == "window-surface");

    if (mode == "window-surface") {
        run_window_surface_mode(argc, argv);
        return 0;
    }
    CHECK(argc == 2);

    if (mode == "no-backend") {
        // Neither backend: the engine still works, host_egl()/host_native_window() are null.
        auto* vm = new zb::mock::MockJvm();
        auto* engine = new zb::GuestJniEngine(*vm);
        CHECK(engine->host_egl() == nullptr);
        CHECK(engine->host_native_window() == nullptr);
        std::puts("egl_chain_test no-backend PASS");
        std::fflush(stdout);
        std::_Exit(0);
    }

    // With both backends: host_egl()/host_native_window() are wired to them, and an egl* and an
    // ANativeWindow_* host-call index reach their mock backends through the chained handler.
    auto* vm = new zb::mock::MockJvm();
    auto* egl_backend = new MockEgl();
    auto* window_backend = new MockNativeWindow();
    egl_backend->set_result("eglGetError", zb::kEglSuccess);
    auto* engine = new zb::GuestJniEngine(*vm, /*gl_backend=*/nullptr, /*egl_context_probe=*/{},
                                          /*asset_backend=*/nullptr, egl_backend, window_backend);
    CHECK(engine->host_egl() != nullptr);
    CHECK(engine->host_native_window() != nullptr);
    CHECK(&engine->host_egl()->backend() == egl_backend);

    zb::LibraryRuntime& runtime = engine->runtime();
    CHECK(runtime.memory().map_anon(0x10000, 0x1000, PROT_READ | PROT_WRITE));
    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);
    thread.regs()[13] = 0x10000;

    // eglGetError reaches the mock EGL driver through the chained HostEgl.
    thread.regs()[0] = 0;
    thread.regs()[1] = 0;
    thread.regs()[2] = 0;
    thread.regs()[3] = 0;
    CHECK(engine->host_egl()->handle_host_call(zb::ZB_EGL_HC_eglGetError, thread));
    CHECK(thread.regs()[0] == static_cast<std::uint32_t>(zb::kEglSuccess));

    // ANativeWindow_getWidth on an unknown handle reaches HostNativeWindow, returns -1 and never
    // touches the backend; no crash.
    thread.regs()[0] = 0xdeadbeef;
    CHECK(engine->host_native_window()->handle_host_call(zb::ZB_WINDOW_HC_ANativeWindow_getWidth, thread));
    CHECK(static_cast<std::int32_t>(thread.regs()[0]) == -1);

    // A JNI-range index is not an EGL or window call; both decline it so the JNI half of the
    // chain gets a turn.
    CHECK(!engine->host_egl()->handle_host_call(0xFC00, thread));
    CHECK(!engine->host_native_window()->handle_host_call(0xFC00, thread));

    std::puts("egl_chain_test with-backend PASS");
    std::fflush(stdout);
    std::_Exit(0);
}
