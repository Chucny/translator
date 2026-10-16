#include "zb/elf_fixups.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace zb {

namespace {

constexpr std::uint32_t kMaxDynamicSize = 64 * 1024;
constexpr std::uint64_t kMaxFileSize = 512ULL * 1024 * 1024;

class ScopedFd {
public:
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() {
        if (fd_ >= 0) ::close(fd_);
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    int get() const { return fd_; }

private:
    int fd_;
};

bool read_exact(int fd, void* buf, std::size_t len, off_t offset) {
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::pread(fd, static_cast<char*>(buf) + done, len - done, offset + static_cast<off_t>(done));
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool write_exact(int fd, const void* buf, std::size_t len, off_t offset) {
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::pwrite(fd, static_cast<const char*>(buf) + done, len - done,
                                   offset + static_cast<off_t>(done));
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool range_fits(std::uint64_t offset, std::uint64_t size, std::size_t file_size) {
    return offset <= file_size && size <= static_cast<std::uint64_t>(file_size) - offset;
}

template <typename T>
T read_object(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

template <typename T>
void write_object(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

ElfFixupReport skipped(std::string message) {
    return {ElfFixupStatus::Skipped, {}, std::move(message)};
}

ElfFixupReport error(std::string message) {
    return {ElfFixupStatus::Error, {}, std::move(message)};
}

struct DynamicTable {
    std::size_t offset;
    std::size_t slots;
    std::size_t terminator;
};

bool vaddr_to_offset(const std::vector<Elf32_Phdr>& loads, std::uint32_t address,
                     std::size_t file_size, std::size_t* offset, std::size_t* available) {
    for (const auto& load : loads) {
        const std::uint64_t begin = load.p_vaddr;
        const std::uint64_t end = begin + load.p_filesz;
        if (address < begin || address >= end) continue;
        const std::uint64_t result = load.p_offset + (static_cast<std::uint64_t>(address) - begin);
        if (!range_fits(result, 1, file_size)) return false;
        *offset = static_cast<std::size_t>(result);
        *available = static_cast<std::size_t>(end - address);
        return true;
    }
    return false;
}

}  // namespace

ElfFixupReport fix_guest_library(const std::string& path) {
    const int raw_fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (raw_fd < 0) return error("open failed: " + std::string(std::strerror(errno)));
    ScopedFd fd(raw_fd);

    struct stat st {};
    if (::fstat(fd.get(), &st) != 0) {
        return error("stat failed: " + std::string(std::strerror(errno)));
    }
    if (st.st_size < 0 || static_cast<std::uint64_t>(st.st_size) > kMaxFileSize) {
        return error("file size is outside the supported range");
    }
    const auto file_size = static_cast<std::size_t>(st.st_size);
    std::vector<std::uint8_t> bytes(file_size);
    if (file_size != 0 && !read_exact(fd.get(), bytes.data(), bytes.size(), 0)) {
        return error("read failed: " + std::string(std::strerror(errno)));
    }

    if (file_size < SELFMAG || std::memcmp(bytes.data(), ELFMAG, SELFMAG) != 0) {
        return skipped("not an ELF file");
    }
    if (file_size < sizeof(Elf32_Ehdr)) return error("truncated ELF header");

    const Elf32_Ehdr eh = read_object<Elf32_Ehdr>(bytes, 0);
    if (eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        return skipped("not a little-endian ELF32 file");
    }
    if (eh.e_machine != EM_ARM) return skipped("not an ARM ELF file");
    if (eh.e_ident[EI_VERSION] != EV_CURRENT || eh.e_version != EV_CURRENT ||
        eh.e_phentsize != sizeof(Elf32_Phdr) || eh.e_phnum == 0 || eh.e_phnum > 1024) {
        return error("invalid ELF header");
    }

    const std::uint64_t phdr_size = static_cast<std::uint64_t>(eh.e_phnum) * sizeof(Elf32_Phdr);
    if (!range_fits(eh.e_phoff, phdr_size, file_size)) return error("truncated program headers");

    std::vector<Elf32_Phdr> loads;
    DynamicTable dynamic{};
    bool has_dynamic = false;
    for (std::size_t i = 0; i < eh.e_phnum; ++i) {
        const std::size_t offset = eh.e_phoff + i * sizeof(Elf32_Phdr);
        const Elf32_Phdr phdr = read_object<Elf32_Phdr>(bytes, offset);
        if (phdr.p_type == PT_LOAD) {
            if (!range_fits(phdr.p_offset, phdr.p_filesz, file_size)) {
                return error("PT_LOAD exceeds the file");
            }
            loads.push_back(phdr);
        } else if (phdr.p_type == PT_DYNAMIC) {
            if (has_dynamic) return error("multiple PT_DYNAMIC segments");
            if (phdr.p_filesz == 0 || phdr.p_filesz > kMaxDynamicSize ||
                phdr.p_filesz % sizeof(Elf32_Dyn) != 0 ||
                !range_fits(phdr.p_offset, phdr.p_filesz, file_size)) {
                return error("invalid PT_DYNAMIC segment");
            }
            has_dynamic = true;
            dynamic.offset = phdr.p_offset;
            dynamic.slots = phdr.p_filesz / sizeof(Elf32_Dyn);
        }
    }
    if (!has_dynamic) return skipped("no PT_DYNAMIC segment");

    bool found_terminator = false;
    for (std::size_t i = 0; i < dynamic.slots; ++i) {
        const auto entry = read_object<Elf32_Dyn>(bytes, dynamic.offset + i * sizeof(Elf32_Dyn));
        if (entry.d_tag == DT_NULL) {
            dynamic.terminator = i;
            found_terminator = true;
            break;
        }
    }
    if (!found_terminator) return error("unterminated dynamic table");

    bool has_needed = false;
    bool has_strtab = false;
    std::uint32_t strtab_address = 0;
    bool has_strtab_size = false;
    std::uint32_t declared_strtab_size = 0;
    for (std::size_t i = 0; i < dynamic.terminator; ++i) {
        const auto entry = read_object<Elf32_Dyn>(bytes, dynamic.offset + i * sizeof(Elf32_Dyn));
        has_needed |= entry.d_tag == DT_NEEDED;
        if (entry.d_tag == DT_STRTAB) {
            has_strtab = true;
            strtab_address = static_cast<std::uint32_t>(entry.d_un.d_ptr);
        } else if (entry.d_tag == DT_STRSZ) {
            has_strtab_size = true;
            declared_strtab_size = static_cast<std::uint32_t>(entry.d_un.d_val);
        }
    }

    std::size_t strtab_offset = 0;
    std::size_t strtab_size = 0;
    if (has_needed) {
        if (!has_strtab || !vaddr_to_offset(loads, strtab_address, file_size, &strtab_offset, &strtab_size)) {
            return error("DT_STRTAB is not file-backed");
        }
        if (has_strtab_size) {
            if (declared_strtab_size == 0 || declared_strtab_size > strtab_size) {
                return error("DT_STRSZ exceeds its file-backed segment");
            }
            strtab_size = declared_strtab_size;
        }
    }

    std::vector<std::size_t> changed_entries;
    std::vector<std::string> changes;
    bool textrel_changed = false;
    bool marker_after = false;

    for (std::size_t i = 0; i < dynamic.terminator; ++i) {
        const std::size_t entry_offset = dynamic.offset + i * sizeof(Elf32_Dyn);
        auto entry = read_object<Elf32_Dyn>(bytes, entry_offset);
        if (entry.d_tag == DT_NEEDED) {
            const auto string_index = static_cast<std::uint32_t>(entry.d_un.d_val);
            if (string_index >= strtab_size) return error("DT_NEEDED string is outside DT_STRTAB");
            const std::uint64_t name_offset64 = static_cast<std::uint64_t>(strtab_offset) + string_index;
            if (!range_fits(name_offset64, 1, file_size)) return error("DT_NEEDED string is outside the file");
            const std::size_t name_offset = static_cast<std::size_t>(name_offset64);
            const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(name_offset);
            const auto table_end = bytes.begin() + static_cast<std::ptrdiff_t>(strtab_offset + strtab_size);
            const auto nul = std::find(first, table_end, 0);
            if (nul == table_end) return error("unterminated DT_NEEDED string");
            const std::string name(first, nul);
            const std::size_t cut = name.find_last_of("/\\");
            if (cut != std::string::npos) {
                const std::uint64_t new_value = static_cast<std::uint64_t>(string_index) + cut + 1;
                if (new_value > std::numeric_limits<std::uint32_t>::max()) {
                    return error("DT_NEEDED basename offset overflows ELF32");
                }
                entry.d_un.d_val = static_cast<std::uint32_t>(new_value);
                write_object(bytes, entry_offset, entry);
                changed_entries.push_back(entry_offset);
                changes.push_back("DT_NEEDED " + name + " -> " + name.substr(cut + 1));
            }
        } else if (entry.d_tag == DT_TEXTREL) {
            entry.d_tag = kDtZbTextrel;
            entry.d_un.d_val = 1;
            write_object(bytes, entry_offset, entry);
            changed_entries.push_back(entry_offset);
            textrel_changed = true;
            marker_after = true;
        } else if (entry.d_tag == DT_FLAGS && (entry.d_un.d_val & DF_TEXTREL) != 0) {
            entry.d_un.d_val &= ~static_cast<Elf32_Word>(DF_TEXTREL);
            write_object(bytes, entry_offset, entry);
            changed_entries.push_back(entry_offset);
            textrel_changed = true;
        } else if (entry.d_tag == kDtZbTextrel) {
            marker_after = true;
        }
    }

    if (textrel_changed && !marker_after) {
        if (dynamic.terminator + 1 >= dynamic.slots) {
            return error("DF_TEXTREL has no spare dynamic slot for the marker");
        }
        const std::size_t marker_offset = dynamic.offset + dynamic.terminator * sizeof(Elf32_Dyn);
        const std::size_t null_offset = marker_offset + sizeof(Elf32_Dyn);
        Elf32_Dyn marker{};
        marker.d_tag = kDtZbTextrel;
        marker.d_un.d_val = 1;
        Elf32_Dyn terminator{};
        terminator.d_tag = DT_NULL;
        write_object(bytes, marker_offset, marker);
        write_object(bytes, null_offset, terminator);
        changed_entries.push_back(marker_offset);
        changed_entries.push_back(null_offset);
        marker_after = true;
    }

    if (textrel_changed) changes.push_back("DT_TEXTREL -> DT_ZB_TEXTREL");
    if (changed_entries.empty()) return {ElfFixupStatus::Unchanged, {}, "unchanged"};

    for (const std::size_t offset : changed_entries) {
        if (!write_exact(fd.get(), bytes.data() + offset, sizeof(Elf32_Dyn), offset)) {
            return error("write failed: " + std::string(std::strerror(errno)));
        }
    }
    if (::fsync(fd.get()) != 0) return error("fsync failed: " + std::string(std::strerror(errno)));
    return {ElfFixupStatus::Changed, std::move(changes), "changed"};
}

bool elf_has_textrel_marker(int fd) {
    Elf32_Ehdr eh;
    if (!read_exact(fd, &eh, sizeof eh, 0)) return false;
    if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS32 ||
        eh.e_phentsize != sizeof(Elf32_Phdr) || eh.e_phnum == 0 || eh.e_phnum > 64) {
        return false;
    }
    std::vector<Elf32_Phdr> phdrs(eh.e_phnum);
    if (!read_exact(fd, phdrs.data(), phdrs.size() * sizeof(Elf32_Phdr), eh.e_phoff)) return false;

    for (const auto& p : phdrs) {
        if (p.p_type != PT_DYNAMIC || p.p_filesz > kMaxDynamicSize) continue;
        std::vector<Elf32_Dyn> dyn(p.p_filesz / sizeof(Elf32_Dyn));
        if (!read_exact(fd, dyn.data(), dyn.size() * sizeof(Elf32_Dyn), p.p_offset)) return false;
        for (const auto& d : dyn) {
            if (d.d_tag == DT_NULL) break;
            if (d.d_tag == kDtZbTextrel) return true;
        }
    }
    return false;
}

}  // namespace zb
