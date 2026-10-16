#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "zb/native_call.h"

namespace zb {

struct NativeCallCounter;  // core/include/zb/runtime_report.h

// Number of precompiled thunks; must match the .rept count in core/src/jni/thunks.S.
constexpr std::size_t kNativeThunkCount = 16384;

// What a registered native method runs: a guest function and how to marshal its arguments.
struct NativeTarget {
    std::uint32_t guest_function = 0;  // Thumb bit included
    std::string shorty;                // return type first
    bool is_static = false;
    std::string label;                 // "Class.method" (or just "method") for the report
    // Cached at registration (HostJni::register_native); every dispatch increments it with one
    // relaxed atomic add, no lock. Never null once the slot is bound.
    NativeCallCounter* counter = nullptr;
};

// Receives every thunk call with the slot number. It reads the arguments from regs and stores
// the result in regs.x[0] / regs.d[0].
using NativeDispatcher = void (*)(std::uint32_t slot, NativeRegs& regs);
void set_native_dispatcher(NativeDispatcher dispatcher);

// Address of a thunk, usable as the fnPtr of RegisterNatives; nullptr when out of range.
void* native_thunk_address(std::uint32_t slot);

// Size in bytes of the precompiled thunk pool (zb_native_thunk_end - zb_native_thunk_base),
// for sanity-checking that the pool matches kNativeThunkCount.
std::size_t native_thunk_pool_bytes();

// Assigns thunk slots to native methods. A slot the backend has bound is never freed
// (UnregisterNatives keeps it), so target() needs no lock; only a slot whose registration failed
// is released for reuse, and Java can never have reached it.
class NativeSlots {
public:
    // capacity is capped at kNativeThunkCount.
    explicit NativeSlots(std::size_t capacity = kNativeThunkCount);
    // The slot number, or -1 when every slot is taken.
    std::int32_t allocate(NativeTarget target);
    // Returns a slot whose registration failed (never bound by the backend) for reuse.
    void release(std::uint32_t slot);
    // The target of an allocated slot, or nullptr.
    const NativeTarget* target(std::uint32_t slot) const;

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::unique_ptr<NativeTarget[]> targets_;
    std::unique_ptr<std::atomic<bool>[]> ready_;
    std::atomic<std::uint32_t> count_{0};
    std::vector<std::uint32_t> released_;
};

}  // namespace zb
