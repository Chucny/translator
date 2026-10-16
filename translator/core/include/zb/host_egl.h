#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "zb/egl_backend.h"
#include "zb/guest_thread.h"
#include "zb/jni_handles.h"
#include "zb/library_runtime.h"

namespace zb {

// EGL_NONE terminates every EGL attribute list.
inline constexpr EGLint kEglNone = 0x3038;
inline constexpr EGLint kEglBadNativeWindow = 0x300b;
inline constexpr EGLint kEglBadAlloc = 0x3003;
inline constexpr EGLint kEglBadAccess = 0x3002;

// The four EGL object classes that cross to the guest as 32-bit handles. The class picks the
// EGL_BAD_* error an unknown or mistyped handle is rejected with; it cannot come from the C++
// type, because EGLDisplay/EGLConfig/EGLContext/EGLSurface are all void*.
enum class EglObject : std::uint8_t { Display, Config, Context, Surface };

EGLint egl_object_error(EglObject kind);

// EGL host-call dispatcher (Phase 7a). Guest code passes AAPCS32 words in r0-r3 and on its
// stack; HostEgl translates them to the portable EglBackend seam after Dynarmic has stopped,
// exactly as HostGl does for GLES. Every EGL object crosses as a 32-bit handle: handle 0 is
// never allocated, so guest comparisons against EGL_NO_DISPLAY/EGL_NO_CONTEXT/EGL_NO_SURFACE
// (all 0) keep working.
// The last EGL host calls this process served with their results, newest last, for a crash report.
std::string egl_recent_calls();

class HostEgl {
public:
    using GuestAllocator = std::function<std::optional<std::uint32_t>(std::size_t)>;
    // eglCreateWindowSurface takes an ANativeWindow handle owned by HostNativeWindow (Task 5).
    // Until that exists, the window table is injected as this seam; the default resolver knows
    // no handle, so every window fails the call with EGL_BAD_NATIVE_WINDOW.
    using WindowResolver = std::function<const void*(std::uint32_t)>;
    // eglGetProcAddress answers with the guest trap stub of a name we generate. The default
    // resolver looks the symbol up in the guest libEGL.so through LibraryRuntime; host tests
    // inject their own, since they run no guest linker.
    using StubResolver = std::function<std::uint32_t(const std::string&)>;

    class Call {
    public:
        Call(HostEgl& host, GuestThread& thread, std::uint32_t index);

        std::uint32_t index() const { return index_; }
        std::uint32_t arg(unsigned position);
        bool valid() const { return valid_; }

        template <typename T>
        T scalar(unsigned position) {
            const std::uint32_t word = arg(position);
            if constexpr (std::is_pointer_v<T>) {
                return reinterpret_cast<T>(static_cast<std::uintptr_t>(word));
            } else if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(std::bit_cast<std::int32_t>(word));
            } else {
                return static_cast<T>(word);
            }
        }

        // Generated cases: the object class comes from (index, position); EGLDisplay/EGLConfig/
        // EGLContext/EGLSurface are the same C++ type, so T only shapes the result.
        template <typename T>
        T handle(unsigned position) {
            return reinterpret_cast<T>(const_cast<void*>(resolve_handle(position)));
        }
        const void* handle_of(unsigned position, EglObject kind);

        template <typename T>
        void set_result(T value) {
            static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
            thread_.regs()[0] = static_cast<std::uint32_t>(value);
        }

        // Host object -> guest handle. Generated cases use the (index)-derived class.
        void set_handle(const void* value);
        void set_handle(const void* value, EglObject kind);

        template <typename T>
        T* pointer(unsigned position, std::uint64_t elements, std::uint8_t need) {
            const std::uint32_t address = arg(position);
            constexpr std::uint64_t element_size = [] {
                if constexpr (std::is_void_v<std::remove_cv_t<T>>) {
                    return std::uint64_t{1};
                } else {
                    return static_cast<std::uint64_t>(sizeof(T));
                }
            }();
            if (elements > kGuestSpaceSize / element_size) {
                fail(kEglBadParameter, "array byte size exceeds the guest address space");
                return nullptr;
            }
            if (address == 0) return nullptr;
            std::uint8_t* host =
                host_.runtime().memory().host_ptr(address, elements * element_size, need);
            if (host == nullptr) {
                fail(kEglBadParameter, "array is outside accessible guest memory");
                return nullptr;
            }
            return reinterpret_cast<T*>(host);
        }

        void fail(EGLint error, const char* reason);

    private:
        const void* resolve_handle(unsigned position);

        HostEgl& host_;
        GuestThread& thread_;
        std::uint32_t index_;
        std::uint32_t regs_[4];
        bool valid_ = true;
    };

    HostEgl(LibraryRuntime& runtime, EglBackend& backend, GuestAllocator allocator = {},
            WindowResolver windows = {}, StubResolver stubs = {})
        : runtime_(runtime), backend_(backend), allocator_(std::move(allocator)),
          windows_(std::move(windows)), stubs_(std::move(stubs)) {}
    HostEgl(const HostEgl&) = delete;
    HostEgl& operator=(const HostEgl&) = delete;

    // Serves the EGL range 168-211 and returns false for every other index.
    bool handle_host_call(std::uint32_t index, GuestThread& thread);
    LibraryRuntime& runtime() { return runtime_; }
    EglBackend& backend() { return backend_; }
    void reject(Call& call, EGLint error, const char* reason);

    // 0 for nullptr (EGL_NO_*), a stable handle otherwise. The same host pointer always maps to
    // the same handle, so guest equality comparisons behave like a real driver's.
    std::uint32_t handle_for(const void* value) { return handle_for(value, EglObject::Surface); }
    std::uint32_t handle_for(const void* value, EglObject kind);
    const void* value_for(std::uint32_t handle) const;
    // nullopt when the handle is unknown or holds an object of another class.
    std::optional<const void*> lookup(std::uint32_t handle, EglObject kind) const;

    const void* window_for(std::uint32_t handle) const {
        return windows_ ? windows_(handle) : nullptr;
    }
    std::uint32_t stub_address(const std::string& name);
    std::optional<std::uint32_t> allocate_guest(std::size_t size);

private:
    bool dispatch(Call& call);  // core/src/gen/egl_dispatch.inc

    LibraryRuntime& runtime_;
    EglBackend& backend_;
    GuestAllocator allocator_;
    WindowResolver windows_;
    StubResolver stubs_;
    GlobalHandles objects_{HandleKind::Global};
    mutable std::mutex mutex_;
    std::unordered_map<const void*, std::uint32_t> by_value_;
    std::unordered_map<std::uint32_t, EglObject> kinds_;
    std::uint32_t libegl_ = 0;
    std::uint32_t libgles_ = 0;
    bool libegl_tried_ = false;
};

}  // namespace zb
