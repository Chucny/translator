// JNI export names: "Java_" + mangled class and method, optional "__" + mangled argument types.
// Escapes: _1 = '_', _2 = ';', _3 = '[', _0xxxx = UTF-16 code unit; a plain '_' is '/'.
#include <cstdio>

#include "check.h"
#include "zb/jni_mangle.h"

int main() {
    auto simple = zb::decode_jni_export("Java_org_haxe_lime_Lime_onTouch");
    CHECK(simple && simple->class_name == "org/haxe/lime/Lime" && simple->method == "onTouch");
    CHECK(!simple->arguments);

    auto underscore = zb::decode_jni_export("Java_com_example_My_1Class_do_1it");
    CHECK(underscore && underscore->class_name == "com/example/My_Class" && underscore->method == "do_it");

    auto overloaded = zb::decode_jni_export("Java_pkg_Foo_bar__ILjava_lang_String_2_3I");
    CHECK(overloaded && overloaded->class_name == "pkg/Foo" && overloaded->method == "bar");
    CHECK(overloaded->arguments && *overloaded->arguments == "(ILjava/lang/String;[I)");

    auto no_args = zb::decode_jni_export("Java_pkg_Foo_bar__");
    CHECK(no_args && no_args->arguments && *no_args->arguments == "()");

    auto unicode = zb::decode_jni_export("Java_pkg_Caf_000e9_run");
    CHECK(unicode && unicode->class_name == "pkg/Caf\xC3\xA9" && unicode->method == "run");

    auto default_package = zb::decode_jni_export("Java_Main_start");
    CHECK(default_package && default_package->class_name == "Main" && default_package->method == "start");

    auto digit_component = zb::decode_jni_export("Java_pkg_Foo_bar_4x");
    CHECK(digit_component && digit_component->class_name == "pkg/Foo/bar" && digit_component->method == "4x");

    // "__" followed by '0'..'3' is not the argument separator: it is '/' followed by an escape.
    auto jna = zb::decode_jni_export("Java_com_sun_jna_Native__1getPointer");
    CHECK(jna && jna->class_name == "com/sun/jna/Native" && jna->method == "_getPointer");
    CHECK(!jna->arguments);

    auto init_array = zb::decode_jni_export("Java_pkg_Foo__1init___3I");
    CHECK(init_array && init_array->class_name == "pkg/Foo" && init_array->method == "_init");
    CHECK(init_array->arguments && *init_array->arguments == "([I)");

    auto org_internal = zb::decode_jni_export("Java_org__1internal_Foo_bar");
    CHECK(org_internal && org_internal->class_name == "org/_internal/Foo" && org_internal->method == "bar");
    CHECK(!org_internal->arguments);

    auto ref_arg = zb::decode_jni_export("Java_pkg_Foo_bar__Lorg__1x_Y_2");
    CHECK(ref_arg && ref_arg->class_name == "pkg/Foo" && ref_arg->method == "bar");
    CHECK(ref_arg->arguments && *ref_arg->arguments == "(Lorg/_x/Y;)");

    auto array_after_sep = zb::decode_jni_export("Java_pkg_Foo_bar___3I");
    CHECK(array_after_sep && array_after_sep->class_name == "pkg/Foo" && array_after_sep->method == "bar");
    CHECK(array_after_sep->arguments && *array_after_sep->arguments == "([I)");

    auto inner_class = zb::decode_jni_export("Java_pkg_Outer_00024Inner_run");
    CHECK(inner_class && inner_class->class_name == "pkg/Outer$Inner" && inner_class->method == "run");
    CHECK(!inner_class->arguments);

    auto surrogate_pair = zb::decode_jni_export("Java_pkg_X_0d83d_0de00_run");
    CHECK(surrogate_pair && surrogate_pair->class_name == "pkg/X\xED\xA0\xBD\xED\xB8\x80" &&
          surrogate_pair->method == "run");

    CHECK(!zb::decode_jni_export("JNI_OnLoad"));
    CHECK(!zb::decode_jni_export("Java_nomethod"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_bar_"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_bar_0zz12"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_bar__I__J"));
    CHECK(!zb::decode_jni_export("Java_"));
    CHECK(!zb::decode_jni_export("Java__a_b"));
    CHECK(!zb::decode_jni_export("Java_a/b_c"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_0002fbar"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_00000bar"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_b_00061r"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_000E9run"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo_bar_000e"));
    CHECK(!zb::decode_jni_export("Java_pkg_Foo__3bar"));

    std::printf("jni_mangle_test ok\n");
    return 0;
}
