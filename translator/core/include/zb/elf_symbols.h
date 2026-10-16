#pragma once

#include <string>
#include <vector>

namespace zb {

enum class ElfSymbolStatus {
    Ok,
    Skipped,
    Error,
};

struct ElfSymbolReport {
    ElfSymbolStatus status = ElfSymbolStatus::Error;
    std::vector<std::string> exports;
    std::string message;
};

// Reads the file-backed ELF32 dynamic symbol table without using section headers.
// Only defined, externally visible JNI exports are returned, in symbol-table order.
ElfSymbolReport scan_elf32_jni_exports(const std::string& path);

}  // namespace zb
