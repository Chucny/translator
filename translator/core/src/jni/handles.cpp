#include "zb/jni_handles.h"

#include <cstdlib>

#include "zb/log.h"

namespace zb {

namespace {

constexpr std::uint32_t kMaxSlots = 1u << 24;

}  // namespace

std::optional<HandleKind> handle_kind(std::uint32_t handle) {
    const std::uint32_t bits = handle & 3u;
    if (bits == 0) return std::nullopt;
    return static_cast<HandleKind>(bits);
}

void LocalHandles::push_frame() {
    frame_starts_.push_back(refs_.size());
}

std::vector<std::uint64_t> LocalHandles::pop_frame() {
    std::vector<std::uint64_t> released;
    if (frame_starts_.empty()) return released;
    const std::size_t start = frame_starts_.back();
    frame_starts_.pop_back();
    for (std::size_t i = start; i < refs_.size(); ++i) {
        if (refs_[i] != 0) released.push_back(refs_[i]);
        serials_[i] = static_cast<std::uint8_t>((serials_[i] + 1) & 63u);
    }
    refs_.resize(start);
    return released;
}

std::uint32_t LocalHandles::add(std::uint64_t host_ref) {
    if (host_ref == 0) return 0;
    if (frame_starts_.empty()) push_frame();
    const std::size_t index = refs_.size();
    if (index >= kMaxSlots) {
        log("too many JNI local references");
        std::abort();
    }
    std::uint32_t serial;
    if (index < serials_.size()) {
        serial = serials_[index];
    } else {
        serials_.push_back(0);
        serial = 0;
    }
    refs_.push_back(host_ref);
    return make_handle(HandleKind::Local, static_cast<std::uint32_t>(index), serial);
}

std::optional<std::uint64_t> LocalHandles::get(std::uint32_t handle) const {
    if (handle == 0) return 0;
    if (handle_kind(handle) != HandleKind::Local) return std::nullopt;
    const std::uint32_t index = handle_index(handle);
    if (index >= refs_.size() || refs_[index] == 0) return std::nullopt;
    if (handle_serial(handle) != serials_[index]) return std::nullopt;
    return refs_[index];
}

std::optional<std::uint64_t> LocalHandles::remove(std::uint32_t handle) {
    if (handle == 0) return 0;
    const std::optional<std::uint64_t> ref = get(handle);
    if (!ref) return std::nullopt;
    const std::uint32_t index = handle_index(handle);
    refs_[index] = 0;
    serials_[index] = static_cast<std::uint8_t>((serials_[index] + 1) & 63u);
    const std::size_t floor = frame_starts_.empty() ? 0 : frame_starts_.back();
    while (refs_.size() > floor && refs_.back() == 0) refs_.pop_back();
    return ref;
}

std::uint32_t GlobalHandles::add(std::uint64_t host_ref) {
    if (host_ref == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t index;
    std::uint32_t serial;
    if (!free_.empty()) {
        index = free_.back();
        free_.pop_back();
        refs_[index] = host_ref;
        serial = serials_[index];
    } else {
        if (refs_.size() >= kMaxSlots) {
            log("too many JNI global references");
            std::abort();
        }
        refs_.push_back(host_ref);
        serials_.push_back(0);
        index = static_cast<std::uint32_t>(refs_.size() - 1);
        serial = 0;
    }
    return make_handle(kind_, index, serial);
}

std::optional<std::uint64_t> GlobalHandles::get(std::uint32_t handle) const {
    if (handle == 0) return 0;
    if (handle_kind(handle) != kind_) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t index = handle_index(handle);
    if (index >= refs_.size() || refs_[index] == 0) return std::nullopt;
    if (handle_serial(handle) != serials_[index]) return std::nullopt;
    return refs_[index];
}

std::optional<std::uint64_t> GlobalHandles::remove(std::uint32_t handle) {
    if (handle == 0) return 0;
    if (handle_kind(handle) != kind_) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t index = handle_index(handle);
    if (index >= refs_.size() || refs_[index] == 0) return std::nullopt;
    if (handle_serial(handle) != serials_[index]) return std::nullopt;
    const std::uint64_t ref = refs_[index];
    refs_[index] = 0;
    serials_[index] = static_cast<std::uint8_t>((serials_[index] + 1) & 63u);
    free_.push_back(index);
    return ref;
}

std::uint32_t IdTable::intern(std::uint64_t host_id) {
    if (host_id == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = index_.find(host_id);
    if (it != index_.end()) return it->second;
    ids_.push_back(host_id);
    const auto id = static_cast<std::uint32_t>(ids_.size());
    index_.emplace(host_id, id);
    return id;
}

std::optional<std::uint64_t> IdTable::get(std::uint32_t id) const {
    if (id == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (id > ids_.size()) return std::nullopt;
    return ids_[id - 1];
}

}  // namespace zb
