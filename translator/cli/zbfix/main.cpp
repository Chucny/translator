#include <cstdio>

#include "zb/elf_fixups.h"

namespace {

void print_report(const char* path, const zb::ElfFixupReport& report) {
    std::printf("%s: ", path);
    if (!report.changes.empty()) {
        for (std::size_t i = 0; i < report.changes.size(); ++i) {
            if (i != 0) std::printf("; ");
            std::printf("%s", report.changes[i].c_str());
        }
    } else {
        std::printf("%s", report.message.c_str());
    }
    std::putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: zbfix <arm32-library> [...]\n");
        return 2;
    }

    int exit_code = 0;
    for (int i = 1; i < argc; ++i) {
        const zb::ElfFixupReport report = zb::fix_guest_library(argv[i]);
        print_report(argv[i], report);
        if (report.status == zb::ElfFixupStatus::Error) exit_code = 1;
    }
    return exit_code;
}
