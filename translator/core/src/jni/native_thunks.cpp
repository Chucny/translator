#include "zb/native_thunks.h"

#include <cstddef>
#include <cstdlib>
#include <utility>

#include "zb/log.h"

extern "C" {
// Both defined in or called from thunks.S.
extern const char zb_native_thunk_base[];
extern const char zb_native_thunk_end[];
void zb_native_dispatch(std::uint32_t slot, zb::NativeRegs* regs) noexcept;
}

namespace zb {

namespace {

std::atomic<NativeDispatcher> g_dispatcher{nullptr};

}  // namespace

// thunks.S saves the registers with exactly this layout.
static_assert(offsetof(NativeRegs, x) == 0);
static_assert(offsetof(NativeRegs, d) == 64);
static_assert(offsetof(NativeRegs, stack) == 128);
static_assert(sizeof(NativeRegs) == 144);

void set_native_dispatcher(NativeDispatcher dispatcher) {
    g_dispatcher.store(dispatcher);
}

void* native_thunk_address(std::uint32_t slot) {
    if (slot >= kNativeThunkCount) return nullptr;
    return const_cast<char*>(zb_native_thunk_base) + 8 * static_cast<std::size_t>(slot);
}

std::size_t native_thunk_pool_bytes() {
    return static_cast<std::size_t>(zb_native_thunk_end - zb_native_thunk_base);
}

NativeSlots::NativeSlots(std::size_t capacity)
    : capacity_(capacity < kNativeThunkCount ? capacity : kNativeThunkCount),
      targets_(new NativeTarget[capacity_]),
      ready_(new std::atomic<bool>[capacity_]) {
    for (std::size_t i = 0; i < capacity_; ++i) ready_[i].store(false, std::memory_order_relaxed);
}

std::int32_t NativeSlots::allocate(NativeTarget target) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!released_.empty()) {
        const std::uint32_t reused = released_.back();
        released_.pop_back();
        targets_[reused] = std::move(target);
        ready_[reused].store(true, std::memory_order_release);
        return static_cast<std::int32_t>(reused);
    }
    const std::uint32_t slot = count_.load();
    if (slot >= capacity_) return -1;
    targets_[slot] = std::move(target);
    ready_[slot].store(true, std::memory_order_release);
    count_.store(slot + 1, std::memory_order_release);  // published only after the entry is complete
    return static_cast<std::int32_t>(slot);
}

void NativeSlots::release(std::uint32_t slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot >= count_.load(std::memory_order_acquire) ||
        !ready_[slot].load(std::memory_order_acquire)) {
        log("NativeSlots::release: slot %u was never allocated", slot);
        std::abort();
    }
    ready_[slot].store(false, std::memory_order_release);
    released_.push_back(slot);
}

const NativeTarget* NativeSlots::target(std::uint32_t slot) const {
    if (slot >= count_.load(std::memory_order_acquire) ||
        !ready_[slot].load(std::memory_order_acquire)) {
        return nullptr;
    }
    return &targets_[slot];
}

}  // namespace zb

void zb_native_dispatch(std::uint32_t slot, zb::NativeRegs* regs) noexcept {
    const zb::NativeDispatcher dispatcher = zb::g_dispatcher.load();
    if (dispatcher == nullptr) {
        zb::log("native thunk slot %u called with no dispatcher", slot);
        std::abort();
    }
    try {
        dispatcher(slot, *regs);
    } catch (...) {
        zb::log("native thunk slot %u: dispatcher threw an exception", slot);
        std::abort();
    }
}
