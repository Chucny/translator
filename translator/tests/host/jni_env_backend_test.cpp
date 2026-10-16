// Executable fake-JNI checks for the real ART backend (core/android/jni_env_backend.cpp): plugin
// class-loader routing, long-form and short-form native discovery, and every failure mode the JNI
// loader has to survive without losing the rest of the library.
//
// The fake models a tiny Java world: a few classes with declared methods, java.lang.Class objects
// that stand for types, java.lang.invoke.MethodType, and a set of type names that cannot be
// resolved, which is what ART throws on for a plugin that bundles classes it cannot load.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "jni_env_backend.h"

namespace {

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);      \
            std::fflush(stderr);                                                                    \
            std::_Exit(1);                                                                          \
        }                                                                                           \
    } while (0)

// ---------------------------------------------------------------- the toy Java world

struct MethodDef {
    const char* cls;        // binary name, as ClassLoader.loadClass takes it
    const char* name;
    const char* params[4];  // java.lang.Class.getName() spellings, nullptr-terminated
    const char* ret;
    bool is_native;
    bool is_static;
};

// "com.google.ads.Ad" stands for the AdMob classes Orange Roulette bundles next to lime: present
// in the dex, not resolvable at run time.
constexpr const char* kUnresolvable = "com.google.ads.Ad";

const MethodDef kMethods[] = {
    {"zb.Natives", "longStatic", {"int", "java.lang.String", nullptr}, "int", true, true},
    {"zb.Natives", "longInstance", {"long", nullptr}, "java.lang.String", true, false},
    {"zb.Natives", "blob", {"[B", nullptr}, "[Ljava.lang.String;", true, true},
    {"zb.Natives", "shortName", {nullptr}, "void", true, true},
    {"zb.Natives", "shortName", {"int", nullptr}, "int", true, false},
    {"zb.Natives", "mixed", {kUnresolvable, nullptr}, "void", true, true},
    {"zb.Natives", "plain", {"int", nullptr}, "int", false, false},
    {"zb.Overload", "pick", {"int", nullptr}, "int", true, true},
    {"zb.Overload", "pick", {kUnresolvable, nullptr}, "int", true, true},
    {"zb.Broken", "broken", {nullptr}, "void", true, true},
};

constexpr jint kAccStatic = 0x0008;
constexpr jint kAccNative = 0x0100;

bool unresolvable_type(const std::string& java_name) {
    return java_name == kUnresolvable;
}

std::string java_type_name(const std::string& descriptor) {
    if (descriptor.size() == 1) {
        switch (descriptor[0]) {
        case 'Z': return "boolean";
        case 'B': return "byte";
        case 'C': return "char";
        case 'S': return "short";
        case 'I': return "int";
        case 'J': return "long";
        case 'F': return "float";
        case 'D': return "double";
        case 'V': return "void";
        default: return descriptor;
        }
    }
    std::string out = descriptor;
    if (out.front() == 'L' && out.back() == ';') out = out.substr(1, out.size() - 2);
    for (char& c : out) {
        if (c == '/') c = '.';
    }
    return out;
}

bool primitive_name(const std::string& java_name) {
    static const char* kNames[] = {"boolean", "byte", "char", "short", "int", "long", "float", "double", "void"};
    for (const char* name : kNames) {
        if (java_name == name) return true;
    }
    return false;
}

// Parameter type names of "(...)V"; false when the descriptor is malformed.
bool parse_parameters(const std::string& descriptor, std::vector<std::string>& out) {
    out.clear();
    if (descriptor.empty() || descriptor.front() != '(') return false;
    std::size_t i = 1;
    while (i < descriptor.size() && descriptor[i] != ')') {
        const std::size_t start = i;
        while (i < descriptor.size() && descriptor[i] == '[') ++i;
        if (i >= descriptor.size()) return false;
        if (descriptor[i] == 'L') {
            const std::size_t end = descriptor.find(';', i);
            if (end == std::string::npos) return false;
            i = end + 1;
        } else {
            ++i;
        }
        out.push_back(java_type_name(descriptor.substr(start, i - start)));
    }
    return i < descriptor.size();
}

// ---------------------------------------------------------------- fake objects

enum class Kind { Class, String, Method, Array, MethodType, Throwable };

struct Obj {
    Kind kind = Kind::Class;
    std::string text;  // class name, string value, or throwable class in JNI form
    bool primitive = false;
    int method = -1;  // index into kMethods
    std::vector<Obj*> elements;
};

std::vector<Obj*> arena;

Obj* alloc(Kind kind, std::string text = {}) {
    Obj* object = new Obj();
    object->kind = kind;
    object->text = std::move(text);
    arena.push_back(object);
    return object;
}

Obj* as_obj(jobject ref) {
    return reinterpret_cast<Obj*>(ref);
}

jobject as_ref(Obj* object) {
    return reinterpret_cast<jobject>(object);
}

std::map<std::string, Obj*>& type_cache() {
    static std::map<std::string, Obj*> cache;
    return cache;
}

Obj* type_object(const std::string& java_name) {
    auto it = type_cache().find(java_name);
    if (it != type_cache().end()) return it->second;
    Obj* object = alloc(Kind::Class, java_name);
    object->primitive = primitive_name(java_name);
    type_cache().emplace(java_name, object);
    return object;
}

enum Mid {
    MID_NONE = 0,
    MID_CLASS_GET_NAME,
    MID_CLASS_IS_PRIMITIVE,
    MID_CLASS_GET_DECLARED_METHODS,
    MID_CLASS_GET_DECLARED_METHOD,
    MID_LOADER_LOAD_CLASS,
    MID_METHOD_GET_NAME,
    MID_METHOD_GET_MODIFIERS,
    MID_METHOD_GET_RETURN_TYPE,
    MID_EXEC_GET_PARAMETER_TYPES,
    MID_MODIFIER_IS_NATIVE,
    MID_MODIFIER_IS_STATIC,
    MID_MT_FROM_DESCRIPTOR,
    MID_MT_PARAMETER_ARRAY,
};

Mid mid_for(const std::string& cls, const std::string& name) {
    if (cls == "java/lang/Class") {
        if (name == "getName") return MID_CLASS_GET_NAME;
        if (name == "isPrimitive") return MID_CLASS_IS_PRIMITIVE;
        if (name == "getDeclaredMethods") return MID_CLASS_GET_DECLARED_METHODS;
        if (name == "getDeclaredMethod") return MID_CLASS_GET_DECLARED_METHOD;
    } else if (cls == "java/lang/ClassLoader") {
        if (name == "loadClass") return MID_LOADER_LOAD_CLASS;
    } else if (cls == "java/lang/reflect/Method") {
        if (name == "getName") return MID_METHOD_GET_NAME;
        if (name == "getModifiers") return MID_METHOD_GET_MODIFIERS;
        if (name == "getReturnType") return MID_METHOD_GET_RETURN_TYPE;
    } else if (cls == "java/lang/reflect/Executable") {
        if (name == "getParameterTypes") return MID_EXEC_GET_PARAMETER_TYPES;
    } else if (cls == "java/lang/reflect/Modifier") {
        if (name == "isNative") return MID_MODIFIER_IS_NATIVE;
        if (name == "isStatic") return MID_MODIFIER_IS_STATIC;
    } else if (cls == "java/lang/invoke/MethodType") {
        if (name == "fromMethodDescriptorString") return MID_MT_FROM_DESCRIPTOR;
        if (name == "parameterArray") return MID_MT_PARAMETER_ARRAY;
    }
    return MID_NONE;
}

Mid mid_of(jmethodID id) {
    return static_cast<Mid>(reinterpret_cast<std::uintptr_t>(id));
}

struct Fake {
    Obj* pending = nullptr;
    Obj* loader = nullptr;
    int find_class_calls = 0;
    int load_class_calls = 0;
    int declared_methods_calls = 0;
    int declared_method_calls = 0;
    int from_descriptor_calls = 0;
    int frame_depth = 0;
    bool no_method_type = false;  // pre-API-26-like world: java.lang.invoke.MethodType is absent
    bool push_fails = false;
    std::string last_loaded;
};

Fake fake;

void throw_java(const char* cls) {
    fake.pending = alloc(Kind::Throwable, cls);
}

// ---------------------------------------------------------------- JNIEnv

jclass JNICALL find_class(JNIEnv*, const char* name) {
    ++fake.find_class_calls;
    const std::string wanted = name;
    if (fake.no_method_type && wanted == "java/lang/invoke/MethodType") {
        throw_java("java/lang/NoClassDefFoundError");
        return nullptr;
    }
    return reinterpret_cast<jclass>(as_ref(alloc(Kind::Class, wanted)));
}

jobject JNICALL new_global_ref(JNIEnv*, jobject object) {
    return object;
}

void JNICALL delete_global_ref(JNIEnv*, jobject) {}

void JNICALL delete_local_ref(JNIEnv*, jobject) {}

jmethodID JNICALL get_method_id(JNIEnv*, jclass cls, const char* name, const char*) {
    const Mid mid = mid_for(as_obj(cls)->text, name);
    if (mid == MID_NONE) {
        throw_java("java/lang/NoSuchMethodError");
        return nullptr;
    }
    return reinterpret_cast<jmethodID>(static_cast<std::uintptr_t>(mid));
}

jmethodID JNICALL get_static_method_id(JNIEnv* env, jclass cls, const char* name, const char* signature) {
    return get_method_id(env, cls, name, signature);
}

jboolean JNICALL is_instance_of(JNIEnv*, jobject object, jclass cls) {
    if (object == nullptr) return JNI_FALSE;
    return as_obj(object)->text == as_obj(cls)->text ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL exception_check(JNIEnv*) {
    return fake.pending != nullptr ? JNI_TRUE : JNI_FALSE;
}

jthrowable JNICALL exception_occurred(JNIEnv*) {
    return reinterpret_cast<jthrowable>(as_ref(fake.pending));
}

void JNICALL exception_clear(JNIEnv*) {
    fake.pending = nullptr;
}

jint JNICALL throw_object(JNIEnv*, jthrowable thrown) {
    fake.pending = as_obj(thrown);
    return 0;
}

jstring JNICALL new_string_utf(JNIEnv*, const char* value) {
    return reinterpret_cast<jstring>(as_ref(alloc(Kind::String, value)));
}

const char* JNICALL get_string_utf_chars(JNIEnv*, jstring text, jboolean* copied) {
    if (copied != nullptr) *copied = JNI_FALSE;
    return as_obj(text)->text.c_str();
}

void JNICALL release_string_utf_chars(JNIEnv*, jstring, const char*) {}

jint JNICALL push_local_frame(JNIEnv*, jint) {
    if (fake.push_fails) {
        throw_java("java/lang/OutOfMemoryError");
        return JNI_ERR;
    }
    ++fake.frame_depth;
    return JNI_OK;
}

jobject JNICALL pop_local_frame(JNIEnv*, jobject result) {
    CHECK(fake.frame_depth > 0);
    --fake.frame_depth;
    return result;
}

jsize JNICALL get_array_length(JNIEnv*, jarray array) {
    return static_cast<jsize>(as_obj(array)->elements.size());
}

jobject JNICALL get_object_array_element(JNIEnv*, jobjectArray array, jsize index) {
    return as_ref(as_obj(array)->elements[static_cast<std::size_t>(index)]);
}

// Method.getParameterTypes() and Method.getReturnType() resolve types, so they throw for a method
// that names a class the plugin cannot load. This is the per-method seam of the enumeration.
Obj* parameter_types(const MethodDef& method) {
    Obj* array = alloc(Kind::Array);
    for (int i = 0; i < 4 && method.params[i] != nullptr; ++i) {
        if (unresolvable_type(method.params[i])) {
            throw_java("java/lang/NoClassDefFoundError");
            return nullptr;
        }
        array->elements.push_back(type_object(method.params[i]));
    }
    return array;
}

Obj* declared_methods(const std::string& cls) {
    ++fake.declared_methods_calls;
    if (cls == "zb.Broken") {  // getDeclaredMethods() resolves every declared method eagerly
        throw_java("java/lang/NoClassDefFoundError");
        return nullptr;
    }
    Obj* array = alloc(Kind::Array);
    for (std::size_t i = 0; i < sizeof kMethods / sizeof kMethods[0]; ++i) {
        if (cls != kMethods[i].cls) continue;
        Obj* method = alloc(Kind::Method);
        method->method = static_cast<int>(i);
        array->elements.push_back(method);
    }
    return array;
}

// Class.getDeclaredMethod(String, Class[]) compares against every same-named declared method, so it
// throws when a sibling overload names an unresolvable type, and NoSuchMethodException when nothing
// matches.
Obj* declared_method(const std::string& cls, const std::string& name, Obj* parameters) {
    ++fake.declared_method_calls;
    int found = -1;
    for (std::size_t i = 0; i < sizeof kMethods / sizeof kMethods[0]; ++i) {
        const MethodDef& method = kMethods[i];
        if (cls != method.cls || name != method.name) continue;
        std::size_t count = 0;
        bool broken = false;
        bool same = true;
        for (int p = 0; p < 4 && method.params[p] != nullptr; ++p, ++count) {
            if (unresolvable_type(method.params[p])) broken = true;
        }
        if (broken) {
            throw_java("java/lang/NoClassDefFoundError");
            return nullptr;
        }
        if (count != parameters->elements.size()) continue;
        for (std::size_t p = 0; p < count; ++p) {
            if (parameters->elements[p]->text != method.params[p]) same = false;
        }
        if (same && found < 0) found = static_cast<int>(i);
    }
    if (found < 0) {
        throw_java("java/lang/NoSuchMethodException");
        return nullptr;
    }
    Obj* method = alloc(Kind::Method);
    method->method = found;
    return method;
}

jobject JNICALL call_object_method_v(JNIEnv*, jobject self, jmethodID id, va_list args) {
    Obj* object = as_obj(self);
    switch (mid_of(id)) {
    case MID_CLASS_GET_NAME:
        return as_ref(alloc(Kind::String, object->text));
    case MID_CLASS_GET_DECLARED_METHODS:
        return as_ref(declared_methods(object->text));
    case MID_CLASS_GET_DECLARED_METHOD: {
        Obj* name = as_obj(va_arg(args, jstring));
        Obj* parameters = as_obj(va_arg(args, jobjectArray));
        return as_ref(declared_method(object->text, name->text, parameters));
    }
    case MID_LOADER_LOAD_CLASS: {
        ++fake.load_class_calls;
        Obj* name = as_obj(va_arg(args, jstring));
        fake.last_loaded = name->text;
        if (name->text == "zb.Absent") {
            throw_java("java/lang/ClassNotFoundException");
            return nullptr;
        }
        if (name->text == "zb.Bad") {
            throw_java("java/lang/IllegalStateException");
            return nullptr;
        }
        return as_ref(alloc(Kind::Class, name->text));
    }
    case MID_METHOD_GET_NAME:
        return as_ref(alloc(Kind::String, kMethods[object->method].name));
    case MID_METHOD_GET_RETURN_TYPE: {
        const char* ret = kMethods[object->method].ret;
        if (unresolvable_type(ret)) {
            throw_java("java/lang/NoClassDefFoundError");
            return nullptr;
        }
        return as_ref(type_object(ret));
    }
    case MID_EXEC_GET_PARAMETER_TYPES:
        return as_ref(parameter_types(kMethods[object->method]));
    case MID_MT_PARAMETER_ARRAY: {
        Obj* array = alloc(Kind::Array);
        array->elements = object->elements;
        return as_ref(array);
    }
    default:
        CHECK(false);
        return nullptr;
    }
}

jobject JNICALL call_static_object_method_v(JNIEnv*, jclass, jmethodID id, va_list args) {
    CHECK(mid_of(id) == MID_MT_FROM_DESCRIPTOR);
    ++fake.from_descriptor_calls;
    Obj* descriptor = as_obj(va_arg(args, jstring));
    Obj* loader = as_obj(va_arg(args, jobject));
    CHECK(loader == fake.loader);
    std::vector<std::string> names;
    if (!parse_parameters(descriptor->text, names)) {
        throw_java("java/lang/IllegalArgumentException");
        return nullptr;
    }
    Obj* type = alloc(Kind::MethodType, descriptor->text);
    for (const std::string& name : names) {
        if (unresolvable_type(name)) {
            throw_java("java/lang/TypeNotPresentException");
            return nullptr;
        }
        type->elements.push_back(type_object(name));
    }
    return as_ref(type);
}

jint JNICALL call_int_method_v(JNIEnv*, jobject self, jmethodID id, va_list) {
    CHECK(mid_of(id) == MID_METHOD_GET_MODIFIERS);
    const MethodDef& method = kMethods[as_obj(self)->method];
    return (method.is_native ? kAccNative : 0) | (method.is_static ? kAccStatic : 0);
}

jboolean JNICALL call_boolean_method_v(JNIEnv*, jobject self, jmethodID id, va_list) {
    CHECK(mid_of(id) == MID_CLASS_IS_PRIMITIVE);
    return as_obj(self)->primitive ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL call_static_boolean_method_v(JNIEnv*, jclass, jmethodID id, va_list args) {
    const jint modifiers = va_arg(args, jint);
    const jint mask = mid_of(id) == MID_MODIFIER_IS_NATIVE ? kAccNative : kAccStatic;
    CHECK(mid_of(id) == MID_MODIFIER_IS_NATIVE || mid_of(id) == MID_MODIFIER_IS_STATIC);
    return (modifiers & mask) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNINativeInterface_ make_functions() {
    JNINativeInterface_ functions{};
    functions.FindClass = find_class;
    functions.NewGlobalRef = new_global_ref;
    functions.DeleteGlobalRef = delete_global_ref;
    functions.DeleteLocalRef = delete_local_ref;
    functions.GetMethodID = get_method_id;
    functions.GetStaticMethodID = get_static_method_id;
    functions.IsInstanceOf = is_instance_of;
    functions.ExceptionCheck = exception_check;
    functions.ExceptionOccurred = exception_occurred;
    functions.ExceptionClear = exception_clear;
    functions.Throw = throw_object;
    functions.NewStringUTF = new_string_utf;
    functions.GetStringUTFChars = get_string_utf_chars;
    functions.ReleaseStringUTFChars = release_string_utf_chars;
    functions.PushLocalFrame = push_local_frame;
    functions.PopLocalFrame = pop_local_frame;
    functions.GetArrayLength = get_array_length;
    functions.GetObjectArrayElement = get_object_array_element;
    functions.CallObjectMethodV = call_object_method_v;
    functions.CallStaticObjectMethodV = call_static_object_method_v;
    functions.CallIntMethodV = call_int_method_v;
    functions.CallBooleanMethodV = call_boolean_method_v;
    functions.CallStaticBooleanMethodV = call_static_boolean_method_v;
    return functions;
}

// ---------------------------------------------------------------- checks

struct Lookup {
    zb::NativeLookupStatus status;
    zb::JniBackend::Ref cls;
    std::vector<zb::DeclaredNativeMethod> methods;
};

Lookup lookup(zb::JniEnvBackend& backend, JNIEnv* env, const char* cls, const char* name,
              const char* arguments) {
    Lookup out{zb::NativeLookupStatus::Error, 0, {}};
    out.status = backend.find_declared_natives(reinterpret_cast<std::uintptr_t>(env), cls, name, arguments,
                                               out.cls, out.methods);
    CHECK(fake.frame_depth == 0);
    return out;
}

void check_discovery(JNIEnv* env, zb::JniEnvBackend& backend, bool long_form_fast_path) {
    using zb::NativeLookupStatus;

    // Long form: the exact descriptor, including the real return type, which the export name does
    // not encode. Static and instance both report their staticness.
    const int enumerations = fake.declared_methods_calls;
    Lookup found = lookup(backend, env, "zb/Natives", "longStatic", "(ILjava/lang/String;)");
    CHECK(found.status == NativeLookupStatus::Found);
    CHECK(found.cls != 0);
    CHECK(found.methods.size() == 1);
    CHECK(found.methods[0].signature == "(ILjava/lang/String;)I");
    CHECK(found.methods[0].is_static);
    // The fast path resolves only the types this one signature names, so it never enumerates.
    CHECK(fake.declared_methods_calls == enumerations + (long_form_fast_path ? 0 : 1));
    if (long_form_fast_path) CHECK(fake.from_descriptor_calls > 0);

    found = lookup(backend, env, "zb/Natives", "longInstance", "(J)");
    CHECK(found.status == NativeLookupStatus::Found);
    CHECK(found.methods.size() == 1);
    CHECK(found.methods[0].signature == "(J)Ljava/lang/String;");
    CHECK(!found.methods[0].is_static);

    found = lookup(backend, env, "zb/Natives", "blob", "([B)");
    CHECK(found.status == NativeLookupStatus::Found);
    CHECK(found.methods.size() == 1);
    CHECK(found.methods[0].signature == "([B)[Ljava/lang/String;");

    // Long form naming a type that cannot be resolved: skip this export, keep the library.
    found = lookup(backend, env, "zb/Natives", "mixed", "(Lcom/google/ads/Ad;)");
    CHECK(found.status == NativeLookupStatus::Unresolvable);
    CHECK(found.cls == 0 && found.methods.empty());
    CHECK(fake.pending == nullptr);

    // Long form whose sibling overload is unresolvable: also a skip, not a library failure.
    found = lookup(backend, env, "zb/Overload", "pick", "(I)");
    CHECK(found.status == NativeLookupStatus::Unresolvable);
    CHECK(fake.pending == nullptr);

    // Declared but not native, and not declared at all: Found with nothing to bind, which the
    // loader skips as an unmatched export.
    found = lookup(backend, env, "zb/Natives", "plain", "(I)");
    CHECK(found.status == NativeLookupStatus::Found && found.methods.empty());
    found = lookup(backend, env, "zb/Natives", "absent", "(I)");
    CHECK(found.status == NativeLookupStatus::Found && found.methods.empty());
    CHECK(fake.pending == nullptr);

    // Short form: every overload binds, so enumeration is still required.
    found = lookup(backend, env, "zb/Natives", "shortName", nullptr);
    CHECK(found.status == NativeLookupStatus::Found);
    CHECK(found.methods.size() == 2);
    CHECK(found.methods[0].signature == "()V" && found.methods[0].is_static);
    CHECK(found.methods[1].signature == "(I)I" && !found.methods[1].is_static);

    // Short form whose matching method names an unresolvable type: skip that export only.
    found = lookup(backend, env, "zb/Natives", "mixed", nullptr);
    CHECK(found.status == NativeLookupStatus::Unresolvable);
    CHECK(found.cls == 0 && found.methods.empty());
    CHECK(fake.pending == nullptr);

    // getDeclaredMethods() itself throwing is a skip too.
    found = lookup(backend, env, "zb/Broken", "broken", nullptr);
    CHECK(found.status == NativeLookupStatus::Unresolvable);
    CHECK(found.cls == 0 && found.methods.empty());
    CHECK(fake.pending == nullptr);

    // Unchanged: a class that is not there is MissingClass with the exception cleared, and any
    // other loadClass failure is a real error that stays pending.
    found = lookup(backend, env, "zb/Absent", "gone", nullptr);
    CHECK(found.status == NativeLookupStatus::MissingClass);
    CHECK(found.cls == 0 && fake.pending == nullptr);
    found = lookup(backend, env, "zb/Bad", "boom", nullptr);
    CHECK(found.status == NativeLookupStatus::Error);
    CHECK(found.cls == 0 && fake.pending != nullptr);
    CHECK(fake.pending->text == "java/lang/IllegalStateException");
    fake.pending = nullptr;

    // A frame that cannot be pushed is an error, not a skip.
    fake.push_fails = true;
    found = lookup(backend, env, "zb/Natives", "shortName", nullptr);
    CHECK(found.status == NativeLookupStatus::Error && found.cls == 0);
    fake.push_fails = false;
    fake.pending = nullptr;
}

}  // namespace

int main() {
    JNINativeInterface_ functions = make_functions();
    JNIEnv env{&functions};

    fake.loader = alloc(Kind::Class, "java/lang/ClassLoader");

    // Plugin class-loader routing: find_class goes through the retained loader, not FindClass.
    zb::JniEnvBackend backend(nullptr);
    CHECK(backend.set_class_loader(&env, as_ref(fake.loader)));
    const int setup_find_calls = fake.find_class_calls;
    const auto found = backend.find_class(reinterpret_cast<std::uintptr_t>(&env), "zb/Natives");
    CHECK(found != 0);
    CHECK(as_obj(reinterpret_cast<jobject>(found))->text == "zb.Natives");
    CHECK(fake.find_class_calls == setup_find_calls);
    CHECK(fake.load_class_calls == 1);
    CHECK(fake.last_loaded == "zb.Natives");

    check_discovery(&env, backend, true);

    // The same behaviour without java.lang.invoke.MethodType: the long-form fast path is optional,
    // and falling back to enumeration must still skip instead of failing the library.
    fake.no_method_type = true;
    fake.pending = nullptr;
    zb::JniEnvBackend fallback(nullptr);
    CHECK(fallback.set_class_loader(&env, as_ref(fake.loader)));
    check_discovery(&env, fallback, false);

    std::puts("jni env backend: native discovery passed");
    return 0;
}
