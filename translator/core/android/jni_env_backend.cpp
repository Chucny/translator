// JniBackend over the real JNIEnv (Android build only; compile-checked here, exercised by T7 in 4d).
#include "jni_env_backend.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "zb/jni_descriptor.h"

namespace zb {

namespace {

static_assert(sizeof(jvalue) == sizeof(JValue) && alignof(jvalue) == alignof(JValue));

JNIEnv* E(JniBackend::Env env) {
    return reinterpret_cast<JNIEnv*>(static_cast<std::uintptr_t>(env));
}

jobject O(JniBackend::Ref ref) {
    return reinterpret_cast<jobject>(static_cast<std::uintptr_t>(ref));
}

jclass C(JniBackend::Ref ref) {
    return static_cast<jclass>(O(ref));
}

JniBackend::Ref R(jobject obj) {
    return static_cast<JniBackend::Ref>(reinterpret_cast<std::uintptr_t>(obj));
}

jmethodID M(JniBackend::Id id) {
    return reinterpret_cast<jmethodID>(static_cast<std::uintptr_t>(id));
}

jfieldID F(JniBackend::Id id) {
    return reinterpret_cast<jfieldID>(static_cast<std::uintptr_t>(id));
}

JniBackend::Id I(void* id) {
    return static_cast<JniBackend::Id>(reinterpret_cast<std::uintptr_t>(id));
}

}  // namespace

JniEnvBackend::JniEnvBackend(JavaVM* vm) : vm_(vm) {}

const JniEnvBackend::Reflection* JniEnvBackend::reflection(JNIEnv* env) {
    // Never run the one-time lookup with an exception pending (CheckJNI forbids FindClass then, and
    // a failed lookup would disable reflection for the whole process).
    if (env->ExceptionCheck()) return nullptr;
    std::call_once(reflection_once_, [&] {
        Reflection r;
        std::vector<jobject> globals;
        bool ok = true;
        // Each step runs only while no earlier step failed, so no JNI call sees a pending exception.
        const auto global_class = [&](const char* name) -> jclass {
            if (!ok) return nullptr;
            jclass local = env->FindClass(name);
            if (local == nullptr) {
                ok = false;
                return nullptr;
            }
            jobject global = env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            if (global == nullptr) {
                ok = false;
                return nullptr;
            }
            globals.push_back(global);
            return static_cast<jclass>(global);
        };
        const auto method_id = [&](jclass cls, const char* name, const char* signature, bool is_static) {
            if (!ok) return static_cast<jmethodID>(nullptr);
            const jmethodID id =
                is_static ? env->GetStaticMethodID(cls, name, signature) : env->GetMethodID(cls, name, signature);
            if (id == nullptr) ok = false;
            return id;
        };
        r.class_class = global_class("java/lang/Class");
        r.class_loader_class = global_class("java/lang/ClassLoader");
        r.method_class = global_class("java/lang/reflect/Method");
        r.executable_class = global_class("java/lang/reflect/Executable");
        r.modifier_class = global_class("java/lang/reflect/Modifier");
        r.constructor_class = global_class("java/lang/reflect/Constructor");
        r.class_not_found_class = global_class("java/lang/ClassNotFoundException");
        r.no_class_def_found_class = global_class("java/lang/NoClassDefFoundError");
        r.class_get_name = method_id(r.class_class, "getName", "()Ljava/lang/String;", false);
        r.class_is_primitive = method_id(r.class_class, "isPrimitive", "()Z", false);
        r.class_get_declared_methods =
            method_id(r.class_class, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;", false);
        r.class_loader_load_class =
            method_id(r.class_loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;", false);
        r.method_get_name = method_id(r.method_class, "getName", "()Ljava/lang/String;", false);
        r.method_get_modifiers = method_id(r.method_class, "getModifiers", "()I", false);
        r.method_get_return_type = method_id(r.method_class, "getReturnType", "()Ljava/lang/Class;", false);
        r.executable_get_parameter_types =
            method_id(r.executable_class, "getParameterTypes", "()[Ljava/lang/Class;", false);
        r.modifier_is_native = method_id(r.modifier_class, "isNative", "(I)Z", true);
        r.modifier_is_static = method_id(r.modifier_class, "isStatic", "(I)Z", true);
        if (!ok) {
            // Boot classes always resolve; treat a failure as "no reflection" without leaving
            // an exception behind for an unrelated caller.
            env->ExceptionClear();
            for (jobject global : globals) env->DeleteGlobalRef(global);
            return;
        }
        // Optional group: the long-form export path. java.lang.invoke.MethodType is API 26, which
        // the launcher requires, but a missing piece here only disables the fast path; it must not
        // disable reflection for the process. Every failure is cleared before the next lookup.
        bool long_ok = true;
        const auto optional_class = [&](const char* name) -> jclass {
            if (!long_ok) return nullptr;
            jclass local = env->FindClass(name);
            if (local == nullptr) {
                env->ExceptionClear();
                long_ok = false;
                return nullptr;
            }
            jobject global = env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            if (global == nullptr) {
                env->ExceptionClear();
                long_ok = false;
                return nullptr;
            }
            globals.push_back(global);
            return static_cast<jclass>(global);
        };
        const auto optional_method = [&](jclass cls, const char* name, const char* signature, bool is_static) {
            if (!long_ok || cls == nullptr) return static_cast<jmethodID>(nullptr);
            const jmethodID id =
                is_static ? env->GetStaticMethodID(cls, name, signature) : env->GetMethodID(cls, name, signature);
            if (id == nullptr) {
                env->ExceptionClear();
                long_ok = false;
            }
            return id;
        };
        r.method_type_class = optional_class("java/lang/invoke/MethodType");
        r.no_such_method_class = optional_class("java/lang/NoSuchMethodException");
        r.class_get_declared_method = optional_method(
            r.class_class, "getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
            false);
        r.method_type_from_descriptor =
            optional_method(r.method_type_class, "fromMethodDescriptorString",
                            "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/invoke/MethodType;", true);
        r.method_type_parameter_array =
            optional_method(r.method_type_class, "parameterArray", "()[Ljava/lang/Class;", false);
        r.long_form_ok = long_ok;
        reflection_ = r;
        reflection_ok_ = true;
    });
    return reflection_ok_ ? &reflection_ : nullptr;
}

char JniEnvBackend::type_letter(JNIEnv* env, jobject type) {
    const Reflection* r = reflection(env);
    if (r == nullptr || type == nullptr) return 0;
    std::string descriptor;
    if (!type_descriptor(env, *r, type, descriptor)) return 0;
    return descriptor[0] == '[' ? 'L' : descriptor[0];
}

bool JniEnvBackend::type_descriptor(JNIEnv* env, const Reflection& r, jobject type, std::string& out) {
    const jboolean primitive = env->CallBooleanMethod(type, r.class_is_primitive);
    if (env->ExceptionCheck()) return false;
    auto type_name = static_cast<jstring>(env->CallObjectMethod(type, r.class_get_name));
    if (env->ExceptionCheck() || type_name == nullptr) return false;
    const char* chars = env->GetStringUTFChars(type_name, nullptr);
    if (chars == nullptr) {  // OutOfMemoryError pending
        env->DeleteLocalRef(type_name);
        return false;
    }
    std::optional<std::string> descriptor = jni_type_descriptor(chars, primitive != JNI_FALSE);
    env->ReleaseStringUTFChars(type_name, chars);
    env->DeleteLocalRef(type_name);
    if (!descriptor) return false;
    out = std::move(*descriptor);
    return true;
}

bool JniEnvBackend::scan_method(JNIEnv* env, const Reflection& r, jobject method, const char* name,
                                std::vector<DeclaredNativeMethod>& methods) {
    // Cheap filters first: modifiers need no allocation, and only matching natives resolve their
    // parameter and return types.
    const jint modifiers = env->CallIntMethod(method, r.method_get_modifiers);
    if (env->ExceptionCheck()) return false;
    const jboolean is_native = env->CallStaticBooleanMethod(r.modifier_class, r.modifier_is_native, modifiers);
    if (env->ExceptionCheck()) return false;
    if (is_native == JNI_FALSE) return true;

    auto method_name = static_cast<jstring>(env->CallObjectMethod(method, r.method_get_name));
    if (env->ExceptionCheck() || method_name == nullptr) return false;
    const char* chars = env->GetStringUTFChars(method_name, nullptr);
    if (chars == nullptr) return false;  // OutOfMemoryError pending
    const bool same_name = std::strcmp(chars, name) == 0;
    env->ReleaseStringUTFChars(method_name, chars);
    env->DeleteLocalRef(method_name);
    if (!same_name) return true;

    const jboolean is_static = env->CallStaticBooleanMethod(r.modifier_class, r.modifier_is_static, modifiers);
    if (env->ExceptionCheck()) return false;
    auto parameters = static_cast<jobjectArray>(env->CallObjectMethod(method, r.executable_get_parameter_types));
    if (env->ExceptionCheck() || parameters == nullptr) return false;
    const jsize count = env->GetArrayLength(parameters);
    std::vector<std::string> parameter_descriptors;
    parameter_descriptors.reserve(static_cast<std::size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        jobject type = env->GetObjectArrayElement(parameters, i);
        if (env->ExceptionCheck() || type == nullptr) return false;
        std::string descriptor;
        const bool ok = type_descriptor(env, r, type, descriptor);
        env->DeleteLocalRef(type);
        if (!ok) return false;
        parameter_descriptors.push_back(std::move(descriptor));
    }
    env->DeleteLocalRef(parameters);

    jobject return_type = env->CallObjectMethod(method, r.method_get_return_type);
    if (env->ExceptionCheck() || return_type == nullptr) return false;
    std::string result;
    const bool ok = type_descriptor(env, r, return_type, result);
    env->DeleteLocalRef(return_type);
    if (!ok) return false;
    std::optional<std::string> signature = jni_method_descriptor(parameter_descriptors, result);
    if (!signature) return false;
    methods.push_back({std::move(*signature), is_static != JNI_FALSE});
    return true;
}

NativeLookupStatus JniEnvBackend::class_load_failure(JNIEnv* env, const Reflection& r) {
    // IsInstanceOf is not allowed with an exception pending: take the throwable out first.
    jthrowable thrown = env->ExceptionOccurred();
    env->ExceptionClear();
    if (thrown == nullptr) return NativeLookupStatus::Error;
    const bool missing = env->IsInstanceOf(thrown, r.class_not_found_class) != JNI_FALSE ||
                         env->IsInstanceOf(thrown, r.no_class_def_found_class) != JNI_FALSE;
    if (!missing) env->Throw(thrown);
    env->DeleteLocalRef(thrown);
    return missing ? NativeLookupStatus::MissingClass : NativeLookupStatus::Error;
}

jobject JniEnvBackend::class_loader() {
    std::lock_guard<std::mutex> lock(class_loader_mutex_);
    return class_loader_;
}

bool JniEnvBackend::set_class_loader(JNIEnv* env, jobject class_loader) {
    if (env == nullptr || class_loader == nullptr) return false;
    const Reflection* r = reflection(env);  // nullptr with an exception pending
    if (r == nullptr) return false;
    if (env->IsInstanceOf(class_loader, r->class_loader_class) == JNI_FALSE) return false;
    std::lock_guard<std::mutex> lock(class_loader_mutex_);
    if (class_loader_ != nullptr) return env->IsSameObject(class_loader_, class_loader) != JNI_FALSE;
    jobject global = env->NewGlobalRef(class_loader);
    if (global == nullptr) return false;  // OutOfMemoryError pending
    class_loader_ = global;
    return true;
}

NativeLookupStatus JniEnvBackend::declared_by_name(JNIEnv* e, const Reflection& r, jobject cls,
                                                   const char* name,
                                                   std::vector<DeclaredNativeMethod>& methods) {
    auto declared = static_cast<jobjectArray>(e->CallObjectMethod(cls, r.class_get_declared_methods));
    // getDeclaredMethods() resolves the parameter and return types of every declared method, so a
    // single unresolvable type anywhere in the class throws here. Skip this export, keep the library.
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        return NativeLookupStatus::Unresolvable;
    }
    if (declared == nullptr) return NativeLookupStatus::Error;
    const jsize count = e->GetArrayLength(declared);
    for (jsize i = 0; i < count; ++i) {
        // Each method gets its own frame, so no local reference of the enumeration outlives its
        // iteration.
        if (e->PushLocalFrame(8) != JNI_OK) return NativeLookupStatus::Error;  // OutOfMemoryError pending
        jobject method = e->GetObjectArrayElement(declared, i);
        const bool ok = !e->ExceptionCheck() && method != nullptr && scan_method(e, r, method, name, methods);
        e->PopLocalFrame(nullptr);
        if (!ok) {
            // A type of this one method did not resolve: same treatment, since the overloads of an
            // unreadable name cannot be bound in part.
            e->ExceptionClear();
            methods.clear();
            return NativeLookupStatus::Unresolvable;
        }
    }
    return NativeLookupStatus::Found;
}

NativeLookupStatus JniEnvBackend::declared_by_arguments(JNIEnv* e, const Reflection& r, jobject loader,
                                                        jobject cls, const char* name, const char* arguments,
                                                        std::vector<DeclaredNativeMethod>& methods) {
    // The descriptor string, the MethodType, its parameter array and elements, the method name, the
    // method and its return type.
    if (e->PushLocalFrame(16) != JNI_OK) return NativeLookupStatus::Error;  // OutOfMemoryError pending
    const auto done = [&](NativeLookupStatus status) {
        if (status != NativeLookupStatus::Found) methods.clear();
        if (status == NativeLookupStatus::Unresolvable) e->ExceptionClear();
        e->PopLocalFrame(nullptr);
        return status;
    };

    // A JNI long name encodes the parameter types and never the return type, so "(args)V" is the
    // only complete descriptor available here; only its parameters are used.
    std::string descriptor(arguments);
    descriptor += 'V';
    jstring java_descriptor = e->NewStringUTF(descriptor.c_str());
    if (java_descriptor == nullptr) return done(NativeLookupStatus::Error);
    jobject type = e->CallStaticObjectMethod(r.method_type_class, r.method_type_from_descriptor,
                                             java_descriptor, loader);
    // TypeNotPresentException, NoClassDefFoundError or IllegalArgumentException: this export names a
    // type the plugin cannot resolve.
    if (e->ExceptionCheck()) return done(NativeLookupStatus::Unresolvable);
    if (type == nullptr) return done(NativeLookupStatus::Error);
    auto parameters = static_cast<jobjectArray>(e->CallObjectMethod(type, r.method_type_parameter_array));
    if (e->ExceptionCheck() || parameters == nullptr) return done(NativeLookupStatus::Error);

    jstring java_name = e->NewStringUTF(name);
    if (java_name == nullptr) return done(NativeLookupStatus::Error);
    jobject method = e->CallObjectMethod(cls, r.class_get_declared_method, java_name, parameters);
    if (e->ExceptionCheck()) {
        // IsInstanceOf is not allowed with an exception pending: take the throwable out first.
        jthrowable thrown = e->ExceptionOccurred();
        e->ExceptionClear();
        // NoSuchMethodException means the export names no declared method at all, which stays the
        // loader's error. Anything else came from resolving a same-named overload's types.
        const bool absent =
            thrown != nullptr && e->IsInstanceOf(thrown, r.no_such_method_class) != JNI_FALSE;
        if (thrown != nullptr) e->DeleteLocalRef(thrown);
        return done(absent ? NativeLookupStatus::Found : NativeLookupStatus::Unresolvable);
    }
    if (method == nullptr) return done(NativeLookupStatus::Error);

    const jint modifiers = e->CallIntMethod(method, r.method_get_modifiers);
    if (e->ExceptionCheck()) return done(NativeLookupStatus::Error);
    const jboolean is_native = e->CallStaticBooleanMethod(r.modifier_class, r.modifier_is_native, modifiers);
    if (e->ExceptionCheck()) return done(NativeLookupStatus::Error);
    if (is_native == JNI_FALSE) return done(NativeLookupStatus::Found);  // nothing to bind
    const jboolean is_static = e->CallStaticBooleanMethod(r.modifier_class, r.modifier_is_static, modifiers);
    if (e->ExceptionCheck()) return done(NativeLookupStatus::Error);

    // The exact descriptor: the resolved parameter classes, then the method's real return type.
    std::vector<std::string> parameter_descriptors;
    const jsize count = e->GetArrayLength(parameters);
    parameter_descriptors.reserve(static_cast<std::size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        jobject parameter = e->GetObjectArrayElement(parameters, i);
        if (e->ExceptionCheck() || parameter == nullptr) return done(NativeLookupStatus::Error);
        std::string one;
        const bool ok = type_descriptor(e, r, parameter, one);
        e->DeleteLocalRef(parameter);
        if (!ok) return done(NativeLookupStatus::Unresolvable);
        parameter_descriptors.push_back(std::move(one));
    }
    jobject return_type = e->CallObjectMethod(method, r.method_get_return_type);
    if (e->ExceptionCheck()) return done(NativeLookupStatus::Unresolvable);  // return type not present
    if (return_type == nullptr) return done(NativeLookupStatus::Error);
    std::string result;
    if (!type_descriptor(e, r, return_type, result)) return done(NativeLookupStatus::Unresolvable);
    std::optional<std::string> signature = jni_method_descriptor(parameter_descriptors, result);
    if (!signature) return done(NativeLookupStatus::Error);
    methods.push_back({std::move(*signature), is_static != JNI_FALSE});
    return done(NativeLookupStatus::Found);
}

// ClassLoader.loadClass(String) on the retained plugin loader, not FindClass: FindClass from a
// proxy JNI_OnLoad or a carrier resolves through the wrong (launcher or system) loader.
NativeLookupStatus JniEnvBackend::find_declared_natives(Env env, const char* cls, const char* name,
                                                        const char* arguments, Ref& class_ref,
                                                        std::vector<DeclaredNativeMethod>& methods) {
    class_ref = 0;
    methods.clear();
    JNIEnv* e = E(env);
    if (e == nullptr || cls == nullptr || name == nullptr) return NativeLookupStatus::Error;
    const Reflection* r = reflection(e);  // nullptr with an exception pending
    const jobject loader = class_loader();
    const std::optional<std::string> binary_name = jni_binary_class_name(cls);
    if (r == nullptr || loader == nullptr || !binary_name) return NativeLookupStatus::Error;

    // Outer frame: the name string and the class.
    if (e->PushLocalFrame(8) != JNI_OK) return NativeLookupStatus::Error;  // OutOfMemoryError pending
    const auto fail = [&](NativeLookupStatus status) {
        methods.clear();
        e->PopLocalFrame(nullptr);
        return status;
    };

    jstring java_name = e->NewStringUTF(binary_name->c_str());
    if (java_name == nullptr) return fail(NativeLookupStatus::Error);
    jobject found = e->CallObjectMethod(loader, r->class_loader_load_class, java_name);
    if (e->ExceptionCheck()) return fail(class_load_failure(e, *r));
    if (found == nullptr) return fail(NativeLookupStatus::Error);

    // A long-form export binds exactly one method, so it never enumerates: that is the whole point
    // of the split. A short-form export binds every native overload of the name and has to.
    const NativeLookupStatus status =
        arguments != nullptr && r->long_form_ok
            ? declared_by_arguments(e, *r, loader, found, name, arguments, methods)
            : declared_by_name(e, *r, found, name, methods);
    if (status != NativeLookupStatus::Found) return fail(status);
    class_ref = R(e->PopLocalFrame(found));
    return class_ref != 0 ? NativeLookupStatus::Found : NativeLookupStatus::Error;
}

JniBackend::Ref JniEnvBackend::find_class(Env env, const char* name) {
    JNIEnv* e = E(env);
    if (e == nullptr || name == nullptr) return 0;
    const jobject loader = class_loader();
    const std::optional<std::string> binary_name = jni_binary_class_name(name);
    if (loader == nullptr || !binary_name) return R(e->FindClass(name));
    const Reflection* r = reflection(e);
    if (r == nullptr) return 0;
    jstring java_name = e->NewStringUTF(binary_name->c_str());
    if (java_name == nullptr) return 0;
    jobject found = e->CallObjectMethod(loader, r->class_loader_load_class, java_name);
    e->DeleteLocalRef(java_name);
    return R(found);
}

JniBackend::Ref JniEnvBackend::get_superclass(Env env, Ref cls) {
    return R(E(env)->GetSuperclass(C(cls)));
}

bool JniEnvBackend::is_assignable_from(Env env, Ref from, Ref to) {
    return E(env)->IsAssignableFrom(C(from), C(to)) == JNI_TRUE;
}

JniBackend::Id JniEnvBackend::get_method_id(Env env, Ref cls, const char* name, const char* signature,
                                            bool is_static) {
    return I(is_static ? E(env)->GetStaticMethodID(C(cls), name, signature)
                       : E(env)->GetMethodID(C(cls), name, signature));
}

JniBackend::Id JniEnvBackend::get_field_id(Env env, Ref cls, const char* name, const char* signature,
                                           bool is_static) {
    return I(is_static ? E(env)->GetStaticFieldID(C(cls), name, signature)
                       : E(env)->GetFieldID(C(cls), name, signature));
}

// The descriptor is rebuilt from reflection with every reference type erased to Object, which
// keeps the shorty exact.
JniBackend::Id JniEnvBackend::from_reflected_method(Env env, Ref method, std::string& signature) {
    JNIEnv* e = E(env);
    const Reflection* r = reflection(e);
    const jmethodID id = e->FromReflectedMethod(O(method));
    if (id == nullptr || r == nullptr) return 0;
    const auto letter_descriptor = [](char letter) {
        return letter == 'L' ? std::string("Ljava/lang/Object;") : std::string(1, letter);
    };
    std::string descriptor = "(";
    auto parameters = static_cast<jobjectArray>(e->CallObjectMethod(O(method), r->executable_get_parameter_types));
    if (parameters == nullptr) return 0;
    const jsize count = e->GetArrayLength(parameters);
    for (jsize i = 0; i < count; ++i) {
        jobject type = e->GetObjectArrayElement(parameters, i);
        const char letter = type_letter(e, type);
        e->DeleteLocalRef(type);
        if (letter == 0 || letter == 'V') {
            e->DeleteLocalRef(parameters);
            return 0;
        }
        descriptor += letter_descriptor(letter);
    }
    e->DeleteLocalRef(parameters);
    char result = 'V';
    if (!e->IsInstanceOf(O(method), r->constructor_class)) {
        jobject type = e->CallObjectMethod(O(method), r->method_get_return_type);
        result = type_letter(e, type);
        e->DeleteLocalRef(type);
        if (result == 0) return 0;
    }
    signature = descriptor + ")" + (result == 'V' ? std::string("V") : letter_descriptor(result));
    return I(id);
}

JniBackend::Id JniEnvBackend::from_reflected_field(Env env, Ref field) {
    return I(E(env)->FromReflectedField(O(field)));
}

JniBackend::Ref JniEnvBackend::to_reflected_method(Env env, Ref cls, Id method, bool is_static) {
    return R(E(env)->ToReflectedMethod(C(cls), M(method), is_static ? JNI_TRUE : JNI_FALSE));
}

JniBackend::Ref JniEnvBackend::to_reflected_field(Env env, Ref cls, Id field, bool is_static) {
    return R(E(env)->ToReflectedField(C(cls), F(field), is_static ? JNI_TRUE : JNI_FALSE));
}

JniBackend::Ref JniEnvBackend::alloc_object(Env env, Ref cls) {
    return R(E(env)->AllocObject(C(cls)));
}

JniBackend::Ref JniEnvBackend::get_object_class(Env env, Ref obj) {
    return R(E(env)->GetObjectClass(O(obj)));
}

bool JniEnvBackend::is_instance_of(Env env, Ref obj, Ref cls) {
    return E(env)->IsInstanceOf(O(obj), C(cls)) == JNI_TRUE;
}

bool JniEnvBackend::is_same_object(Env env, Ref a, Ref b) {
    return E(env)->IsSameObject(O(a), O(b)) == JNI_TRUE;
}

JValue JniEnvBackend::call_method(Env env, JniCallKind kind, char type, Ref obj, Ref cls, Id method,
                                  const JValue* args) {
    JNIEnv* e = E(env);
    const auto* a = reinterpret_cast<const jvalue*>(args);
    const jmethodID m = M(method);
    JValue out{};
#define ZB_CALL(Name, member, wrap)                                                       \
    switch (kind) {                                                                       \
    case JniCallKind::Virtual: member wrap(e->Call##Name##MethodA(O(obj), m, a)); break;  \
    case JniCallKind::Nonvirtual:                                                         \
        member wrap(e->CallNonvirtual##Name##MethodA(O(obj), C(cls), m, a));              \
        break;                                                                            \
    case JniCallKind::Static: member wrap(e->CallStatic##Name##MethodA(C(cls), m, a)); break; \
    case JniCallKind::NewObject: break;                                                   \
    }
    if (kind == JniCallKind::NewObject) {
        out.l = R(e->NewObjectA(C(cls), m, a));
        return out;
    }
    switch (type) {
    case 'Z': ZB_CALL(Boolean, out.z =, ) break;
    case 'B': ZB_CALL(Byte, out.b =, ) break;
    case 'C': ZB_CALL(Char, out.c =, ) break;
    case 'S': ZB_CALL(Short, out.s =, ) break;
    case 'I': ZB_CALL(Int, out.i =, ) break;
    case 'J': ZB_CALL(Long, out.j =, ) break;
    case 'F': ZB_CALL(Float, out.f =, ) break;
    case 'D': ZB_CALL(Double, out.d =, ) break;
    case 'L': ZB_CALL(Object, out.l =, R) break;
    default: ZB_CALL(Void, , ) break;
    }
#undef ZB_CALL
    return out;
}

JValue JniEnvBackend::get_field(Env env, bool is_static, char type, Ref obj, Id field) {
    JNIEnv* e = E(env);
    const jfieldID f = F(field);
    JValue out{};
#define ZB_GET(Name, member, wrap)                                                      \
    member wrap(is_static ? e->GetStatic##Name##Field(C(obj), f) : e->Get##Name##Field(O(obj), f))
    switch (type) {
    case 'Z': ZB_GET(Boolean, out.z =, ); break;
    case 'B': ZB_GET(Byte, out.b =, ); break;
    case 'C': ZB_GET(Char, out.c =, ); break;
    case 'S': ZB_GET(Short, out.s =, ); break;
    case 'I': ZB_GET(Int, out.i =, ); break;
    case 'J': ZB_GET(Long, out.j =, ); break;
    case 'F': ZB_GET(Float, out.f =, ); break;
    case 'D': ZB_GET(Double, out.d =, ); break;
    default: ZB_GET(Object, out.l =, R); break;
    }
#undef ZB_GET
    return out;
}

void JniEnvBackend::set_field(Env env, bool is_static, char type, Ref obj, Id field, JValue value) {
    JNIEnv* e = E(env);
    const jfieldID f = F(field);
#define ZB_SET(Name, v)                                                                 \
    if (is_static) {                                                                    \
        e->SetStatic##Name##Field(C(obj), f, v);                                        \
    } else {                                                                            \
        e->Set##Name##Field(O(obj), f, v);                                              \
    }
    switch (type) {
    case 'Z': ZB_SET(Boolean, value.z) break;
    case 'B': ZB_SET(Byte, value.b) break;
    case 'C': ZB_SET(Char, value.c) break;
    case 'S': ZB_SET(Short, value.s) break;
    case 'I': ZB_SET(Int, value.i) break;
    case 'J': ZB_SET(Long, value.j) break;
    case 'F': ZB_SET(Float, value.f) break;
    case 'D': ZB_SET(Double, value.d) break;
    default: ZB_SET(Object, O(value.l)) break;
    }
#undef ZB_SET
}

JniBackend::Ref JniEnvBackend::new_string(Env env, const std::uint16_t* chars, std::int32_t length) {
    // Guest buffers may be unaligned for jchar.
    if (length > 0 && (reinterpret_cast<std::uintptr_t>(chars) & 1u) != 0) {
        std::vector<jchar> copy(static_cast<std::size_t>(length));
        std::memcpy(copy.data(), chars, copy.size() * sizeof(jchar));
        return R(E(env)->NewString(copy.data(), length));
    }
    return R(E(env)->NewString(chars, length));
}

JniBackend::Ref JniEnvBackend::new_string_utf(Env env, const char* utf) {
    return R(E(env)->NewStringUTF(utf));
}

std::int32_t JniEnvBackend::get_string_length(Env env, Ref str) {
    return E(env)->GetStringLength(static_cast<jstring>(O(str)));
}

std::int32_t JniEnvBackend::get_string_utf_length(Env env, Ref str) {
    return E(env)->GetStringUTFLength(static_cast<jstring>(O(str)));
}

void JniEnvBackend::get_string_region(Env env, Ref str, std::int32_t start, std::int32_t length, void* out) {
    if (length > 0 && (reinterpret_cast<std::uintptr_t>(out) & 1u) != 0) {
        std::vector<jchar> copy(static_cast<std::size_t>(length));
        E(env)->GetStringRegion(static_cast<jstring>(O(str)), start, length, copy.data());
        if (!E(env)->ExceptionCheck()) std::memcpy(out, copy.data(), copy.size() * sizeof(jchar));
        return;
    }
    E(env)->GetStringRegion(static_cast<jstring>(O(str)), start, length, static_cast<jchar*>(out));
}

bool JniEnvBackend::get_string_utf_region(Env env, Ref str, std::int32_t start, std::int32_t length,
                                          std::string& out) {
    JNIEnv* e = E(env);
    // Modified UTF-8 needs at most 3 bytes per UTF-16 unit and never contains a zero byte.
    std::vector<char> buffer(length > 0 ? 3 * static_cast<std::size_t>(length) + 1 : 1, '\0');
    e->GetStringUTFRegion(static_cast<jstring>(O(str)), start, length, buffer.data());
    if (e->ExceptionCheck()) return false;
    out.assign(buffer.data());
    return true;
}

std::int32_t JniEnvBackend::get_array_length(Env env, Ref array) {
    return E(env)->GetArrayLength(static_cast<jarray>(O(array)));
}

char JniEnvBackend::get_array_element_type(Env env, Ref array) {
    JNIEnv* e = E(env);
    const Reflection* r = reflection(e);
    if (r == nullptr || array == 0) return 0;
    jclass cls = e->GetObjectClass(O(array));
    auto name = static_cast<jstring>(e->CallObjectMethod(cls, r->class_get_name));
    e->DeleteLocalRef(cls);
    if (name == nullptr) return 0;
    const char* chars = e->GetStringUTFChars(name, nullptr);
    char letter = 0;
    if (chars != nullptr && chars[0] == '[') letter = chars[1] == '[' || chars[1] == 'L' ? 'L' : chars[1];
    if (chars != nullptr) e->ReleaseStringUTFChars(name, chars);
    e->DeleteLocalRef(name);
    return letter;
}

JniBackend::Ref JniEnvBackend::new_object_array(Env env, std::int32_t length, Ref element_class, Ref initial) {
    return R(E(env)->NewObjectArray(length, C(element_class), O(initial)));
}

JniBackend::Ref JniEnvBackend::get_object_array_element(Env env, Ref array, std::int32_t index) {
    return R(E(env)->GetObjectArrayElement(static_cast<jobjectArray>(O(array)), index));
}

void JniEnvBackend::set_object_array_element(Env env, Ref array, std::int32_t index, Ref value) {
    E(env)->SetObjectArrayElement(static_cast<jobjectArray>(O(array)), index, O(value));
}

JniBackend::Ref JniEnvBackend::new_primitive_array(Env env, char type, std::int32_t length) {
    JNIEnv* e = E(env);
    switch (type) {
    case 'Z': return R(e->NewBooleanArray(length));
    case 'B': return R(e->NewByteArray(length));
    case 'C': return R(e->NewCharArray(length));
    case 'S': return R(e->NewShortArray(length));
    case 'I': return R(e->NewIntArray(length));
    case 'J': return R(e->NewLongArray(length));
    case 'F': return R(e->NewFloatArray(length));
    case 'D': return R(e->NewDoubleArray(length));
    default: return 0;
    }
}

// ART copies array regions with memcpy, so unaligned guest buffers are fine.
void JniEnvBackend::get_primitive_array_region(Env env, char type, Ref array, std::int32_t start,
                                               std::int32_t length, void* out) {
    JNIEnv* e = E(env);
    switch (type) {
    case 'Z': e->GetBooleanArrayRegion(static_cast<jbooleanArray>(O(array)), start, length, static_cast<jboolean*>(out)); break;
    case 'B': e->GetByteArrayRegion(static_cast<jbyteArray>(O(array)), start, length, static_cast<jbyte*>(out)); break;
    case 'C': e->GetCharArrayRegion(static_cast<jcharArray>(O(array)), start, length, static_cast<jchar*>(out)); break;
    case 'S': e->GetShortArrayRegion(static_cast<jshortArray>(O(array)), start, length, static_cast<jshort*>(out)); break;
    case 'I': e->GetIntArrayRegion(static_cast<jintArray>(O(array)), start, length, static_cast<jint*>(out)); break;
    case 'J': e->GetLongArrayRegion(static_cast<jlongArray>(O(array)), start, length, static_cast<jlong*>(out)); break;
    case 'F': e->GetFloatArrayRegion(static_cast<jfloatArray>(O(array)), start, length, static_cast<jfloat*>(out)); break;
    case 'D': e->GetDoubleArrayRegion(static_cast<jdoubleArray>(O(array)), start, length, static_cast<jdouble*>(out)); break;
    default: break;
    }
}

void JniEnvBackend::set_primitive_array_region(Env env, char type, Ref array, std::int32_t start,
                                               std::int32_t length, const void* in) {
    JNIEnv* e = E(env);
    switch (type) {
    case 'Z': e->SetBooleanArrayRegion(static_cast<jbooleanArray>(O(array)), start, length, static_cast<const jboolean*>(in)); break;
    case 'B': e->SetByteArrayRegion(static_cast<jbyteArray>(O(array)), start, length, static_cast<const jbyte*>(in)); break;
    case 'C': e->SetCharArrayRegion(static_cast<jcharArray>(O(array)), start, length, static_cast<const jchar*>(in)); break;
    case 'S': e->SetShortArrayRegion(static_cast<jshortArray>(O(array)), start, length, static_cast<const jshort*>(in)); break;
    case 'I': e->SetIntArrayRegion(static_cast<jintArray>(O(array)), start, length, static_cast<const jint*>(in)); break;
    case 'J': e->SetLongArrayRegion(static_cast<jlongArray>(O(array)), start, length, static_cast<const jlong*>(in)); break;
    case 'F': e->SetFloatArrayRegion(static_cast<jfloatArray>(O(array)), start, length, static_cast<const jfloat*>(in)); break;
    case 'D': e->SetDoubleArrayRegion(static_cast<jdoubleArray>(O(array)), start, length, static_cast<const jdouble*>(in)); break;
    default: break;
    }
}

JniBackend::Ref JniEnvBackend::new_global_ref(Env env, Ref obj) {
    return R(E(env)->NewGlobalRef(O(obj)));
}

void JniEnvBackend::delete_global_ref(Env env, Ref ref) {
    E(env)->DeleteGlobalRef(O(ref));
}

JniBackend::Ref JniEnvBackend::new_weak_global_ref(Env env, Ref obj) {
    return R(E(env)->NewWeakGlobalRef(O(obj)));
}

void JniEnvBackend::delete_weak_global_ref(Env env, Ref ref) {
    E(env)->DeleteWeakGlobalRef(static_cast<jweak>(O(ref)));
}

JniBackend::Ref JniEnvBackend::new_local_ref(Env env, Ref obj) {
    return R(E(env)->NewLocalRef(O(obj)));
}

void JniEnvBackend::delete_local_ref(Env env, Ref ref) {
    E(env)->DeleteLocalRef(O(ref));
}

std::int32_t JniEnvBackend::ensure_local_capacity(Env env, std::int32_t capacity) {
    return E(env)->EnsureLocalCapacity(capacity);
}

std::int32_t JniEnvBackend::push_local_frame(Env env, std::int32_t capacity) {
    return E(env)->PushLocalFrame(capacity);
}

JniBackend::Ref JniEnvBackend::pop_local_frame(Env env, Ref result) {
    return R(E(env)->PopLocalFrame(O(result)));
}

std::int32_t JniEnvBackend::throw_exception(Env env, Ref throwable) {
    return E(env)->Throw(static_cast<jthrowable>(O(throwable)));
}

std::int32_t JniEnvBackend::throw_new(Env env, Ref cls, const char* message) {
    return E(env)->ThrowNew(C(cls), message);
}

JniBackend::Ref JniEnvBackend::exception_occurred(Env env) {
    return R(E(env)->ExceptionOccurred());
}

void JniEnvBackend::exception_describe(Env env) {
    E(env)->ExceptionDescribe();
}

void JniEnvBackend::exception_clear(Env env) {
    E(env)->ExceptionClear();
}

bool JniEnvBackend::exception_check(Env env) {
    return E(env)->ExceptionCheck() == JNI_TRUE;
}

void JniEnvBackend::fatal_error(Env env, const char* message) {
    E(env)->FatalError(message);
}

std::int32_t JniEnvBackend::monitor_enter(Env env, Ref obj) {
    return E(env)->MonitorEnter(O(obj));
}

std::int32_t JniEnvBackend::monitor_exit(Env env, Ref obj) {
    return E(env)->MonitorExit(O(obj));
}

std::int32_t JniEnvBackend::register_native(Env env, Ref cls, const char* name, const char* signature,
                                            void* function) {
    const JNINativeMethod method = {const_cast<char*>(name), const_cast<char*>(signature), function};
    return E(env)->RegisterNatives(C(cls), &method, 1);
}

std::int32_t JniEnvBackend::unregister_natives(Env env, Ref cls) {
    return E(env)->UnregisterNatives(C(cls));
}

JniBackend::Ref JniEnvBackend::new_direct_byte_buffer(Env env, void* address, std::int64_t capacity) {
    return R(E(env)->NewDirectByteBuffer(address, capacity));
}

void* JniEnvBackend::get_direct_buffer_address(Env env, Ref buffer) {
    return E(env)->GetDirectBufferAddress(O(buffer));
}

std::int64_t JniEnvBackend::get_direct_buffer_capacity(Env env, Ref buffer) {
    return E(env)->GetDirectBufferCapacity(O(buffer));
}

JniBackend::Env JniEnvBackend::attach_current_thread(bool daemon, const char* name, Ref group) {
    JavaVMAttachArgs args = {JNI_VERSION_1_6, const_cast<char*>(name), O(group)};
    JNIEnv* env = nullptr;
#if defined(__ANDROID__)
    const jint rc = daemon ? vm_->AttachCurrentThreadAsDaemon(&env, &args) : vm_->AttachCurrentThread(&env, &args);
#else
    auto out = reinterpret_cast<void**>(&env);
    const jint rc = daemon ? vm_->AttachCurrentThreadAsDaemon(out, &args) : vm_->AttachCurrentThread(out, &args);
#endif
    return rc == JNI_OK ? static_cast<Env>(reinterpret_cast<std::uintptr_t>(env)) : 0;
}

std::int32_t JniEnvBackend::detach_current_thread() {
    return vm_->DetachCurrentThread();
}

}  // namespace zb
