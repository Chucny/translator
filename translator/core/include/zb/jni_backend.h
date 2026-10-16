#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zb {

// A JNI value as the backend sees it: the layout of host jvalue (8 bytes). References are host
// references in `l`.
union JValue {
    std::uint8_t z;
    std::int8_t b;
    std::uint16_t c;
    std::int16_t s;
    std::int32_t i;
    std::int64_t j;
    float f;
    double d;
    // Keep every byte deterministic for Java's zero-initialized fields and for
    // narrow values later read through another JNI union member.
    std::uint64_t l = 0;
};

enum class JniCallKind : std::uint32_t { Virtual = 0, Nonvirtual = 1, Static = 2, NewObject = 3 };

struct DeclaredNativeMethod {
    std::string signature;
    bool is_static = false;
};

enum class NativeLookupStatus {
    Found,
    MissingClass,
    // The class loaded, but the declared methods this export needs could not be resolved: a type
    // named by the export, or by a sibling method the enumeration touched, is not present. The
    // loader skips that one export and logs it once; it never fails the library.
    Unresolvable,
    Error,
};

// The Java side of the JNI bridge. HostJni translates guest handles and guest memory and calls
// exactly one backend function per guest JNI operation. Implementations: the real JNIEnv (Android
// build, core/android/jni_env_backend.cpp) and the mock JVM of the host tests.
//
// Env is the host JNIEnv* of the calling host thread. Refs are host references (0 is null) and
// Ids are host jmethodID/jfieldID values, all opaque. `type` arguments are JNI descriptor letters
// with 'L' for every reference type. Failures leave a pending Java exception, as in JNI.
class JniBackend {
public:
    using Env = std::uint64_t;
    using Ref = std::uint64_t;
    using Id = std::uint64_t;

    virtual ~JniBackend() = default;

    // Classes, member ids, reflection.
    virtual Ref find_class(Env env, const char* name) = 0;
    virtual Ref get_superclass(Env env, Ref cls) = 0;
    virtual bool is_assignable_from(Env env, Ref from, Ref to) = 0;
    virtual Id get_method_id(Env env, Ref cls, const char* name, const char* signature, bool is_static) = 0;
    virtual Id get_field_id(Env env, Ref cls, const char* name, const char* signature, bool is_static) = 0;
    // signature receives a descriptor with the method's shorty (reference types may be erased).
    virtual Id from_reflected_method(Env env, Ref method, std::string& signature) = 0;
    virtual Id from_reflected_field(Env env, Ref field) = 0;
    virtual Ref to_reflected_method(Env env, Ref cls, Id method, bool is_static) = 0;
    virtual Ref to_reflected_field(Env env, Ref cls, Id field, bool is_static) = 0;
    // Loader-only reflection seam. Found returns a local class reference and every declared
    // native with the requested name. MissingClass must clear only the expected class-not-found
    // exception. Android supplies the plugin-scoped implementation in Phase 4d Task 3.
    // `arguments` is the argument part of the descriptor of a long-form export ("(ILjava/lang/String;)"),
    // or nullptr for a short-form export, which binds every native overload of that name. Long-form
    // lookups resolve only the types they name; short-form lookups must enumerate.
    virtual NativeLookupStatus find_declared_natives(Env, const char*, const char*, const char*, Ref& cls,
                                                     std::vector<DeclaredNativeMethod>& methods) {
        cls = 0;
        methods.clear();
        return NativeLookupStatus::Error;
    }

    // Objects.
    virtual Ref alloc_object(Env env, Ref cls) = 0;
    virtual Ref get_object_class(Env env, Ref obj) = 0;
    virtual bool is_instance_of(Env env, Ref obj, Ref cls) = 0;
    virtual bool is_same_object(Env env, Ref a, Ref b) = 0;

    // Calls and fields. call_method with NewObject returns the new object in `l`.
    virtual JValue call_method(Env env, JniCallKind kind, char type, Ref obj, Ref cls, Id method,
                               const JValue* args) = 0;
    virtual JValue get_field(Env env, bool is_static, char type, Ref obj, Id field) = 0;
    virtual void set_field(Env env, bool is_static, char type, Ref obj, Id field, JValue value) = 0;

    // Strings. Buffers may be unaligned. get_string_utf_region returns false when it threw.
    virtual Ref new_string(Env env, const std::uint16_t* chars, std::int32_t length) = 0;
    virtual Ref new_string_utf(Env env, const char* utf) = 0;
    virtual std::int32_t get_string_length(Env env, Ref str) = 0;
    virtual std::int32_t get_string_utf_length(Env env, Ref str) = 0;
    virtual void get_string_region(Env env, Ref str, std::int32_t start, std::int32_t length, void* out) = 0;
    virtual bool get_string_utf_region(Env env, Ref str, std::int32_t start, std::int32_t length,
                                       std::string& out) = 0;

    // Arrays. get_array_element_type is the element letter ('L' for object arrays), 0 on failure.
    virtual std::int32_t get_array_length(Env env, Ref array) = 0;
    virtual char get_array_element_type(Env env, Ref array) = 0;
    virtual Ref new_object_array(Env env, std::int32_t length, Ref element_class, Ref initial) = 0;
    virtual Ref get_object_array_element(Env env, Ref array, std::int32_t index) = 0;
    virtual void set_object_array_element(Env env, Ref array, std::int32_t index, Ref value) = 0;
    virtual Ref new_primitive_array(Env env, char type, std::int32_t length) = 0;
    virtual void get_primitive_array_region(Env env, char type, Ref array, std::int32_t start, std::int32_t length,
                                            void* out) = 0;
    virtual void set_primitive_array_region(Env env, char type, Ref array, std::int32_t start, std::int32_t length,
                                            const void* in) = 0;

    // References and local frames.
    virtual Ref new_global_ref(Env env, Ref obj) = 0;
    virtual void delete_global_ref(Env env, Ref ref) = 0;
    virtual Ref new_weak_global_ref(Env env, Ref obj) = 0;
    virtual void delete_weak_global_ref(Env env, Ref ref) = 0;
    virtual Ref new_local_ref(Env env, Ref obj) = 0;
    virtual void delete_local_ref(Env env, Ref ref) = 0;
    virtual std::int32_t ensure_local_capacity(Env env, std::int32_t capacity) = 0;
    virtual std::int32_t push_local_frame(Env env, std::int32_t capacity) = 0;
    virtual Ref pop_local_frame(Env env, Ref result) = 0;

    // Exceptions. fatal_error does not return.
    virtual std::int32_t throw_exception(Env env, Ref throwable) = 0;
    virtual std::int32_t throw_new(Env env, Ref cls, const char* message) = 0;
    virtual Ref exception_occurred(Env env) = 0;
    virtual void exception_describe(Env env) = 0;
    virtual void exception_clear(Env env) = 0;
    virtual bool exception_check(Env env) = 0;
    virtual void fatal_error(Env env, const char* message) = 0;

    // Monitors, native registration, direct buffers.
    virtual std::int32_t monitor_enter(Env env, Ref obj) = 0;
    virtual std::int32_t monitor_exit(Env env, Ref obj) = 0;
    // One method per call: 0 on success, a negative JNI error otherwise.
    virtual std::int32_t register_native(Env env, Ref cls, const char* name, const char* signature,
                                         void* function) = 0;
    virtual std::int32_t unregister_natives(Env env, Ref cls) = 0;
    virtual Ref new_direct_byte_buffer(Env env, void* address, std::int64_t capacity) = 0;
    virtual void* get_direct_buffer_address(Env env, Ref buffer) = 0;
    virtual std::int64_t get_direct_buffer_capacity(Env env, Ref buffer) = 0;

    // Threads. attach_current_thread returns the JNIEnv of the calling host thread, 0 on failure.
    virtual Env attach_current_thread(bool daemon, const char* name, Ref group) = 0;
    virtual std::int32_t detach_current_thread() = 0;
};

// Serves a library the guest asked Java to load (`System.load(path)`) through the guest runtime
// instead of ART: a translated plugin's libraries are arm32, and the path the guest builds names a
// directory of the original package that this process does not have, so ART can only report
// "not found". Returns true when the call was served, false to let it reach ART unchanged (with
// the reason in `error` when the library is not one of the active plugin's).
using GuestLibraryLoader =
    std::function<bool(JniBackend::Env env, const std::string& path, std::string& error)>;

}  // namespace zb
