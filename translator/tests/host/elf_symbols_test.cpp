#include <elf.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "zb/elf_symbols.h"

namespace {

constexpr std::size_t kFileSize = 0x800;
constexpr std::uint32_t kBase = 0x1000;
constexpr std::size_t kDynamicOffset = 0x100;
constexpr std::size_t kStringOffset = 0x300;
constexpr std::size_t kSymbolOffset = 0x400;
constexpr std::size_t kHashOffset = 0x600;

struct TempFile {
    explicit TempFile(const std::vector<std::uint8_t>& bytes) {
        char name[] = "/tmp/zb-elf-symbols-XXXXXX";
        const int fd = ::mkstemp(name);
        CHECK(fd >= 0);
        path = name;
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
            CHECK(n > 0);
            done += static_cast<std::size_t>(n);
        }
        CHECK(::close(fd) == 0);
    }
    ~TempFile() { ::unlink(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    std::string path;
};

template <typename T>
void put(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value) {
    CHECK(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

template <typename T>
T get(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    CHECK(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

Elf32_Dyn dyn(std::int32_t tag, std::uint32_t value) {
    Elf32_Dyn entry{};
    entry.d_tag = tag;
    entry.d_un.d_val = value;
    return entry;
}

std::uint32_t append_string(std::vector<std::uint8_t>& bytes, std::size_t& cursor, const char* text) {
    const auto index = static_cast<std::uint32_t>(cursor - kStringOffset);
    const std::size_t size = std::strlen(text) + 1;
    CHECK(cursor + size <= kSymbolOffset);
    std::memcpy(bytes.data() + cursor, text, size);
    cursor += size;
    return index;
}

std::vector<std::uint8_t> make_symbols(bool gnu_hash, bool duplicate = false) {
    std::vector<std::uint8_t> bytes(kFileSize);
    Elf32_Ehdr eh{};
    std::memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS32;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_type = ET_DYN;
    eh.e_machine = EM_ARM;
    eh.e_version = EV_CURRENT;
    eh.e_ehsize = sizeof(Elf32_Ehdr);
    eh.e_phoff = sizeof(Elf32_Ehdr);
    eh.e_phentsize = sizeof(Elf32_Phdr);
    eh.e_phnum = 2;
    put(bytes, 0, eh);

    Elf32_Phdr load{};
    load.p_type = PT_LOAD;
    load.p_vaddr = kBase;
    load.p_filesz = bytes.size();
    load.p_memsz = bytes.size();
    put(bytes, eh.e_phoff, load);

    Elf32_Phdr dynamic{};
    dynamic.p_type = PT_DYNAMIC;
    dynamic.p_offset = kDynamicOffset;
    dynamic.p_vaddr = kBase + kDynamicOffset;
    dynamic.p_filesz = 6 * sizeof(Elf32_Dyn);
    dynamic.p_memsz = dynamic.p_filesz;
    put(bytes, eh.e_phoff + sizeof(Elf32_Phdr), dynamic);

    std::size_t string_cursor = kStringOffset + 1;
    const std::uint32_t java_name = append_string(bytes, string_cursor, "Java_zb_Natives_run");
    const std::uint32_t onload_name = append_string(bytes, string_cursor, "JNI_OnLoad");
    const std::uint32_t undefined_name = append_string(bytes, string_cursor, "Java_zb_Natives_missing");
    const std::uint32_t invalid_name = append_string(bytes, string_cursor, "Java_bad");
    const auto string_size = static_cast<std::uint32_t>(string_cursor - kStringOffset);

    const std::vector<Elf32_Dyn> entries = {
        dyn(DT_STRTAB, kBase + kStringOffset), dyn(DT_STRSZ, string_size),
        dyn(DT_SYMTAB, kBase + kSymbolOffset), dyn(DT_SYMENT, sizeof(Elf32_Sym)),
        dyn(gnu_hash ? DT_GNU_HASH : DT_HASH, kBase + kHashOffset), dyn(DT_NULL, 0)};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        put(bytes, kDynamicOffset + i * sizeof(Elf32_Dyn), entries[i]);
    }

    const auto symbol = [](std::uint32_t name, std::uint16_t section) {
        Elf32_Sym result{};
        result.st_name = name;
        result.st_value = 0x2001;
        result.st_info = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
        result.st_other = STV_DEFAULT;
        result.st_shndx = section;
        return result;
    };
    const std::vector<Elf32_Sym> symbols = {
        {}, symbol(java_name, 1), symbol(onload_name, 1), symbol(undefined_name, SHN_UNDEF),
        symbol(invalid_name, 1), symbol(duplicate ? java_name : 0, duplicate ? 1 : SHN_UNDEF)};
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        put(bytes, kSymbolOffset + i * sizeof(Elf32_Sym), symbols[i]);
    }

    if (!gnu_hash) {
        const std::uint32_t hash[] = {1, static_cast<std::uint32_t>(symbols.size()), 1, 0, 0, 0, 0, 0, 0};
        put(bytes, kHashOffset, hash);
    } else {
        // Header, one 32-bit bloom word, one bucket, then chains for symbols 1..5.
        const std::uint32_t hash[] = {1, 1, 1, 0, 0, 1, 2, 2, 2, 2, 3};
        put(bytes, kHashOffset, hash);
    }
    return bytes;
}

void check_scan(bool gnu_hash) {
    TempFile file(make_symbols(gnu_hash));
    const auto report = zb::scan_elf32_jni_exports(file.path);
    CHECK(report.status == zb::ElfSymbolStatus::Ok);
    CHECK(report.exports.size() == 2);
    CHECK(report.exports[0] == "Java_zb_Natives_run");
    CHECK(report.exports[1] == "JNI_OnLoad");
}

void check_duplicates() {
    TempFile file(make_symbols(false, true));
    const auto report = zb::scan_elf32_jni_exports(file.path);
    CHECK(report.status == zb::ElfSymbolStatus::Ok);
    CHECK(report.exports.size() == 2);
}

void check_skips_and_errors() {
    auto bytes = make_symbols(false);
    bytes[EI_CLASS] = ELFCLASS64;
    TempFile wrong_class(bytes);
    CHECK(zb::scan_elf32_jni_exports(wrong_class.path).status == zb::ElfSymbolStatus::Skipped);

    bytes = make_symbols(false);
    bytes[EI_DATA] = ELFDATA2MSB;
    TempFile wrong_endian(bytes);
    CHECK(zb::scan_elf32_jni_exports(wrong_endian.path).status == zb::ElfSymbolStatus::Skipped);

    bytes = make_symbols(false);
    auto eh = get<Elf32_Ehdr>(bytes, 0);
    eh.e_machine = EM_X86_64;
    put(bytes, 0, eh);
    TempFile wrong_machine(bytes);
    CHECK(zb::scan_elf32_jni_exports(wrong_machine.path).status == zb::ElfSymbolStatus::Skipped);

    std::vector<std::uint8_t> truncated(sizeof(Elf32_Ehdr) - 1);
    std::memcpy(truncated.data(), ELFMAG, SELFMAG);
    TempFile truncated_file(truncated);
    CHECK(zb::scan_elf32_jni_exports(truncated_file.path).status == zb::ElfSymbolStatus::Error);

    bytes = make_symbols(false);
    auto dynamic = get<Elf32_Phdr>(bytes, eh.e_phoff + sizeof(Elf32_Phdr));
    dynamic.p_offset = kFileSize - sizeof(Elf32_Dyn) + 1;
    put(bytes, eh.e_phoff + sizeof(Elf32_Phdr), dynamic);
    TempFile bad_dynamic(bytes);
    CHECK(zb::scan_elf32_jni_exports(bad_dynamic.path).status == zb::ElfSymbolStatus::Error);

    bytes = make_symbols(false);
    auto strtab = get<Elf32_Dyn>(bytes, kDynamicOffset);
    strtab.d_un.d_val = kBase + kFileSize + 1;
    put(bytes, kDynamicOffset, strtab);
    TempFile bad_strtab(bytes);
    CHECK(zb::scan_elf32_jni_exports(bad_strtab.path).status == zb::ElfSymbolStatus::Error);

    bytes = make_symbols(false);
    std::uint32_t impossible_count = 0xffffffffU;
    put(bytes, kHashOffset + sizeof(std::uint32_t), impossible_count);
    TempFile bad_sysv_hash(bytes);
    CHECK(zb::scan_elf32_jni_exports(bad_sysv_hash.path).status == zb::ElfSymbolStatus::Error);

    bytes = make_symbols(true);
    std::uint32_t unterminated_chain = 2;
    put(bytes, kHashOffset + 10 * sizeof(std::uint32_t), unterminated_chain);
    TempFile bad_gnu_hash(bytes);
    CHECK(zb::scan_elf32_jni_exports(bad_gnu_hash.path).status == zb::ElfSymbolStatus::Error);

    bytes = make_symbols(false);
    auto symbol = get<Elf32_Sym>(bytes, kSymbolOffset + sizeof(Elf32_Sym));
    symbol.st_name = 0xffffffffU;
    put(bytes, kSymbolOffset + sizeof(Elf32_Sym), symbol);
    TempFile bad_symbol(bytes);
    CHECK(zb::scan_elf32_jni_exports(bad_symbol.path).status == zb::ElfSymbolStatus::Error);
}

// Untrusted APK input: many GNU hash buckets sharing one long chain must not cost
// buckets * chain steps (about 2^36 here). Any status is fine; the scan must finish quickly.
void check_gnu_hash_work_is_bounded() {
    constexpr std::uint32_t kBuckets = 1U << 18;
    constexpr std::uint32_t kChain = 1U << 18;
    auto bytes = make_symbols(true);
    const std::size_t words = 4 + 1 + kBuckets + kChain;
    bytes.resize(kHashOffset + words * sizeof(std::uint32_t));

    auto eh = get<Elf32_Ehdr>(bytes, 0);
    auto load = get<Elf32_Phdr>(bytes, eh.e_phoff);
    load.p_filesz = static_cast<std::uint32_t>(bytes.size());
    load.p_memsz = load.p_filesz;
    put(bytes, eh.e_phoff, load);

    const std::uint32_t header[] = {kBuckets, 1, 1, 0, 0};
    put(bytes, kHashOffset, header);
    const std::size_t bucket_base = kHashOffset + sizeof header;
    for (std::uint32_t i = 0; i < kBuckets; ++i) put(bytes, bucket_base + i * sizeof(std::uint32_t), std::uint32_t{1});
    const std::size_t chain_base = bucket_base + kBuckets * sizeof(std::uint32_t);
    for (std::uint32_t i = 0; i < kChain; ++i) {
        put(bytes, chain_base + i * sizeof(std::uint32_t), std::uint32_t{i + 1 == kChain ? 1U : 0U});
    }

    TempFile file(bytes);
    const auto begin = std::chrono::steady_clock::now();
    (void)zb::scan_elf32_jni_exports(file.path);
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    CHECK(elapsed < std::chrono::seconds(5));
}

}  // namespace

int main() {
    check_scan(false);
    check_scan(true);
    check_duplicates();
    check_skips_and_errors();
    check_gnu_hash_work_is_bounded();
    std::puts("elf_symbols_test PASS");
    return 0;
}
