// Decoding of JNI export names (JNI specification, "Resolving Native Method Names").
#include "zb/jni_mangle.h"

namespace zb {

namespace {

// Appends one UTF-16 code unit as UTF-8 (surrogates stay separate, as in modified UTF-8).
void append_utf8(std::string& out, unsigned unit) {
    if (unit < 0x80) {
        out.push_back(static_cast<char>(unit));
    } else if (unit < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
        out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    }
}

// Lowercase hex digits only: ART's mangler always emits "_0xxxx" in lowercase.
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool is_ascii_alnum(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// True for a code unit that has its own canonical form, so ART's mangler would never reach it
// through "_0xxxx": ASCII alphanumerics pass through unescaped, '.' and '/' become a lone '_',
// '_' becomes "_1", ';' becomes "_2", '[' becomes "_3".
bool has_canonical_form(unsigned unit) {
    if (unit > 0x7F) return false;
    const char c = static_cast<char>(unit);
    return is_ascii_alnum(c) || c == '.' || c == '/' || c == '_' || c == ';' || c == '[';
}

// Decodes from pos until the end or a "__" separator (then hit_separator is set and pos is past
// it). A raw character must be [A-Za-z0-9]. A lone '_' (the next character is not '_') becomes
// '/'. "__" is the separator, unless the character after it is '0'..'3': then the first '_' is
// itself a lone '/' and the second '_' starts an escape for the following source character (e.g.
// a method named "_init" mangles as "..__1init", not a separator). "___3" (an array argument
// right after the real separator) still parses as separator + "_3". Escapes: "_1" = '_',
// "_2" = ';', "_3" = '[', "_0xxxx" = a UTF-16 code unit as 4 lowercase hex digits. Returns false
// for a non-alnum raw character, a malformed escape, a trailing '_', or an "_0xxxx" escape for a
// code unit that ART could not have produced that way: 0, or one with its own canonical form.
bool decode_part(std::string_view in, std::size_t& pos, std::string& out, bool& hit_separator) {
    hit_separator = false;
    while (pos < in.size()) {
        const char c = in[pos];
        if (c != '_') {
            if (!is_ascii_alnum(c)) return false;
            out.push_back(c);
            ++pos;
            continue;
        }
        if (pos + 1 >= in.size()) return false;
        switch (in[pos + 1]) {
        case '1':
            out.push_back('_');
            pos += 2;
            break;
        case '2':
            out.push_back(';');
            pos += 2;
            break;
        case '3':
            out.push_back('[');
            pos += 2;
            break;
        case '0': {
            if (pos + 6 > in.size()) return false;
            unsigned unit = 0;
            for (std::size_t i = pos + 2; i < pos + 6; ++i) {
                const int v = hex_value(in[i]);
                if (v < 0) return false;
                unit = unit * 16 + static_cast<unsigned>(v);
            }
            if (unit == 0 || has_canonical_form(unit)) return false;
            append_utf8(out, unit);
            pos += 6;
            break;
        }
        case '_':
            if (pos + 2 < in.size() && in[pos + 2] >= '0' && in[pos + 2] <= '3') {
                // The first '_' is a lone '_' meaning '/'; the second one begins an escape for
                // the very next source character.
                out.push_back('/');
                ++pos;
                break;
            }
            hit_separator = true;
            pos += 2;
            return true;
        default:
            out.push_back('/');
            ++pos;
            break;
        }
    }
    return true;
}

}  // namespace

std::optional<JniExport> decode_jni_export(std::string_view symbol) {
    constexpr std::string_view kPrefix = "Java_";
    if (!symbol.starts_with(kPrefix)) return std::nullopt;
    std::size_t pos = kPrefix.size();

    std::string path;
    bool separator = false;
    if (!decode_part(symbol, pos, path, separator)) return std::nullopt;
    // starts_with is required for the class/method split; the other path-shape checks are
    // defensive because canonical decoding cannot otherwise produce trailing or doubled '/'.
    if (path.starts_with('/') || path.ends_with('/') || path.find("//") != std::string::npos ||
        path.find(';') != std::string::npos || path.find('[') != std::string::npos) {
        return std::nullopt;
    }
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == path.size()) return std::nullopt;

    JniExport result;
    result.class_name = path.substr(0, slash);
    result.method = path.substr(slash + 1);
    if (separator) {
        std::string arguments;
        bool again = false;
        if (!decode_part(symbol, pos, arguments, again) || again) return std::nullopt;
        result.arguments = "(" + arguments + ")";
    }
    return result;
}

}  // namespace zb
