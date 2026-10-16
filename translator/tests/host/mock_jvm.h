#pragma once

// A toy Java VM behind JniBackend for the host tests: classes with methods and fields, strings,
// primitive and object arrays, exceptions, monitors, direct buffers, reflection objects, native
// registration, attached threads, and local/global/weak references with ART-like frames. It
// checks JNI discipline (wrong-thread JNIEnv, stale or deleted references, calls with a pending
// exception) and records every violation in errors().
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "zb/jni_backend.h"

namespace zb::mock {

// Exit status of a process ended through fatal_error.
inline constexpr int kFatalExitStatus = 86;

class MockJvm;

// One Java method invocation. `self` is the receiver object id, or the class object for a static
// method. References in `args` are object ids; a body returns references as object ids too.
struct MockCall {
    MockJvm& vm;
    JniBackend::Env env;
    std::uint64_t self;
    std::vector<JValue> args;
};

using Body = std::function<JValue(MockCall& call)>;

class MockJvm final : public JniBackend {
public:
    using ObjectId = std::uint64_t;  // 0 is null

    MockJvm();

    // Model setup. Class names use the JNI form ("zb/Probe", "[I").
    void define_class(const std::string& name, const std::string& super = "java/lang/Object");
    void add_method(const std::string& cls, const std::string& name, const std::string& signature, bool is_static,
                    Body body);
    void add_native(const std::string& cls, const std::string& name, const std::string& signature, bool is_static);
    void add_field(const std::string& cls, const std::string& name, const std::string& signature, bool is_static);
    // Makes the next RegisterNatives call for signature fail with NoSuchMethodError.
    void fail_native_registration(const std::string& signature);
    // Makes short-form find_declared_natives on cls report `status`, standing in for an ART class
    // whose getDeclaredMethods() throws (Unresolvable) or fails outright (Error).
    void fail_declared_enumeration(const std::string& cls, NativeLookupStatus status);
    // Makes long-form find_declared_natives report Unresolvable whenever the requested arguments
    // name this type descriptor, standing in for a parameter class the plugin cannot load.
    void fail_type_resolution(const std::string& descriptor);

    // Direct object access for tests and method bodies.
    ObjectId class_object(const std::string& name);
    ObjectId new_object(const std::string& cls);
    ObjectId new_string_object(const std::u16string& text);
    ObjectId new_direct_buffer_object(void* address, std::int64_t capacity);
    // Test-only array metadata without a potentially enormous backing allocation.
    ObjectId new_sparse_primitive_array(char type, std::int32_t length);
    std::u16string string_value(ObjectId string);
    std::string class_name_of(ObjectId obj);
    // Instance field of obj, or static field when obj is a class object.
    JValue field_value(ObjectId obj, const std::string& name);
    void set_field_value(ObjectId obj, const std::string& name, JValue value);
    // Clears every weak global reference to obj, like a garbage collection.
    void collect(ObjectId obj);
    // Makes cls(message) the pending exception of env.
    void throw_in(Env env, const std::string& cls, const std::string& message);
    ObjectId pending_exception(Env env);
    void clear_pending_exception(Env env);
    // The function registered for a native method, or nullptr.
    void* native_function(const std::string& cls, const std::string& name, const std::string& signature);

    // A host thread becomes a Java thread through thread_env(); other threads attach.
    Env thread_env();
    std::string thread_name(Env env);
    bool thread_daemon(Env env);
    std::size_t attached_threads();

    std::size_t live_local_refs(Env env);
    std::size_t live_global_refs();
    std::size_t live_weak_refs();
    std::vector<std::string> errors();

    // The native frame ART opens for a call from Java into a native method. Local references
    // created while it is open belong to it and die when it closes.
    class NativeFrame {
    public:
        NativeFrame(MockJvm& vm, Env env);
        ~NativeFrame();
        NativeFrame(const NativeFrame&) = delete;
        NativeFrame& operator=(const NativeFrame&) = delete;
        // A local reference argument for the native method. Like ART, DeleteLocalRef on it is an
        // error (ART logs "failed to find entry") and leaves it live.
        Ref local(ObjectId obj);
        // Decodes the reference a native method returned and deletes it.
        ObjectId result(Ref ref);
        // Closes the frame; returns how many of its references were still live.
        std::size_t close();

    private:
        MockJvm& vm_;
        Env env_;
        std::size_t depth_;
        bool open_ = true;
    };

    // JniBackend.
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

private:
    enum class RefKind { Local, Global, Weak };
    struct RefEntry {
        ObjectId object = 0;
        RefKind kind = RefKind::Local;
        Env env = 0;
        bool live = false;
        bool argument = false;  // created by NativeFrame::local, like an ART JNI transition reference
    };
    struct Method {
        std::string cls, name, signature;
        bool is_static = false;
        bool is_native = false;
        Body body;
        void* function = nullptr;
    };
    struct Field {
        std::string cls, name, signature;
        bool is_static = false;
        JValue value{};
    };
    struct Class {
        std::string name, super;
        ObjectId object = 0;
    };
    struct Object {
        std::string cls;         // runtime class
        std::string class_name;  // java/lang/Class objects: the class they stand for
        std::map<std::size_t, JValue> fields;
        std::u16string text;     // strings and throwable messages
        char element = 0;        // arrays: element letter, 'L' for object arrays
        std::int32_t length = 0;
        std::vector<std::uint8_t> bytes;
        std::vector<ObjectId> elements;
        void* address = nullptr;
        std::int64_t capacity = -1;
        bool direct = false;
        std::size_t reflected = 0;  // reflection objects: method or field index + 1
        bool reflected_field = false;
        std::thread::id owner;
        int monitor_count = 0;
    };
    struct Thread {
        std::string name;
        bool daemon = false;
        std::vector<std::vector<std::size_t>> frames;
        ObjectId exception = 0;
    };

    void error_locked(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    Thread* thread_locked(Env env, const char* function);
    Class* class_locked(const std::string& name);
    Class* class_ref_locked(Env env, Ref cls, const char* function);
    ObjectId new_object_locked(const std::string& cls);
    Ref new_local_locked(Env env, ObjectId obj);
    Ref new_ref_locked(ObjectId obj, RefKind kind, Env env);
    RefEntry* entry_locked(Ref ref);
    ObjectId object_locked(Env env, Ref ref, const char* function);
    Object* array_locked(Env env, Ref array, const char* function);
    Method* method_locked(Id id, const char* function);
    Field* field_locked(Id id, const char* function);
    bool is_subclass_locked(std::string cls, const std::string& target);
    void throw_locked(Env env, const std::string& cls, const std::string& message);
    std::size_t field_index_locked(ObjectId obj, const std::string& name, bool& is_static);
    Env attach_locked(bool daemon, const std::string& name);
    std::size_t pop_frame_locked(Thread& thread);

    std::mutex mutex_;
    std::condition_variable monitor_cv_;
    std::map<std::string, Class> classes_;
    std::deque<Method> methods_;
    std::deque<Field> fields_;
    std::deque<Object> objects_;
    std::deque<RefEntry> refs_;
    std::unordered_map<Env, Thread> threads_;
    std::unordered_map<std::uint64_t, Env> thread_envs_;
    Env next_env_ = 0xE000;
    std::vector<std::string> errors_;
    std::optional<std::string> failed_registration_;
    std::map<std::string, NativeLookupStatus> failed_enumerations_;
    std::vector<std::string> unresolvable_types_;
};

}  // namespace zb::mock
