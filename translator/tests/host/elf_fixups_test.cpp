#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "zb/elf_fixups.h"

namespace {

constexpr std::size_t kFileSize = 0x300;
constexpr std::size_t kDynamicOffset = 0x100;
constexpr std::size_t kStringOffset = 0x200;
constexpr std::uint32_t kBaseAddress = 0x1000;
constexpr std::uint32_t kStringAddress = kBaseAddress + kStringOffset;

struct TempFile {
    explicit TempFile(const std::vector<std::uint8_t>& bytes) {
        char name[] = "/tmp/zb-elf-fixups-XXXXXX";
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
    CHECK(offset <= bytes.size());
    CHECK(sizeof(T) <= bytes.size() - offset);
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

template <typename T>
T get(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    CHECK(offset <= bytes.size());
    CHECK(sizeof(T) <= bytes.size() - offset);
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    const off_t size = ::lseek(fd, 0, SEEK_END);
    CHECK(size >= 0);
    CHECK(::lseek(fd, 0, SEEK_SET) == 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::read(fd, bytes.data() + done, bytes.size() - done);
        CHECK(n > 0);
        done += static_cast<std::size_t>(n);
    }
    CHECK(::close(fd) == 0);
    return bytes;
}

std::vector<std::uint8_t> make_elf(const std::vector<Elf32_Dyn>& dynamic,
                                   std::size_t dynamic_slots = 0) {
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
    load.p_offset = 0;
    load.p_vaddr = kBaseAddress;
    load.p_filesz = bytes.size();
    load.p_memsz = bytes.size();
    put(bytes, eh.e_phoff, load);

    Elf32_Phdr dyn_phdr{};
    dyn_phdr.p_type = PT_DYNAMIC;
    dyn_phdr.p_offset = kDynamicOffset;
    dyn_phdr.p_vaddr = kBaseAddress + kDynamicOffset;
    dyn_phdr.p_filesz = (dynamic_slots == 0 ? dynamic.size() : dynamic_slots) * sizeof(Elf32_Dyn);
    dyn_phdr.p_memsz = dyn_phdr.p_filesz;
    put(bytes, eh.e_phoff + sizeof(Elf32_Phdr), dyn_phdr);

    for (std::size_t i = 0; i < dynamic.size(); ++i) {
        put(bytes, kDynamicOffset + i * sizeof(Elf32_Dyn), dynamic[i]);
    }
    return bytes;
}

Elf32_Dyn dyn(std::int32_t tag, std::uint32_t value) {
    Elf32_Dyn entry{};
    entry.d_tag = tag;
    entry.d_un.d_val = value;
    return entry;
}

void put_string(std::vector<std::uint8_t>& bytes, std::size_t offset, const std::string& value) {
    CHECK(offset + value.size() < bytes.size());
    std::memcpy(bytes.data() + offset, value.c_str(), value.size() + 1);
}

void check_path_and_textrel_fixups() {
    constexpr std::uint32_t kSlash = 0;
    constexpr std::uint32_t kBackslash = 32;
    constexpr std::uint32_t kPlain = 64;
    auto bytes = make_elf({dyn(DT_STRTAB, kStringAddress),
                           dyn(DT_NEEDED, kSlash),
                           dyn(DT_NEEDED, kBackslash),
                           dyn(DT_NEEDED, kPlain),
                           dyn(DT_TEXTREL, 0),
                           dyn(DT_FLAGS, DF_TEXTREL | DF_SYMBOLIC),
                           dyn(DT_NULL, 0),
                           dyn(DT_NULL, 0)});
    put_string(bytes, kStringOffset + kSlash, "/old/toolchain/libc.so");
    put_string(bytes, kStringOffset + kBackslash, "C:\\old\\toolchain\\libm.so");
    put_string(bytes, kStringOffset + kPlain, "libplain.so");
    TempFile file(bytes);

    const zb::ElfFixupReport report = zb::fix_guest_library(file.path);
    CHECK(report.status == zb::ElfFixupStatus::Changed);
    CHECK(report.changes.size() == 3);

    const auto fixed = read_file(file.path);
    const auto slash = get<Elf32_Dyn>(fixed, kDynamicOffset + sizeof(Elf32_Dyn));
    const auto backslash = get<Elf32_Dyn>(fixed, kDynamicOffset + 2 * sizeof(Elf32_Dyn));
    const auto plain = get<Elf32_Dyn>(fixed, kDynamicOffset + 3 * sizeof(Elf32_Dyn));
    const auto marker = get<Elf32_Dyn>(fixed, kDynamicOffset + 4 * sizeof(Elf32_Dyn));
    const auto flags = get<Elf32_Dyn>(fixed, kDynamicOffset + 5 * sizeof(Elf32_Dyn));
    CHECK(slash.d_un.d_val == kSlash + std::string("/old/toolchain/").size());
    CHECK(backslash.d_un.d_val == kBackslash + std::string("C:\\old\\toolchain\\").size());
    CHECK(plain.d_un.d_val == kPlain);
    CHECK(marker.d_tag == zb::kDtZbTextrel);
    CHECK(marker.d_un.d_val == 1);
    CHECK(flags.d_tag == DT_FLAGS);
    CHECK(flags.d_un.d_val == DF_SYMBOLIC);
    for (std::size_t i = 0; i < fixed.size(); ++i) {
        const bool mutable_word =
            (i >= kDynamicOffset + sizeof(Elf32_Dyn) &&
             i < kDynamicOffset + 3 * sizeof(Elf32_Dyn)) ||
            (i >= kDynamicOffset + 4 * sizeof(Elf32_Dyn) &&
             i < kDynamicOffset + 6 * sizeof(Elf32_Dyn));
        if (!mutable_word) CHECK(fixed[i] == bytes[i]);
    }

    const zb::ElfFixupReport again = zb::fix_guest_library(file.path);
    CHECK(again.status == zb::ElfFixupStatus::Unchanged);
    CHECK(read_file(file.path) == fixed);
}

void check_flags_only_insertion() {
    auto bytes = make_elf({dyn(DT_FLAGS, DF_TEXTREL), dyn(DT_NULL, 0), dyn(DT_NULL, 0)});
    TempFile file(bytes);
    CHECK(zb::fix_guest_library(file.path).status == zb::ElfFixupStatus::Changed);
    const auto fixed = read_file(file.path);
    const auto flags = get<Elf32_Dyn>(fixed, kDynamicOffset);
    const auto marker = get<Elf32_Dyn>(fixed, kDynamicOffset + sizeof(Elf32_Dyn));
    const auto terminator = get<Elf32_Dyn>(fixed, kDynamicOffset + 2 * sizeof(Elf32_Dyn));
    CHECK(flags.d_un.d_val == 0);
    CHECK(marker.d_tag == zb::kDtZbTextrel);
    CHECK(marker.d_un.d_val == 1);
    CHECK(terminator.d_tag == DT_NULL);
}

void check_no_spare_is_atomic_error() {
    auto bytes = make_elf({dyn(DT_FLAGS, DF_TEXTREL), dyn(DT_NULL, 0)}, 2);
    TempFile file(bytes);
    const auto before = read_file(file.path);
    const auto report = zb::fix_guest_library(file.path);
    CHECK(report.status == zb::ElfFixupStatus::Error);
    CHECK(read_file(file.path) == before);
}

void check_unchanged_and_skipped() {
    auto unchanged = make_elf({dyn(DT_NULL, 0)});
    TempFile unchanged_file(unchanged);
    CHECK(zb::fix_guest_library(unchanged_file.path).status == zb::ElfFixupStatus::Unchanged);

    auto wrong_class = unchanged;
    wrong_class[EI_CLASS] = ELFCLASS64;
    TempFile class_file(wrong_class);
    CHECK(zb::fix_guest_library(class_file.path).status == zb::ElfFixupStatus::Skipped);

    auto wrong_endian = unchanged;
    wrong_endian[EI_DATA] = ELFDATA2MSB;
    TempFile endian_file(wrong_endian);
    CHECK(zb::fix_guest_library(endian_file.path).status == zb::ElfFixupStatus::Skipped);

    auto wrong_machine = unchanged;
    auto eh = get<Elf32_Ehdr>(wrong_machine, 0);
    eh.e_machine = EM_X86_64;
    put(wrong_machine, 0, eh);
    TempFile machine_file(wrong_machine);
    CHECK(zb::fix_guest_library(machine_file.path).status == zb::ElfFixupStatus::Skipped);
}

void check_malformed_files() {
    std::vector<std::uint8_t> truncated(sizeof(Elf32_Ehdr) - 1);
    std::memcpy(truncated.data(), ELFMAG, SELFMAG);
    TempFile truncated_file(truncated);
    CHECK(zb::fix_guest_library(truncated_file.path).status == zb::ElfFixupStatus::Error);

    auto bad_phdr = make_elf({dyn(DT_NULL, 0)});
    auto eh = get<Elf32_Ehdr>(bad_phdr, 0);
    eh.e_phoff = kFileSize - sizeof(Elf32_Phdr) + 1;
    put(bad_phdr, 0, eh);
    TempFile bad_phdr_file(bad_phdr);
    CHECK(zb::fix_guest_library(bad_phdr_file.path).status == zb::ElfFixupStatus::Error);
}

}  // namespace

int main() {
    check_path_and_textrel_fixups();
    check_flags_only_insertion();
    check_no_spare_is_atomic_error();
    check_unchanged_and_skipped();
    check_malformed_files();
    std::puts("elf_fixups_test PASS");
    return 0;
}
