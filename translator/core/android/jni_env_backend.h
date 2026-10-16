#pragma once

#include <jni.h>

#include <mutex>
#include <string>

#include "zb/jni_backend.h"

namespace zb {

// JniBackend over the real JNIEnv / JavaVM of the app process (Android build only). Env values are
// host JNIEnv*, Refs are jobject, Ids are jmethodID / jfieldID. Each function is one JNI call (a few
// use reflection), so failures leave ART's pending exception exactly as native code would.
class JniEnvBackend final : public JniBackend {
public:
    explicit JniEnvBackend(JavaVM* vm);

    Ref find_class(Env env, const char* name) override;
    Ref get_superclass(Env env, Ref cls) override;
    bool is_assignable_from(Env env, Ref from, Ref to) override;
    Id get_method_id(Env env, Ref cls, const char* name, const char* signature, bool is_static) override;
    Id get_field_id(Env env, Ref cls, const char* name, const char* signature, bool is_static) override;
    Id from_reflected_method(Env env, Ref method, std::string& signature) override;
    Id from_reflected_field(Env env, Ref field) override;
    Ref to_reflected_method(Env env, Ref cls, Id method, bool is_static) override;
    Ref to_reflected_field(Env env, Ref cls, Id field, bool is_static) override;
    NativeLookupStatus find_declared_natives(Env env, const char* cls, const char* name, const char* arguments,
                                             Ref& class_ref,
                                             std::vector<DeclaredNativeMethod>& methods) override;
    Ref alloc_object(Env env, Ref cls) override;
    Ref get_object_class(Env env, Ref obj) override;
    bool is_instance_of(Env env, Ref obj, Ref cls) override;
    bool is_same_object(Env env, Ref a, Ref b) override;
    JValue call_method(Env env, JniCallKind kind, char type, Ref obj, Ref cls, Id method,
                       const JValue* args) override;
    JValue get_field(Env env, bool is_static, char type, Ref obj, Id field) override;
    void set_field(Env env, bool is_static, char type, Ref obj, Id field, JValue value) override;
    Ref new_string(Env env, const std::uint16_t* chars, std::int32_t length) override;
    Ref new_string_utf(Env env, const char* utf) override;
    std::int32_t get_string_length(Env env, Ref str) override;
    std::int32_t get_string_utf_length(Env env, Ref str) override;
    void get_string_region(Env env, Ref str, std::int32_t start, std::int32_t length, void* out) override;
    bool get_string_utf_region(Env env, Ref str, std::int32_t start, std::int32_t length, std::string& out) override;
    std::int32_t get_array_length(Env env, Ref array) override;
    char get_array_element_type(Env env, Ref array) override;
    Ref new_object_array(Env env, std::int32_t length, Ref element_class, Ref initial) override;
    Ref get_object_array_element(Env env, Ref array, std::int32_t index) override;
    void set_object_array_element(Env env, Ref array, std::int32_t index, Ref value) override;
    Ref new_primitive_array(Env env, char type, std::int32_t length) override;
    void get_primitive_array_region(Env env, char type, Ref array, std::int32_t start, std::int32_t length,
                                    void* out) override;
    void set_primitive_array_region(Env env, char type, Ref array, std::int32_t start, std::int32_t length,
                                    const void* in) override;
    Ref new_global_ref(Env env, Ref obj) override;
    void delete_global_ref(Env env, Ref ref) override;
    Ref new_weak_global_ref(Env env, Ref obj) override;
    void delete_weak_global_ref(Env env, Ref ref) override;
    Ref new_local_ref(Env env, Ref obj) override;
    void delete_local_ref(Env env, Ref ref) override;
    std::int32_t ensure_local_capacity(Env env, std::int32_t capacity) override;
    std::int32_t push_local_frame(Env env, std::int32_t capacity) override;
    Ref pop_local_frame(Env env, Ref result) override;
    std::int32_t throw_exception(Env env, Ref throwable) override;
    std::int32_t throw_new(Env env, Ref cls, const char* message) override;
    Ref exception_occurred(Env env) override;
    void exception_describe(Env env) override;
    void exception_clear(Env env) override;
    bool exception_check(Env env) override;
    void fatal_error(Env env, const char* message) override;
    std::int32_t monitor_enter(Env env, Ref obj) override;
    std::int32_t monitor_exit(Env env, Ref obj) override;
    std::int32_t register_native(Env env, Ref cls, const char* name, const char* signature, void* function) override;
    std::int32_t unregister_natives(Env env, Ref cls) override;
    Ref new_direct_byte_buffer(Env env, void* address, std::int64_t capacity) override;
    void* get_direct_buffer_address(Env env, Ref buffer) override;
    std::int64_t get_direct_buffer_capacity(Env env, Ref buffer) override;
    Env attach_current_thread(bool daemon, const char* name, Ref group) override;
    std::int32_t detach_current_thread() override;

    // Retains the one active plugin loader as a global reference. A different loader cannot
    // replace it in the process-lifetime backend.
    bool set_class_loader(JNIEnv* env, jobject class_loader);

private:
    // Reflection classes and ids, looked up once. Classes are global references to boot classes,
    // which are never unloaded, so they and their method ids stay valid for the process.
    struct Reflection {
        jclass class_class = nullptr;               // java.lang.Class
        jclass class_loader_class = nullptr;        // java.lang.ClassLoader
        jclass method_class = nullptr;              // java.lang.reflect.Method
        jclass executable_class = nullptr;          // java.lang.reflect.Executable
        jclass modifier_class = nullptr;            // java.lang.reflect.Modifier
        jclass constructor_class = nullptr;         // java.lang.reflect.Constructor
        jclass class_not_found_class = nullptr;     // java.lang.ClassNotFoundException
        jclass no_class_def_found_class = nullptr;  // java.lang.NoClassDefFoundError
        jclass method_type_class = nullptr;         // java.lang.invoke.MethodType (API 26)
        jclass no_such_method_class = nullptr;      // java.lang.NoSuchMethodException
        jmethodID class_get_name = nullptr;               // Class.getName()
        jmethodID class_is_primitive = nullptr;           // Class.isPrimitive()
        jmethodID class_get_declared_methods = nullptr;   // Class.getDeclaredMethods()
        jmethodID class_loader_load_class = nullptr;      // ClassLoader.loadClass(String)
        jmethodID method_get_name = nullptr;              // Method.getName()
        jmethodID method_get_modifiers = nullptr;         // Method.getModifiers()
        jmethodID method_get_return_type = nullptr;       // Method.getReturnType()
        jmethodID executable_get_parameter_types = nullptr;  // Executable.getParameterTypes()
        jmethodID modifier_is_native = nullptr;           // static Modifier.isNative(int)
        jmethodID modifier_is_static = nullptr;           // static Modifier.isStatic(int)
        jmethodID class_get_declared_method = nullptr;    // Class.getDeclaredMethod(String, Class[])
        // static MethodType.fromMethodDescriptorString(String, ClassLoader)
        jmethodID method_type_from_descriptor = nullptr;
        jmethodID method_type_parameter_array = nullptr;  // MethodType.parameterArray()
        // False when java.lang.invoke.MethodType is unavailable (below API 26). Long-form exports
        // then fall back to enumeration, which skips instead of failing on an unresolvable type.
        bool long_form_ok = false;
    };
    // nullptr when the lookup failed or an exception is pending (the lookup is never consumed then).
    const Reflection* reflection(JNIEnv* env);
    // Descriptor letter of a java.lang.Class ('L' for references and arrays); 0 on failure.
    char type_letter(JNIEnv* env, jobject type);
    // Exact JNI descriptor of a java.lang.Class; false on failure (an exception may be pending).
    bool type_descriptor(JNIEnv* env, const Reflection& r, jobject type, std::string& out);
    // Appends `method` when it is native and named `name`. False on failure. Local references it
    // leaves behind belong to the caller's per-method local frame.
    bool scan_method(JNIEnv* env, const Reflection& r, jobject method, const char* name,
                     std::vector<DeclaredNativeMethod>& methods);
    // Short-form discovery: every declared native called `name`. getDeclaredMethods() resolves the
    // types of every declared method, so one unresolvable type anywhere in the class throws here;
    // that is Unresolvable (skip this export), with the exception cleared, never Error.
    NativeLookupStatus declared_by_name(JNIEnv* env, const Reflection& r, jobject cls, const char* name,
                                        std::vector<DeclaredNativeMethod>& methods);
    // Long-form discovery: the single method whose parameters are `arguments` ("(ILjava/lang/String;)").
    // A JNI long name encodes no return type, so the complete descriptor cannot be spelled here:
    // MethodType.fromMethodDescriptorString(arguments + "V", loader) resolves exactly the classes
    // this one signature names, Class.getDeclaredMethod picks the method, and its real return type
    // completes the descriptor. Nothing else in the class is touched.
    NativeLookupStatus declared_by_arguments(JNIEnv* env, const Reflection& r, jobject loader, jobject cls,
                                             const char* name, const char* arguments,
                                             std::vector<DeclaredNativeMethod>& methods);
    // Called with the loadClass exception pending: clears it and reports MissingClass only for
    // ClassNotFoundException / NoClassDefFoundError, otherwise rethrows it and reports Error.
    NativeLookupStatus class_load_failure(JNIEnv* env, const Reflection& r);
    jobject class_loader();

    JavaVM* vm_;
    std::once_flag reflection_once_;
    Reflection reflection_;
    bool reflection_ok_ = false;
    std::mutex class_loader_mutex_;
    jobject class_loader_ = nullptr;  // global reference, retained for the process
};

}  // namespace zb
