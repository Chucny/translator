// Synthesized /proc files. The guest must see its own 32-bit address space and an AArch32 CPU,
// not the 64-bit host: bionic's pthread_getattr_np() on the main thread parses
// /proc/self/stat (startstack) and /proc/self/maps, and apps parse /proc/cpuinfo for features.
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "zb/process.h"

namespace zb {

namespace {

constexpr std::uint32_t kKuserPage = 0xFFFF0000;
// memfd_create(2) flag; the libc wrapper is missing from bionic before API 30.
constexpr unsigned kMfdCloexec = 0x0001;

bool is_self_path(std::string_view path, std::string_view leaf) {
    if (path == std::string("/proc/self/") + std::string(leaf)) return true;
    if (path == std::string("/proc/thread-self/") + std::string(leaf)) return true;
    const std::string own = "/proc/" + std::to_string(::getpid()) + "/" + std::string(leaf);
    return path == own;
}

std::string cpuinfo_text() {
    cpu_set_t set;
    CPU_ZERO(&set);
    int cpus = 1;
    if (::sched_getaffinity(0, sizeof set, &set) == 0) cpus = CPU_COUNT(&set);
    std::string text;
    for (int i = 0; i < cpus; ++i) {
        char block[512];
        std::snprintf(block, sizeof block,
                      "processor\t: %d\n"
                      "model name\t: ARMv8 Processor rev 2 (v8l)\n"
                      "BogoMIPS\t: 38.40\n"
                      "Features\t: half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm\n"
                      "CPU implementer\t: 0x51\n"
                      "CPU architecture: 8\n"
                      "CPU variant\t: 0x0\n"
                      "CPU part\t: 0x001\n"
                      "CPU revision\t: 2\n\n",
                      i);
        text += block;
    }
    text += "Hardware\t: Chucny's Translator\n";
    return text;
}

int memfd_with(const std::string& text, int flags) {
    const int fd = static_cast<int>(::syscall(SYS_memfd_create, "zbridge-proc", (flags & O_CLOEXEC) ? kMfdCloexec : 0u));
    if (fd < 0) return -errno;
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n <= 0) {
            const int err = n < 0 ? errno : EIO;
            ::close(fd);
            return -err;
        }
        done += static_cast<std::size_t>(n);
    }
    ::lseek(fd, 0, SEEK_SET);
    return fd;
}

}  // namespace

bool Process::open_synthetic_file(const char* guest_path, int flags, std::int32_t& result) {
    const std::string_view path(guest_path);
    std::string text;
    if (is_self_path(path, "maps")) {
        std::lock_guard<std::mutex> lock(mm_mutex_);
        text = build_maps();
    } else if (is_self_path(path, "stat")) {
        text = build_stat();
    } else if (path == "/proc/cpuinfo") {
        text = cpuinfo_text();
    } else {
        return false;
    }
    if ((flags & O_ACCMODE) != O_RDONLY) {
        result = -EACCES;
        return true;
    }
    result = memfd_with(text, flags);
    return true;
}

std::string Process::build_maps() const {
    std::string text;
    const auto flags_at = [&](std::uint64_t page) { return mem_.page_flags(static_cast<std::uint32_t>(page << 12)); };
    const auto name_at = [&](std::uint32_t addr, std::uint64_t& offset) -> std::string {
        offset = 0;
        if (addr >= kStackTop - kStackSize && addr < kStackTop) return "[stack]";
        if (addr == kKuserPage) return "[vectors]";
        for (const auto& m : file_mappings_) {
            if (addr >= m.start && addr - m.start < m.length) {
                if (!m.offset_is_vaddr) offset = m.offset + (addr - m.start);
                return m.path;
            }
        }
        return {};
    };

    const std::uint64_t page_count = kGuestSpaceSize >> 12;
    std::uint64_t page = 0;
    while (page < page_count) {
        const std::uint8_t flags = flags_at(page);
        if ((flags & kPageMapped) == 0) {
            ++page;
            continue;
        }
        const auto start = static_cast<std::uint32_t>(page << 12);
        std::uint64_t offset = 0;
        const std::string name = name_at(start, offset);
        std::uint64_t end_page = page + 1;
        while (end_page < page_count && flags_at(end_page) == flags) {
            std::uint64_t unused = 0;
            if (name_at(static_cast<std::uint32_t>(end_page << 12), unused) != name) break;
            ++end_page;
        }
        char line[256];
        std::snprintf(line, sizeof line, "%08x-%08llx %c%c%cp %08llx 00:00 0", start,
                      static_cast<unsigned long long>(end_page << 12), (flags & kPageRead) ? 'r' : '-',
                      (flags & kPageWrite) ? 'w' : '-', (flags & kPageExec) ? 'x' : '-',
                      static_cast<unsigned long long>(offset));
        text += line;
        if (!name.empty()) {
            text += "                    ";
            text += name;
        }
        text += '\n';
        page = end_page;
    }
    return text;
}

std::string Process::build_stat() const {
    // Fields as in proc(5); only the ones programs actually read carry real values.
    char buffer[512];
    std::snprintf(buffer, sizeof buffer,
                  "%d (zbguest) R %d %d %d 0 -1 4194304 0 0 0 0 0 0 0 0 20 0 %zu 0 0 %u 0 "
                  "4294967295 %u %u %u 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
                  static_cast<int>(::getpid()), static_cast<int>(::getppid()), static_cast<int>(::getpgrp()),
                  static_cast<int>(::getsid(0)), thread_count(), 0u /* vsize */, 0u /* startcode */,
                  0u /* endcode */, initial_sp_ /* startstack */);
    return buffer;
}

}  // namespace zb
