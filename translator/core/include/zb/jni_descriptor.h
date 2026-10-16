#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zb {

// Pure JNI descriptor helpers shared by the Android reflection backend and host tests.

// JNI internal class name ("org/haxe/lime/Lime") to the binary name ClassLoader.loadClass takes
// ("org.haxe.lime.Lime"). Empty, array, or already dotted names are rejected.
std::optional<std::string> jni_binary_class_name(std::string_view internal_name);

// java.lang.Class.getName() text to a JNI type descriptor. `is_primitive` is Class.isPrimitive():
//   int (primitive)            -> I       void (primitive) -> V
//   java.lang.String           -> Ljava/lang/String;
//   [Ljava.lang.String;        -> [Ljava/lang/String;
//   [[I                        -> [[I
// Malformed names return nullopt.
std::optional<std::string> jni_type_descriptor(std::string_view class_name, bool is_primitive);

// "(" + parameters + ")" + result. Parameters must not be "V"; nullopt otherwise.
std::optional<std::string> jni_method_descriptor(const std::vector<std::string>& parameters,
                                                 std::string_view result);

}  // namespace zb
