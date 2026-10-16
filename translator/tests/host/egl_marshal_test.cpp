#include <sys/mman.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include <dynarmic/interface/exclusive_monitor.h>

#include "check.h"
#include "mock_egl.h"
#include "zb/egl_hostcalls.h"
#include "zb/host_egl.h"

namespace {

constexpr std::uint32_t kStack = 0x10000;
constexpr std::uint32_t kData = 0x20000;
constexpr zb::EGLint kEglNone = 0x3038;
constexpr zb::EGLint kEglRedSize = 0x3024;
constexpr zb::EGLint kEglVendor = 0x3053;
constexpr zb::EGLint kEglBadDisplay = 0x3008;
constexpr zb::EGLint kEglBadNativeWindow = 0x300b;

zb::LibraryRuntime* g_runtime = nullptr;
zb::GuestThread* g_thread = nullptr;
zb::HostEgl* g_egl = nullptr;
MockEgl* g_backend = nullptr;

std::uint32_t call_egl(std::uint32_t index, std::initializer_list<std::uint32_t> words) {
    std::array<std::uint32_t, 12> all{};
    std::size_t i = 0;
    for (std::uint32_t word : words) all[i++] = word;
    for (unsigned r = 0; r < 4; ++r) g_thread->regs()[r] = all[r];
    g_thread->regs()[13] = kStack;
    std::memcpy(g_runtime->memory().base() + kStack, all.data() + 4,
                (all.size() - 4) * sizeof(all[0]));
    CHECK(g_egl->handle_host_call(index, *g_thread));
    return g_thread->regs()[0];
}

std::uint32_t guest_word(std::uint32_t address) {
    std::uint32_t value;
    std::memcpy(&value, g_runtime->memory().base() + address, sizeof value);
    return value;
}

void write_words(std::uint32_t address, std::initializer_list<std::uint32_t> words) {
    std::uint32_t cursor = address;
    for (std::uint32_t word : words) {
        std::memcpy(g_runtime->memory().base() + cursor, &word, sizeof word);
        cursor += 4;
    }
}

}  // namespace

int main() {
    CHECK(zb::kEglHostCallCount == 44);
    CHECK(zb::kEglHostCallFirst == 168 && zb::kEglHostCallLast == 211);

    zb::LibraryRuntime runtime;
    CHECK(runtime.memory().map_anon(kStack, 0x1000, PROT_READ | PROT_WRITE));
    CHECK(runtime.memory().map_anon(kData, 0x1000, PROT_READ | PROT_WRITE));
    MockEgl backend;
    std::uint32_t next_allocation = kData + 0x800;
    zb::HostEgl egl(
        runtime, backend,
        [&](std::size_t size) -> std::optional<std::uint32_t> {
            const std::uint32_t result = next_allocation;
            next_allocation += static_cast<std::uint32_t>((size + 7) & ~std::size_t{7});
            if (next_allocation > kData + 0xF00) return std::nullopt;
            return result;
        },
        [](std::uint32_t handle) -> const void* {
            return handle == 42 ? reinterpret_cast<const void*>(0x5000) : nullptr;
        },
        // Stands in for the guest linker: it answers for the stubs the guest really has.
        [](const std::string& name) -> std::uint32_t {
            return name == "eglCreateContext" || name == "glClear" ? 0x9000 : 0;
        });
    Dynarmic::ExclusiveMonitor monitor(1);
    zb::GuestThread thread(runtime.memory(), &monitor, 0, false, zb::kCarrierCodeCacheSize);
    g_runtime = &runtime;
    g_thread = &thread;
    g_egl = &egl;
    g_backend = &backend;

    // Indices outside 168-211 belong to other dispatchers.
    CHECK(!egl.handle_host_call(167, thread));
    CHECK(!egl.handle_host_call(212, thread));

    // A display handle is stable, never 0, and an unknown handle never reaches the backend.
    backend.set_result("eglGetDisplay", 0x7000);
    const std::uint32_t display = call_egl(zb::ZB_EGL_HC_eglGetDisplay, {0});
    CHECK(display != 0);
    CHECK(egl.value_for(display) == reinterpret_cast<const void*>(0x7000));
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetDisplay, {0}) == display);

    backend.clear_calls();
    CHECK(call_egl(zb::ZB_EGL_HC_eglInitialize, {display + 1000, 0, 0}) == 0);
    CHECK(backend.calls().empty());
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetError, {}) ==
          static_cast<std::uint32_t>(kEglBadDisplay));
    // The pending error is consumed once.
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetError, {}) == 0);

    // A known display reaches the backend, and EGLint out-parameters alias guest memory.
    backend.clear_calls();
    backend.set_result("eglInitialize", 1);
    write_words(kData, {0, 0});
    CHECK(call_egl(zb::ZB_EGL_HC_eglInitialize, {display, kData, kData + 4}) == 1);
    CHECK(backend.calls().size() == 1 && backend.calls()[0].name == "eglInitialize");
    CHECK(backend.calls()[0].arguments[0] == 0x7000);
    CHECK(backend.calls()[0].arguments[1] ==
          reinterpret_cast<std::uintptr_t>(runtime.memory().base() + kData));

    // eglChooseConfig writes handles, not host pointers, into the guest array and honours size.
    backend.clear_calls();
    const std::uint32_t attribs = kData + 0x100;
    write_words(attribs, {static_cast<std::uint32_t>(kEglRedSize), 8,
                          static_cast<std::uint32_t>(kEglNone)});
    const std::uint32_t configs = kData + 0x200;
    const std::uint32_t count = kData + 0x280;
    write_words(configs, {0, 0, 0, 0});
    write_words(count, {0});
    backend.set_configs({reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000)});
    CHECK(call_egl(zb::ZB_EGL_HC_eglChooseConfig, {display, attribs, configs, 4, count}) == 1);
    CHECK(guest_word(count) == 2);
    const std::uint32_t config = guest_word(configs);
    CHECK(config != 0 && config < 0x10000);
    CHECK(egl.value_for(config) == reinterpret_cast<const void*>(0x1000));
    CHECK(guest_word(configs + 4) != 0 && guest_word(configs + 4) != config);
    // The attribute list is copied, not aliased, and terminated by EGL_NONE.
    CHECK(backend.calls().size() == 1 && backend.calls()[0].name == "eglChooseConfig");
    CHECK(backend.last_attribs().size() == 3);
    CHECK(backend.last_attribs()[0] == kEglRedSize && backend.last_attribs()[1] == 8 &&
          backend.last_attribs()[2] == kEglNone);

    // A config handle is not interchangeable with a surface handle.
    CHECK(call_egl(zb::ZB_EGL_HC_eglDestroySurface, {display, config}) == 0);
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetError, {}) == 0x300d);  // EGL_BAD_SURFACE

    // eglGetProcAddress answers with the guest stub of any gl*/egl* name the guest has (Skia and
    // Impeller resolve every GL entry point this way), and 0 otherwise.
    const std::uint32_t known_name = kData + 0x300;
    std::memcpy(runtime.memory().base() + known_name, "eglCreateContext", 17);
    const std::uint32_t unknown_name = kData + 0x320;
    std::memcpy(runtime.memory().base() + unknown_name, "eglNoSuchThing", 15);
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetProcAddress, {known_name}) == 0x9000);
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetProcAddress, {unknown_name}) == 0);

    // A GLES entry point resolves too; a name that is neither gl* nor egl* never does.
    const std::uint32_t gl_name = kData + 0x340;
    std::memcpy(runtime.memory().base() + gl_name, "glClear", 8);
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetProcAddress, {gl_name}) == 0x9000);
    const std::uint32_t other_name = kData + 0x360;
    std::memcpy(runtime.memory().base() + other_name, "system", 7);
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetProcAddress, {other_name}) == 0);

    // eglQueryString copies into guest memory and caches one copy per (display, name).
    backend.set_string(kEglVendor, "Chucny's Translator");
    const std::uint32_t vendor = call_egl(zb::ZB_EGL_HC_eglQueryString,
                                          {display, static_cast<std::uint32_t>(kEglVendor)});
    CHECK(vendor >= kData && vendor < kData + 0x1000);
    CHECK(std::strcmp(reinterpret_cast<const char*>(runtime.memory().base() + vendor),
                      "Chucny's Translator") == 0);
    CHECK(call_egl(zb::ZB_EGL_HC_eglQueryString,
                   {display, static_cast<std::uint32_t>(kEglVendor)}) == vendor);

    // A context is created, comes back as its own handle, and eglMakeCurrent resolves it.
    backend.clear_calls();
    backend.set_result("eglCreateContext", 0x3000);
    const std::uint32_t context_attribs = kData + 0x340;
    write_words(context_attribs, {static_cast<std::uint32_t>(kEglNone)});
    const std::uint32_t context =
        call_egl(zb::ZB_EGL_HC_eglCreateContext, {display, config, 0, context_attribs});
    CHECK(context != 0 && context != config);
    CHECK(egl.value_for(context) == reinterpret_cast<const void*>(0x3000));

    // eglCreateWindowSurface resolves the window handle through the injected seam.
    backend.clear_calls();
    backend.set_result("eglCreateWindowSurface", 0x4000);
    CHECK(call_egl(zb::ZB_EGL_HC_eglCreateWindowSurface, {display, config, 7, 0}) == 0);
    CHECK(backend.calls().empty());
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetError, {}) ==
          static_cast<std::uint32_t>(kEglBadNativeWindow));
    const std::uint32_t surface =
        call_egl(zb::ZB_EGL_HC_eglCreateWindowSurface, {display, config, 42, 0});
    CHECK(surface != 0);
    CHECK(backend.calls().size() == 1 && backend.calls()[0].arguments[2] == 0x5000);

    backend.clear_calls();
    backend.set_result("eglMakeCurrent", 1);
    CHECK(call_egl(zb::ZB_EGL_HC_eglMakeCurrent, {display, surface, surface, context}) == 1);
    CHECK(backend.calls().size() == 1);
    CHECK(backend.calls()[0].arguments[1] == 0x4000 && backend.calls()[0].arguments[3] == 0x3000);

    // eglGetCurrentContext maps the driver's value back to the same handle.
    backend.set_result("eglGetCurrentContext", 0x3000);
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetCurrentContext, {}) == context);

    // eglQuerySurface writes one EGLint into guest memory.
    backend.set_attribute(0x3057, 1280);  // EGL_WIDTH
    write_words(kData + 0x400, {0});
    CHECK(call_egl(zb::ZB_EGL_HC_eglQuerySurface, {display, surface, 0x3057, kData + 0x400}) == 1);
    CHECK(guest_word(kData + 0x400) == 1280);

    backend.clear_calls();
    backend.set_result("eglSwapBuffers", 1);
    CHECK(call_egl(zb::ZB_EGL_HC_eglSwapBuffers, {display, surface}) == 1);
    CHECK(backend.calls().size() == 1 && backend.calls()[0].name == "eglSwapBuffers");

    // Objects that cannot cross a 32-bit boundary are rejected, never truncated.
    backend.clear_calls();
    CHECK(call_egl(zb::ZB_EGL_HC_eglCreateSync, {display, 0x30F0, 0}) == 0);
    CHECK(backend.calls().empty());
    CHECK(call_egl(zb::ZB_EGL_HC_eglGetError, {}) == 0x3004);  // EGL_BAD_PARAMETER

    std::printf("egl_marshal_test ok\n");
    return 0;
}
