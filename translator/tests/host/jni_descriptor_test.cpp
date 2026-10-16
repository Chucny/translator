// Portable descriptor helpers used by the Android ART reflection backend.
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "zb/jni_descriptor.h"

namespace {

int failures = 0;

void expect_value(const char* what, const std::optional<std::string>& got, const char* want) {
    if (!got || *got != want) {
        std::printf("FAIL %s: got %s, want %s\n", what, got ? got->c_str() : "(nullopt)", want);
        ++failures;
    }
}

void expect_none(const char* what, const std::optional<std::string>& got) {
    if (got) {
        std::printf("FAIL %s: got %s, want nullopt\n", what, got->c_str());
        ++failures;
    }
}

}  // namespace

int main() {
    using zb::jni_binary_class_name;
    using zb::jni_method_descriptor;
    using zb::jni_type_descriptor;

    expect_value("binary name", jni_binary_class_name("org/haxe/lime/Lime"), "org.haxe.lime.Lime");
    expect_value("binary nested", jni_binary_class_name("a/B$C"), "a.B$C");
    expect_value("binary default package", jni_binary_class_name("Top"), "Top");
    expect_none("binary empty", jni_binary_class_name(""));
    expect_none("binary dotted", jni_binary_class_name("a.b.C"));
    expect_none("binary array", jni_binary_class_name("[I"));
    expect_none("binary empty segment", jni_binary_class_name("a//B"));
    expect_none("binary leading slash", jni_binary_class_name("/a/B"));
    expect_none("binary trailing slash", jni_binary_class_name("a/B/"));
    expect_none("binary semicolon", jni_binary_class_name("a/B;"));

    const struct {
        const char* name;
        const char* letter;
    } primitives[] = {{"boolean", "Z"}, {"byte", "B"}, {"char", "C"}, {"short", "S"}, {"int", "I"},
                      {"long", "J"},    {"float", "F"}, {"double", "D"}, {"void", "V"}};
    for (const auto& primitive : primitives) {
        expect_value(primitive.name, jni_type_descriptor(primitive.name, true), primitive.letter);
    }
    expect_none("unknown primitive", jni_type_descriptor("integer", true));
    expect_none("reference flagged primitive", jni_type_descriptor("java.lang.String", true));

    expect_value("reference", jni_type_descriptor("java.lang.String", false), "Ljava/lang/String;");
    expect_value("nested reference", jni_type_descriptor("a.b.Outer$Inner", false), "La/b/Outer$Inner;");
    expect_value("default package", jni_type_descriptor("Top", false), "LTop;");
    // A class really named "int" (legal in dex, not in Java source) is a reference.
    expect_value("class named int", jni_type_descriptor("int", false), "Lint;");
    expect_value("int array", jni_type_descriptor("[I", false), "[I");
    expect_value("2d int array", jni_type_descriptor("[[I", false), "[[I");
    expect_value("string array", jni_type_descriptor("[Ljava.lang.String;", false), "[Ljava/lang/String;");
    expect_value("2d string array", jni_type_descriptor("[[Ljava.lang.String;", false), "[[Ljava/lang/String;");
    expect_value("nested array", jni_type_descriptor("[La.B$C;", false), "[La/B$C;");
    expect_none("empty", jni_type_descriptor("", false));
    expect_none("array primitive flag", jni_type_descriptor("[I", true));
    expect_none("void array", jni_type_descriptor("[V", false));
    expect_none("bare bracket", jni_type_descriptor("[", false));
    expect_none("bad array letter", jni_type_descriptor("[Q", false));
    expect_none("array trailing", jni_type_descriptor("[II", false));
    expect_none("unterminated array ref", jni_type_descriptor("[Ljava.lang.String", false));
    expect_none("empty array ref", jni_type_descriptor("[L;", false));
    expect_none("array ref trailing", jni_type_descriptor("[La.B;x", false));
    expect_none("slashed reference", jni_type_descriptor("java/lang/String", false));
    expect_none("semicolon reference", jni_type_descriptor("a.B;", false));
    expect_none("empty segment", jni_type_descriptor("a..B", false));
    expect_none("trailing dot", jni_type_descriptor("a.B.", false));
    expect_none("array empty segment", jni_type_descriptor("[La..B;", false));

    expect_value("empty method", jni_method_descriptor({}, "V"), "()V");
    expect_value("primitive method", jni_method_descriptor({"Z", "B", "C", "S", "I", "J", "F", "D"}, "I"),
                 "(ZBCSIJFD)I");
    expect_value("array method", jni_method_descriptor({"[B", "[[Ljava/lang/String;"}, "[[I"),
                 "([B[[Ljava/lang/String;)[[I");
    expect_none("void parameter", jni_method_descriptor({"V"}, "I"));
    expect_none("empty parameter", jni_method_descriptor({""}, "I"));
    expect_none("empty result", jni_method_descriptor({"I"}, ""));

    if (failures != 0) {
        std::printf("jni_descriptor_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("jni_descriptor_test: OK\n");
    return 0;
}
