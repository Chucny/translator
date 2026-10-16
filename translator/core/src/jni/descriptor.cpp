// Pure JNI descriptor helpers (see zb/jni_descriptor.h).
#include "zb/jni_descriptor.h"

#include <algorithm>

namespace zb {

namespace {

// Non-empty segments separated by `separator`, without array, terminator or the other separator.
bool valid_segments(std::string_view name, char separator, char other) {
    if (name.empty() || name.front() == separator || name.back() == separator) return false;
    char previous = 0;
    for (const char c : name) {
        if (c == '[' || c == ';' || c == other) return false;
        if (c == separator && previous == separator) return false;
        previous = c;
    }
    return true;
}

std::string replace_all(std::string_view name, char from, char to) {
    std::string out(name);
    std::replace(out.begin(), out.end(), from, to);
    return out;
}

bool is_primitive_letter(char c) {
    return std::string_view("ZBCSIJFD").find(c) != std::string_view::npos;
}

}  // namespace

std::optional<std::string> jni_binary_class_name(std::string_view internal_name) {
    if (!valid_segments(internal_name, '/', '.')) return std::nullopt;
    return replace_all(internal_name, '/', '.');
}

std::optional<std::string> jni_type_descriptor(std::string_view class_name, bool is_primitive) {
    if (is_primitive) {
        static const struct {
            std::string_view name;
            const char* letter;
        } kPrimitives[] = {{"boolean", "Z"}, {"byte", "B"}, {"char", "C"}, {"short", "S"}, {"int", "I"},
                           {"long", "J"},    {"float", "F"}, {"double", "D"}, {"void", "V"}};
        for (const auto& primitive : kPrimitives) {
            if (class_name == primitive.name) return std::string(primitive.letter);
        }
        return std::nullopt;
    }
    if (class_name.empty()) return std::nullopt;
    if (class_name.front() != '[') {
        if (!valid_segments(class_name, '.', '/')) return std::nullopt;
        return "L" + replace_all(class_name, '.', '/') + ";";
    }
    // Arrays: Class.getName() already has descriptor shape, only with dots in element names.
    const std::size_t dimensions = class_name.find_first_not_of('[');
    if (dimensions == std::string_view::npos) return std::nullopt;
    const std::string_view element = class_name.substr(dimensions);
    if (element.size() == 1 && is_primitive_letter(element.front())) return std::string(class_name);
    if (element.size() < 3 || element.front() != 'L' || element.back() != ';') return std::nullopt;
    const std::string_view inner = element.substr(1, element.size() - 2);
    if (!valid_segments(inner, '.', '/')) return std::nullopt;
    return std::string(dimensions, '[') + "L" + replace_all(inner, '.', '/') + ";";
}

std::optional<std::string> jni_method_descriptor(const std::vector<std::string>& parameters,
                                                 std::string_view result) {
    if (result.empty()) return std::nullopt;
    std::string out = "(";
    for (const std::string& parameter : parameters) {
        if (parameter.empty() || parameter == "V") return std::nullopt;
        out += parameter;
    }
    out += ')';
    out += result;
    return out;
}

}  // namespace zb
