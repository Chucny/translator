#include "zb/elf_symbols.h"

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
#include <unordered_set>
#include <utility>
#include <vector>

#include "zb/jni_mangle.h"

namespace zb {

namespace {

constexpr std::uint64_t kMaxFileSize = 512ULL * 1024 * 1024;
constexpr std::uint32_t kMaxDynamicSize = 64 * 1024;
constexpr std::uint32_t kMaxSymbols = 1U << 20;

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

struct FileRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

bool range_fits(std::uint64_t offset, std::uint64_t size, std::size_t total) {
    return offset <= total && size <= static_cast<std::uint64_t>(total) - offset;
}

bool read_exact(int fd, void* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        const ssize_t n = ::read(fd, static_cast<char*>(data) + done, size - done);
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

template <typename T>
T object_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

ElfSymbolReport skipped(std::string message) {
    return {ElfSymbolStatus::Skipped, {}, std::move(message)};
}

ElfSymbolReport error(std::string message) {
    return {ElfSymbolStatus::Error, {}, std::move(message)};
}

bool map_address(const std::vector<Elf32_Phdr>& loads, std::uint32_t address,
                 std::size_t file_size, FileRange* range) {
    for (const auto& load : loads) {
        const std::uint64_t begin = load.p_vaddr;
        const std::uint64_t end = begin + load.p_filesz;
        if (address < begin || address >= end) continue;
        const std::uint64_t offset = load.p_offset + (static_cast<std::uint64_t>(address) - begin);
        const std::uint64_t available = end - address;
        if (!range_fits(offset, available, file_size)) return false;
        range->offset = static_cast<std::size_t>(offset);
        range->size = static_cast<std::size_t>(available);
        return true;
    }
    return false;
}

bool word_at(const std::vector<std::uint8_t>& bytes, const FileRange& range,
             std::uint64_t word, std::uint32_t* value) {
    const std::uint64_t relative = word * sizeof(std::uint32_t);
    if (!range_fits(relative, sizeof(std::uint32_t), range.size)) return false;
    *value = object_at<std::uint32_t>(bytes, range.offset + static_cast<std::size_t>(relative));
    return true;
}

bool sysv_symbol_count(const std::vector<std::uint8_t>& bytes, const FileRange& hash,
                       std::uint32_t* count, std::string* message) {
    std::uint32_t buckets = 0;
    std::uint32_t chains = 0;
    if (!word_at(bytes, hash, 0, &buckets) || !word_at(bytes, hash, 1, &chains)) {
        *message = "truncated DT_HASH header";
        return false;
    }
    if (chains > kMaxSymbols) {
        *message = "DT_HASH symbol count exceeds the limit";
        return false;
    }
    const std::uint64_t words = 2ULL + buckets + chains;
    if (words > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t) ||
        !range_fits(0, words * sizeof(std::uint32_t), hash.size)) {
        *message = "truncated DT_HASH table";
        return false;
    }
    *count = chains;
    return true;
}

bool gnu_symbol_count(const std::vector<std::uint8_t>& bytes, const FileRange& hash,
                      std::uint32_t* count, std::string* message) {
    std::uint32_t buckets = 0;
    std::uint32_t symbol_offset = 0;
    std::uint32_t bloom_size = 0;
    std::uint32_t ignored_shift = 0;
    if (!word_at(bytes, hash, 0, &buckets) || !word_at(bytes, hash, 1, &symbol_offset) ||
        !word_at(bytes, hash, 2, &bloom_size) || !word_at(bytes, hash, 3, &ignored_shift)) {
        *message = "truncated DT_GNU_HASH header";
        return false;
    }
    if (buckets > kMaxSymbols || symbol_offset > kMaxSymbols || bloom_size == 0 || bloom_size > kMaxSymbols) {
        *message = "invalid DT_GNU_HASH dimensions";
        return false;
    }
    const std::uint64_t bucket_word = 4ULL + bloom_size;
    const std::uint64_t chain_word = bucket_word + buckets;
    if (chain_word > hash.size / sizeof(std::uint32_t)) {
        *message = "truncated DT_GNU_HASH buckets";
        return false;
    }

    // Validate every bucket, but walk only the chain that starts at the highest symbol index.
    // Chains are consecutive runs of the symbol table, so that chain ends at the last symbol.
    // Walking each bucket's chain separately would let a crafted table (many buckets sharing one
    // long chain) cost buckets * chain steps on untrusted APK input; this is buckets + chain.
    std::uint32_t start = 0;
    for (std::uint32_t i = 0; i < buckets; ++i) {
        std::uint32_t symbol = 0;
        if (!word_at(bytes, hash, bucket_word + i, &symbol)) {
            *message = "truncated DT_GNU_HASH bucket";
            return false;
        }
        if (symbol == 0) continue;
        if (symbol < symbol_offset || symbol >= kMaxSymbols) {
            *message = "invalid DT_GNU_HASH bucket";
            return false;
        }
        start = std::max(start, symbol);
    }
    if (start == 0) {
        *count = symbol_offset;
        return true;
    }
    for (std::uint32_t symbol = start;;) {
        const std::uint64_t chain_index = chain_word + (symbol - symbol_offset);
        std::uint32_t chain = 0;
        if (!word_at(bytes, hash, chain_index, &chain)) {
            *message = "unterminated DT_GNU_HASH chain";
            return false;
        }
        if ((chain & 1U) != 0) {
            *count = symbol + 1;
            return true;
        }
        if (++symbol >= kMaxSymbols) {
            *message = "DT_GNU_HASH chain exceeds the symbol limit";
            return false;
        }
    }
}

}  // namespace

ElfSymbolReport scan_elf32_jni_exports(const std::string& path) {
    const int raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (raw_fd < 0) return error("open failed: " + std::string(std::strerror(errno)));
    ScopedFd fd(raw_fd);
    struct stat st {};
    if (::fstat(fd.get(), &st) != 0) return error("stat failed: " + std::string(std::strerror(errno)));
    if (st.st_size < 0 || static_cast<std::uint64_t>(st.st_size) > kMaxFileSize) {
        return error("file size is outside the supported range");
    }
    const auto file_size = static_cast<std::size_t>(st.st_size);
    std::vector<std::uint8_t> bytes(file_size);
    if (!bytes.empty() && !read_exact(fd.get(), bytes.data(), bytes.size())) {
        return error("read failed: " + std::string(std::strerror(errno)));
    }
    if (file_size < SELFMAG || std::memcmp(bytes.data(), ELFMAG, SELFMAG) != 0) {
        return skipped("not an ELF file");
    }
    if (file_size < sizeof(Elf32_Ehdr)) return error("truncated ELF header");

    const Elf32_Ehdr eh = object_at<Elf32_Ehdr>(bytes, 0);
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
    FileRange dynamic;
    bool has_dynamic = false;
    for (std::size_t i = 0; i < eh.e_phnum; ++i) {
        const auto phdr = object_at<Elf32_Phdr>(bytes, eh.e_phoff + i * sizeof(Elf32_Phdr));
        if (phdr.p_type == PT_LOAD) {
            if (!range_fits(phdr.p_offset, phdr.p_filesz, file_size)) return error("PT_LOAD exceeds the file");
            loads.push_back(phdr);
        } else if (phdr.p_type == PT_DYNAMIC) {
            if (has_dynamic) return error("multiple PT_DYNAMIC segments");
            if (phdr.p_filesz == 0 || phdr.p_filesz > kMaxDynamicSize ||
                phdr.p_filesz % sizeof(Elf32_Dyn) != 0 ||
                !range_fits(phdr.p_offset, phdr.p_filesz, file_size)) {
                return error("invalid PT_DYNAMIC segment");
            }
            has_dynamic = true;
            dynamic = {phdr.p_offset, phdr.p_filesz};
        }
    }
    if (!has_dynamic) return skipped("no PT_DYNAMIC segment");

    bool terminated = false;
    bool has_strtab = false;
    bool has_strsz = false;
    bool has_symtab = false;
    bool has_syment = false;
    bool has_sysv_hash = false;
    bool has_gnu_hash = false;
    std::uint32_t strtab_address = 0;
    std::uint32_t strtab_size = 0;
    std::uint32_t symtab_address = 0;
    std::uint32_t symbol_size = 0;
    std::uint32_t sysv_hash_address = 0;
    std::uint32_t gnu_hash_address = 0;
    for (std::size_t offset = 0; offset < dynamic.size; offset += sizeof(Elf32_Dyn)) {
        const auto entry = object_at<Elf32_Dyn>(bytes, dynamic.offset + offset);
        const auto value = static_cast<std::uint32_t>(entry.d_un.d_val);
        if (entry.d_tag == DT_NULL) {
            terminated = true;
            break;
        }
        switch (entry.d_tag) {
        case DT_STRTAB: has_strtab = true; strtab_address = value; break;
        case DT_STRSZ: has_strsz = true; strtab_size = value; break;
        case DT_SYMTAB: has_symtab = true; symtab_address = value; break;
        case DT_SYMENT: has_syment = true; symbol_size = value; break;
        case DT_HASH: has_sysv_hash = true; sysv_hash_address = value; break;
        case DT_GNU_HASH: has_gnu_hash = true; gnu_hash_address = value; break;
        default: break;
        }
    }
    if (!terminated) return error("unterminated dynamic table");
    if (!has_symtab) return {ElfSymbolStatus::Ok, {}, "no dynamic symbol table"};
    if (!has_strtab || !has_strsz || strtab_size == 0 || !has_syment || symbol_size != sizeof(Elf32_Sym)) {
        return error("incomplete dynamic symbol metadata");
    }

    FileRange strings;
    FileRange symbols;
    if (!map_address(loads, strtab_address, file_size, &strings) || strtab_size > strings.size) {
        return error("DT_STRTAB is not fully file-backed");
    }
    strings.size = strtab_size;
    if (!map_address(loads, symtab_address, file_size, &symbols)) {
        return error("DT_SYMTAB is not file-backed");
    }

    std::uint32_t symbol_count = 0;
    std::string count_error;
    FileRange hash;
    if (has_sysv_hash) {
        if (!map_address(loads, sysv_hash_address, file_size, &hash) ||
            !sysv_symbol_count(bytes, hash, &symbol_count, &count_error)) {
            return error(count_error.empty() ? "DT_HASH is not file-backed" : count_error);
        }
    } else if (has_gnu_hash) {
        if (!map_address(loads, gnu_hash_address, file_size, &hash) ||
            !gnu_symbol_count(bytes, hash, &symbol_count, &count_error)) {
            return error(count_error.empty() ? "DT_GNU_HASH is not file-backed" : count_error);
        }
    } else {
        return error("dynamic symbol table has no supported hash table");
    }
    const std::uint64_t symbol_bytes = static_cast<std::uint64_t>(symbol_count) * sizeof(Elf32_Sym);
    if (!range_fits(0, symbol_bytes, symbols.size)) return error("truncated dynamic symbol table");

    std::vector<std::string> exports;
    std::unordered_set<std::string> seen;
    for (std::uint32_t i = 0; i < symbol_count; ++i) {
        const auto symbol = object_at<Elf32_Sym>(bytes, symbols.offset + i * sizeof(Elf32_Sym));
        const unsigned visibility = symbol.st_other & 0x3U;
        if (symbol.st_name >= strings.size) return error("dynamic symbol name is outside DT_STRTAB");
        const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(strings.offset + symbol.st_name);
        const auto end = bytes.begin() + static_cast<std::ptrdiff_t>(strings.offset + strings.size);
        const auto nul = std::find(first, end, 0);
        if (nul == end) return error("unterminated dynamic symbol name");
        if (symbol.st_shndx == SHN_UNDEF ||
            (ELF32_ST_BIND(symbol.st_info) != STB_GLOBAL && ELF32_ST_BIND(symbol.st_info) != STB_WEAK) ||
            (ELF32_ST_TYPE(symbol.st_info) != STT_FUNC && ELF32_ST_TYPE(symbol.st_info) != STT_NOTYPE) ||
            (visibility != STV_DEFAULT && visibility != STV_PROTECTED)) {
            continue;
        }
        const std::string name(first, nul);
        const bool wanted = name == "JNI_OnLoad" ||
                            (name.starts_with("Java_") && decode_jni_export(name).has_value());
        if (wanted && seen.insert(name).second) exports.push_back(name);
    }
    return {ElfSymbolStatus::Ok, std::move(exports), "ok"};
}

}  // namespace zb
