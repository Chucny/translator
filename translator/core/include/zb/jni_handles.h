#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace zb {

// Guest-visible JNI references are 32-bit handles: 0 is null. Bits 0-1 give the kind, bits 2-7
// are a serial that increments (mod 64) every time the slot is reused, and bits 8-31 index a
// table of host references (up to 16,777,215 slots), stored as opaque 64-bit values. The serial
// lets get()/remove() detect a handle from a slot that has since been freed and reused: a stale
// handle carries the old serial and no longer matches the slot's current one.
enum class HandleKind : std::uint32_t { Local = 1, Global = 2, WeakGlobal = 3 };

constexpr std::uint32_t make_handle(HandleKind kind, std::uint32_t slot, std::uint32_t serial = 0) {
    return (slot << 8) | ((serial & 63u) << 2) | static_cast<std::uint32_t>(kind);
}

constexpr std::uint32_t handle_index(std::uint32_t handle) {
    return handle >> 8;
}

constexpr std::uint32_t handle_serial(std::uint32_t handle) {
    return (handle >> 2) & 63u;
}

// nullopt for the null handle and for kind bits 00.
std::optional<HandleKind> handle_kind(std::uint32_t handle);

// Local references of one host thread: a stack of frames (the native call frame plus
// PushLocalFrame frames). Popping a frame invalidates its handles. Not thread-safe: one
// instance per host thread.
class LocalHandles {
public:
    void push_frame();
    // Pops the top frame and returns the host references it still held, so the caller can
    // release them on the host. Returns an empty list when there is no frame.
    std::vector<std::uint64_t> pop_frame();
    // Adds a host reference to the top frame, creating a base frame if needed. 0 for null.
    std::uint32_t add(std::uint64_t host_ref);
    // The host reference; 0 for the null handle; nullopt for an invalid or deleted handle.
    std::optional<std::uint64_t> get(std::uint32_t handle) const;
    // DeleteLocalRef: frees the slot and returns the host reference; 0 for the null handle;
    // nullopt for an invalid handle.
    std::optional<std::uint64_t> remove(std::uint32_t handle);
    std::size_t frame_count() const { return frame_starts_.size(); }

private:
    std::vector<std::uint64_t> refs_;      // 0 marks a deleted slot
    std::vector<std::uint8_t> serials_;    // never shrinks; keeps every slot's current serial
    std::vector<std::size_t> frame_starts_;
};

// Global or weak global references of the process. Thread-safe; freed slots are reused.
class GlobalHandles {
public:
    explicit GlobalHandles(HandleKind kind) : kind_(kind) {}
    std::uint32_t add(std::uint64_t host_ref);
    std::optional<std::uint64_t> get(std::uint32_t handle) const;
    std::optional<std::uint64_t> remove(std::uint32_t handle);

private:
    HandleKind kind_;
    mutable std::mutex mutex_;
    std::vector<std::uint64_t> refs_;
    std::vector<std::uint8_t> serials_;
    std::vector<std::uint32_t> free_;
};

// jmethodID / jfieldID values: append-only and deduplicated, since host ids stay valid for the
// process lifetime. Guest ids start at 1; 0 is null. Thread-safe.
class IdTable {
public:
    std::uint32_t intern(std::uint64_t host_id);
    std::optional<std::uint64_t> get(std::uint32_t id) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::uint64_t> ids_;
    std::unordered_map<std::uint64_t, std::uint32_t> index_;
};

}  // namespace zb
