#include "zb/jni_shorty.h"

#include <cstddef>

namespace zb {

namespace {

// Parses one type at pos and returns its shorty letter, or 0 when malformed. 'V' is accepted
// only where void_ok is set (the return type).
char parse_type(std::string_view s, std::size_t& pos, bool void_ok) {
    if (pos >= s.size()) return 0;
    switch (s[pos]) {
    case 'Z':
    case 'B':
    case 'C':
    case 'S':
    case 'I':
    case 'J':
    case 'F':
    case 'D':
        return s[pos++];
    case 'V':
        if (!void_ok) return 0;
        ++pos;
        return 'V';
    case 'L': {
        // The class name runs up to the first ';'; any of "()[." found first means the ';' the
        // caller (or an earlier malformed scan) thought terminated this name actually belongs to
        // an outer construct, so reject rather than swallow it. A binary name may not start or
        // end with '/', nor contain "//".
        const std::size_t end = s.find_first_of(";()[.", pos + 1);
        if (end == std::string_view::npos || s[end] != ';') return 0;
        const std::string_view name = s.substr(pos + 1, end - (pos + 1));
        if (name.empty() || name.front() == '/' || name.back() == '/' ||
            name.find("//") != std::string_view::npos) {
            return 0;
        }
        pos = end + 1;
        return 'L';
    }
    case '[': {
        // The JVM limits array types to 255 dimensions.
        std::size_t dims = 0;
        while (pos < s.size() && s[pos] == '[') {
            ++pos;
            ++dims;
        }
        if (dims > 255) return 0;
        return parse_type(s, pos, false) != 0 ? 'L' : 0;
    }
    default:
        return 0;
    }
}

}  // namespace

std::optional<std::string> shorty_from_signature(std::string_view signature) {
    if (signature.empty() || signature[0] != '(') return std::nullopt;
    std::size_t pos = 1;
    std::string parameters;
    while (pos < signature.size() && signature[pos] != ')') {
        const char type = parse_type(signature, pos, false);
        if (type == 0) return std::nullopt;
        parameters.push_back(type);
    }
    if (pos >= signature.size()) return std::nullopt;
    ++pos;  // ')'
    const char return_type = parse_type(signature, pos, true);
    if (return_type == 0 || pos != signature.size()) return std::nullopt;
    return std::string(1, return_type) + parameters;
}

}  // namespace zb
