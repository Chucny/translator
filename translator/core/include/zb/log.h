#pragma once

namespace zb {

// Diagnostic line to stderr, prefixed with "[zb] ".
[[gnu::format(printf, 1, 2)]] void log(const char* fmt, ...);

}  // namespace zb
