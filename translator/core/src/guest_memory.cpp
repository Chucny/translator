#include "zb/guest_memory.h"

#include <sys/mman.h>

#include <cerrno>
#include <cstring>

#include "zb/log.h"

namespace zb {

namespace {

constexpr std::uint64_t kGuardSize = 64 * 1024;
constexpr std::uint32_t kLowestAllocPage = 0x10000 >> 12;

bool range_valid(std::uint32_t addr, std::uint64_t len) {
    return len != 0 && (addr & kPageMask) == 0 && static_cast<std::uint64_t>(addr) + len <= kGuestSpaceSize;
}

std::uint8_t flags_from_prot(int prot) {
    std::uint8_t f = kPageMapped;
    if (prot & PROT_READ) f |= kPageRead;
    if (prot & PROT_WRITE) f |= kPageWrite;
    if (prot & PROT_EXEC) f |= kPageExec;
    return f;
}

}  // namespace

int host_prot(int guest_prot) {
    int p = PROT_NONE;
    if (guest_prot & (PROT_READ | PROT_EXEC)) p |= PROT_READ;
    if (guest_prot & PROT_WRITE) p |= PROT_READ | PROT_WRITE;
    return p;
}

GuestMemory::GuestMemory() : pages_(static_cast<std::size_t>(kGuestSpaceSize >> 12), 0) {
    void* p = mmap(nullptr, kGuestSpaceSize + kGuardSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        log("cannot reserve guest address space: %s", std::strerror(errno));
        return;
    }
    base_ = static_cast<std::uint8_t*>(p);
}

GuestMemory::~GuestMemory() {
    if (base_) munmap(base_, kGuestSpaceSize + kGuardSize);
}

void GuestMemory::set_flags(std::uint32_t addr, std::uint64_t len, std::uint8_t flags) {
    std::memset(pages_.data() + (addr >> 12), flags, static_cast<std::size_t>(page_round_up(len) >> 12));
}

bool GuestMemory::map_anon(std::uint32_t addr, std::uint64_t len, int prot) {
    len = page_round_up(len);
    if (!ok() || !range_valid(addr, len)) return false;
    void* p = mmap(base_ + addr, len, host_prot(prot), MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return false;
    set_flags(addr, len, flags_from_prot(prot));
    return true;
}

bool GuestMemory::map_file(std::uint32_t addr, std::uint64_t len, int prot, int share_flags, int fd, std::uint64_t offset) {
    len = page_round_up(len);
    if (!ok() || !range_valid(addr, len)) return false;
    int flags = (share_flags & (MAP_SHARED | MAP_PRIVATE)) | MAP_FIXED;
    void* p = mmap(base_ + addr, len, host_prot(prot), flags, fd, static_cast<off_t>(offset));
    if (p == MAP_FAILED) return false;
    set_flags(addr, len, flags_from_prot(prot));
    return true;
}

bool GuestMemory::protect(std::uint32_t addr, std::uint64_t len, int prot) {
    len = page_round_up(len);
    if (!ok() || !range_valid(addr, len) || !accessible(addr, len, 0)) return false;
    if (mprotect(base_ + addr, len, host_prot(prot)) != 0) return false;
    set_flags(addr, len, flags_from_prot(prot));
    return true;
}

bool GuestMemory::unmap(std::uint32_t addr, std::uint64_t len) {
    len = page_round_up(len);
    if (!ok() || !range_valid(addr, len)) return false;
    void* p = mmap(base_ + addr, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return false;
    set_flags(addr, len, 0);
    return true;
}

std::uint32_t GuestMemory::find_free(std::uint64_t len, std::uint32_t limit) const {
    const std::uint64_t pages = page_round_up(len) >> 12;
    const std::uint64_t top = limit >> 12;
    if (pages == 0 || top < kLowestAllocPage || top - kLowestAllocPage < pages) return 0;
    std::uint64_t run = 0;
    for (std::uint64_t i = top; i-- > kLowestAllocPage;) {
        if (pages_[i] == 0) {
            if (++run == pages) return static_cast<std::uint32_t>(i << 12);
        } else {
            run = 0;
        }
    }
    return 0;
}

bool GuestMemory::range_free(std::uint32_t addr, std::uint64_t len) const {
    len = page_round_up(len);
    if (!range_valid(addr, len)) return false;
    for (std::uint64_t i = addr >> 12, end = (addr + len) >> 12; i < end; ++i) {
        if (pages_[i] != 0) return false;
    }
    return true;
}

bool GuestMemory::accessible(std::uint32_t addr, std::uint64_t len, std::uint8_t need) const {
    if (len == 0) return true;
    const std::uint64_t end = static_cast<std::uint64_t>(addr) + len;
    if (end > kGuestSpaceSize) return false;
    const std::uint8_t mask = need | kPageMapped;
    for (std::uint64_t i = addr >> 12, last = (end - 1) >> 12; i <= last; ++i) {
        if ((pages_[i] & mask) != mask) return false;
    }
    return true;
}

std::uint8_t* GuestMemory::host_ptr(std::uint32_t addr, std::uint64_t len, std::uint8_t need) const {
    return accessible(addr, len, need) ? base_ + addr : nullptr;
}

}  // namespace zb
