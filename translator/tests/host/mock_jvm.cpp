#include "mock_jvm.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "zb/jni_shorty.h"

namespace zb::mock {

namespace {

constexpr std::uint64_t kRefBase = 0x100000000000ull;
constexpr std::uint64_t kMethodBase = 0x200000000000ull;
constexpr std::uint64_t kFieldBase = 0x300000000000ull;

std::uint64_t current_thread_token() {
    static std::atomic<std::uint64_t> next{1};
    thread_local const std::uint64_t token = next.fetch_add(1, std::memory_order_relaxed);
    return token;
}

std::u16string decode_utf(const char* utf) {
    std::u16string out;
    const auto* p = reinterpret_cast<const unsigned char*>(utf);
    while (*p != 0) {
        const unsigned c = *p;
        if (c < 0x80) {
            out.push_back(static_cast<char16_t>(c));
            p += 1;
        } else if ((c & 0xE0) == 0xC0 && p[1] != 0) {
            out.push_back(static_cast<char16_t>(((c & 0x1F) << 6) | (p[1] & 0x3F)));
            p += 2;
        } else if ((c & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
            out.push_back(static_cast<char16_t>(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)));
            p += 3;
        } else if ((c & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
            const unsigned point =
                (((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F)) - 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (point >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (point & 0x3FF)));
            p += 4;
        } else {
            out.push_back(u'\uFFFD');
            p += 1;
        }
    }
    return out;
}

// Modified UTF-8: NUL is C0 80 and every UTF-16 unit, surrogates included, is encoded alone.
std::string encode_utf(const char16_t* chars, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned c = chars[i];
        if (c != 0 && c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::size_t element_size(char type) {
    switch (type) {
    case 'Z':
    case 'B':
        return 1;
    case 'C':
    case 'S':
        return 2;
    case 'I':
    case 'F':
        return 4;
    case 'J':
    case 'D':
        return 8;
    default:
        return 0;
    }
}

char type_letter(const std::string& descriptor) {
    return descriptor.empty() ? 0 : (descriptor[0] == '[' ? 'L' : descriptor[0]);
}

char return_letter(const std::string& signature) {
    const std::size_t close = signature.find(')');
    return close == std::string::npos ? 0 : type_letter(signature.substr(close + 1));
}

}  // namespace

MockJvm::MockJvm() {
    classes_["java/lang/Object"] = Class{"java/lang/Object", "", 0};
    define_class("java/lang/Class");
    define_class("java/lang/String");
    define_class("java/lang/Throwable");
    define_class("java/lang/Exception", "java/lang/Throwable");
    define_class("java/lang/RuntimeException", "java/lang/Exception");
    define_class("java/lang/Error", "java/lang/Throwable");
    define_class("java/lang/NoSuchMethodError", "java/lang/Error");
    define_class("java/lang/NoSuchFieldError", "java/lang/Error");
    define_class("java/lang/NoClassDefFoundError", "java/lang/Error");
    define_class("java/lang/IllegalArgumentException", "java/lang/RuntimeException");
    define_class("java/lang/NullPointerException", "java/lang/RuntimeException");
    define_class("java/lang/NegativeArraySizeException", "java/lang/RuntimeException");
    define_class("java/lang/ArrayIndexOutOfBoundsException", "java/lang/RuntimeException");
    define_class("java/lang/StringIndexOutOfBoundsException", "java/lang/RuntimeException");
    define_class("java/lang/ThreadGroup");
    define_class("java/lang/reflect/Method");
    define_class("java/lang/reflect/Field");
    define_class("java/nio/Buffer");
    define_class("java/nio/ByteBuffer", "java/nio/Buffer");
    define_class("java/nio/DirectByteBuffer", "java/nio/ByteBuffer");
    std::lock_guard<std::mutex> lock(mutex_);
    Class& object = classes_["java/lang/Object"];
    object.object = new_object_locked("java/lang/Class");
    objects_[object.object - 1].class_name = "java/lang/Object";
}

void MockJvm::error_locked(const char* fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "mock JNI error: %s\n", text);
    errors_.emplace_back(text);
}

void MockJvm::define_class(const std::string& name, const std::string& super) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (classes_.count(name) != 0) return;
    ObjectId object = new_object_locked("java/lang/Class");
    objects_[object - 1].class_name = name;
    classes_[name] = Class{name, super, object};
}

void MockJvm::add_method(const std::string& cls, const std::string& name, const std::string& signature,
                         bool is_static, Body body) {
    std::lock_guard<std::mutex> lock(mutex_);
    Method method;
    method.cls = cls;
    method.name = name;
    method.signature = signature;
    method.is_static = is_static;
    method.body = std::move(body);
    methods_.push_back(std::move(method));
}

void MockJvm::add_native(const std::string& cls, const std::string& name, const std::string& signature,
                         bool is_static) {
    std::lock_guard<std::mutex> lock(mutex_);
    Method method;
    method.cls = cls;
    method.name = name;
    method.signature = signature;
    method.is_static = is_static;
    method.is_native = true;
    methods_.push_back(std::move(method));
}

void MockJvm::add_field(const std::string& cls, const std::string& name, const std::string& signature,
                        bool is_static) {
    std::lock_guard<std::mutex> lock(mutex_);
    Field field;
    field.cls = cls;
    field.name = name;
    field.signature = signature;
    field.is_static = is_static;
    fields_.push_back(field);
}

void MockJvm::fail_declared_enumeration(const std::string& cls, NativeLookupStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    failed_enumerations_[cls] = status;
}

void MockJvm::fail_type_resolution(const std::string& descriptor) {
    std::lock_guard<std::mutex> lock(mutex_);
    unresolvable_types_.push_back(descriptor);
}

void MockJvm::fail_native_registration(const std::string& signature) {
    std::lock_guard<std::mutex> lock(mutex_);
    failed_registration_ = signature;
}

MockJvm::Class* MockJvm::class_locked(const std::string& name) {
    auto it = classes_.find(name);
    if (it != classes_.end()) return &it->second;
    if (name.size() < 2 || name[0] != '[') return nullptr;
    const std::string element = name.substr(1);
    const bool valid = element_size(element[0]) != 0 ? element.size() == 1
                       : element[0] == '['     ? class_locked(element) != nullptr
                       : element[0] == 'L' && element.back() == ';' &&
                             class_locked(element.substr(1, element.size() - 2)) != nullptr;
    if (!valid) return nullptr;
    ObjectId object = new_object_locked("java/lang/Class");
    objects_[object - 1].class_name = name;
    return &(classes_[name] = Class{name, "java/lang/Object", object});
}

MockJvm::ObjectId MockJvm::new_object_locked(const std::string& cls) {
    Object object;
    object.cls = cls;
    objects_.push_back(std::move(object));
    return objects_.size();
}

MockJvm::Ref MockJvm::new_ref_locked(ObjectId obj, RefKind kind, Env env) {
    if (obj == 0) return 0;
    RefEntry entry;
    entry.object = obj;
    entry.kind = kind;
    entry.env = env;
    entry.live = true;
    refs_.push_back(entry);
    return kRefBase + ((refs_.size() - 1) << 4);
}

MockJvm::Ref MockJvm::new_local_locked(Env env, ObjectId obj) {
    if (obj == 0) return 0;
    Thread* thread = thread_locked(env, "new local reference");
    if (thread == nullptr) return 0;
    const Ref ref = new_ref_locked(obj, RefKind::Local, env);
    if (thread->frames.empty()) thread->frames.emplace_back();
    thread->frames.back().push_back((ref - kRefBase) >> 4);
    return ref;
}

MockJvm::RefEntry* MockJvm::entry_locked(Ref ref) {
    if (ref < kRefBase || ((ref - kRefBase) & 0xF) != 0) return nullptr;
    const std::uint64_t index = (ref - kRefBase) >> 4;
    return index < refs_.size() ? &refs_[index] : nullptr;
}

MockJvm::Thread* MockJvm::thread_locked(Env env, const char* function) {
    const auto current = thread_envs_.find(current_thread_token());
    if (current == thread_envs_.end() || current->second != env) {
        error_locked("%s: JNIEnv 0x%llx is not the JNIEnv of the calling thread", function,
                     static_cast<unsigned long long>(env));
        return nullptr;
    }
    return &threads_[env];
}

MockJvm::ObjectId MockJvm::object_locked(Env env, Ref ref, const char* function) {
    if (ref == 0) return 0;
    RefEntry* entry = entry_locked(ref);
    if (entry == nullptr || !entry->live) {
        error_locked("%s: invalid or deleted reference 0x%llx", function, static_cast<unsigned long long>(ref));
        return 0;
    }
    if (entry->kind == RefKind::Local && entry->env != env) {
        error_locked("%s: local reference 0x%llx used on another thread", function,
                     static_cast<unsigned long long>(ref));
        return 0;
    }
    return entry->object;
}

MockJvm::Class* MockJvm::class_ref_locked(Env env, Ref cls, const char* function) {
    const ObjectId id = object_locked(env, cls, function);
    if (id == 0 || objects_[id - 1].cls != "java/lang/Class") {
        error_locked("%s: 0x%llx is not a class", function, static_cast<unsigned long long>(cls));
        return nullptr;
    }
    return class_locked(objects_[id - 1].class_name);
}

MockJvm::Object* MockJvm::array_locked(Env env, Ref array, const char* function) {
    const ObjectId id = object_locked(env, array, function);
    if (id == 0 || objects_[id - 1].element == 0) {
        error_locked("%s: 0x%llx is not an array", function, static_cast<unsigned long long>(array));
        return nullptr;
    }
    return &objects_[id - 1];
}

MockJvm::Method* MockJvm::method_locked(Id id, const char* function) {
    const std::uint64_t index = (id - kMethodBase) / 8;
    if (id < kMethodBase || (id - kMethodBase) % 8 != 0 || index >= methods_.size()) {
        error_locked("%s: invalid method id 0x%llx", function, static_cast<unsigned long long>(id));
        return nullptr;
    }
    return &methods_[index];
}

MockJvm::Field* MockJvm::field_locked(Id id, const char* function) {
    const std::uint64_t index = (id - kFieldBase) / 8;
    if (id < kFieldBase || (id - kFieldBase) % 8 != 0 || index >= fields_.size()) {
        error_locked("%s: invalid field id 0x%llx", function, static_cast<unsigned long long>(id));
        return nullptr;
    }
    return &fields_[index];
}

bool MockJvm::is_subclass_locked(std::string cls, const std::string& target) {
    if (target == "java/lang/Object") return true;
    while (!cls.empty()) {
        if (cls == target) return true;
        const auto it = classes_.find(cls);
        if (it == classes_.end()) return false;
        cls = it->second.super;
    }
    return false;
}

void MockJvm::throw_locked(Env env, const std::string& cls, const std::string& message) {
    const ObjectId exception = new_object_locked(cls);
    objects_[exception - 1].text = decode_utf(message.c_str());
    threads_[env].exception = exception;
}

MockJvm::ObjectId MockJvm::class_object(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* cls = class_locked(name);
    return cls != nullptr ? cls->object : 0;
}

MockJvm::ObjectId MockJvm::new_object(const std::string& cls) {
    std::lock_guard<std::mutex> lock(mutex_);
    return new_object_locked(cls);
}

MockJvm::ObjectId MockJvm::new_string_object(const std::u16string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = new_object_locked("java/lang/String");
    objects_[id - 1].text = text;
    return id;
}

MockJvm::ObjectId MockJvm::new_direct_buffer_object(void* address, std::int64_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = new_object_locked("java/nio/DirectByteBuffer");
    objects_[id - 1].direct = true;
    objects_[id - 1].address = address;
    objects_[id - 1].capacity = capacity;
    return id;
}

std::u16string MockJvm::string_value(ObjectId string) {
    std::lock_guard<std::mutex> lock(mutex_);
    return string == 0 ? std::u16string() : objects_[string - 1].text;
}

std::string MockJvm::class_name_of(ObjectId obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    return obj == 0 ? std::string() : objects_[obj - 1].cls;
}

std::size_t MockJvm::field_index_locked(ObjectId obj, const std::string& name, bool& is_static) {
    const Object& object = objects_[obj - 1];
    is_static = object.cls == "java/lang/Class";
    std::string cls = is_static ? object.class_name : object.cls;
    while (!cls.empty()) {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].cls == cls && fields_[i].name == name && fields_[i].is_static == is_static) return i;
        }
        cls = classes_[cls].super;
    }
    error_locked("no field %s", name.c_str());
    return fields_.size();
}

JValue MockJvm::field_value(ObjectId obj, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool is_static = false;
    const std::size_t index = field_index_locked(obj, name, is_static);
    if (index == fields_.size()) return JValue{};
    return is_static ? fields_[index].value : objects_[obj - 1].fields[index];
}

void MockJvm::set_field_value(ObjectId obj, const std::string& name, JValue value) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool is_static = false;
    const std::size_t index = field_index_locked(obj, name, is_static);
    if (index == fields_.size()) return;
    if (is_static) {
        fields_[index].value = value;
    } else {
        objects_[obj - 1].fields[index] = value;
    }
}

void MockJvm::collect(ObjectId obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (RefEntry& entry : refs_) {
        if (entry.live && entry.kind == RefKind::Weak && entry.object == obj) entry.object = 0;
    }
}

void MockJvm::throw_in(Env env, const std::string& cls, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    throw_locked(env, cls, message);
}

MockJvm::ObjectId MockJvm::pending_exception(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_[env].exception;
}

void MockJvm::clear_pending_exception(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    threads_[env].exception = 0;
}

void* MockJvm::native_function(const std::string& cls, const std::string& name, const std::string& signature) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Method& method : methods_) {
        if (method.is_native && method.cls == cls && method.name == name && method.signature == signature) {
            return method.function;
        }
    }
    return nullptr;
}

MockJvm::Env MockJvm::attach_locked(bool daemon, const std::string& name) {
    const std::uint64_t token = current_thread_token();
    const auto it = thread_envs_.find(token);
    if (it != thread_envs_.end()) return it->second;
    const Env env = next_env_;
    next_env_ += 0x100;
    thread_envs_[token] = env;
    Thread& thread = threads_[env];
    thread.name = name;
    thread.daemon = daemon;
    return env;
}

MockJvm::Env MockJvm::thread_env() {
    std::lock_guard<std::mutex> lock(mutex_);
    return attach_locked(false, "java");
}

std::string MockJvm::thread_name(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.count(env) != 0 ? threads_[env].name : std::string();
}

bool MockJvm::thread_daemon(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.count(env) != 0 && threads_[env].daemon;
}

std::size_t MockJvm::attached_threads() {
    std::lock_guard<std::mutex> lock(mutex_);
    return thread_envs_.size();
}

std::size_t MockJvm::live_local_refs(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const RefEntry& entry : refs_) count += entry.live && entry.kind == RefKind::Local && entry.env == env;
    return count;
}

std::size_t MockJvm::live_global_refs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const RefEntry& entry : refs_) count += entry.live && entry.kind == RefKind::Global;
    return count;
}

std::size_t MockJvm::live_weak_refs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const RefEntry& entry : refs_) count += entry.live && entry.kind == RefKind::Weak;
    return count;
}

std::vector<std::string> MockJvm::errors() {
    std::lock_guard<std::mutex> lock(mutex_);
    return errors_;
}

std::size_t MockJvm::pop_frame_locked(Thread& thread) {
    std::size_t live = 0;
    for (const std::size_t index : thread.frames.back()) {
        if (refs_[index].live) {
            refs_[index].live = false;
            ++live;
        }
    }
    thread.frames.pop_back();
    return live;
}

MockJvm::NativeFrame::NativeFrame(MockJvm& vm, Env env) : vm_(vm), env_(env) {
    std::lock_guard<std::mutex> lock(vm_.mutex_);
    Thread* thread = vm_.thread_locked(env_, "NativeFrame");
    if (thread == nullptr) std::abort();
    thread->frames.emplace_back();
    depth_ = thread->frames.size();
}

MockJvm::NativeFrame::~NativeFrame() {
    close();
}

MockJvm::Ref MockJvm::NativeFrame::local(ObjectId obj) {
    std::lock_guard<std::mutex> lock(vm_.mutex_);
    const Ref ref = vm_.new_local_locked(env_, obj);
    if (RefEntry* entry = vm_.entry_locked(ref)) entry->argument = true;
    return ref;
}

MockJvm::ObjectId MockJvm::NativeFrame::result(Ref ref) {
    std::lock_guard<std::mutex> lock(vm_.mutex_);
    const ObjectId obj = vm_.object_locked(env_, ref, "native method result");
    if (RefEntry* entry = vm_.entry_locked(ref)) entry->live = false;
    return obj;
}

std::size_t MockJvm::NativeFrame::close() {
    if (!open_) return 0;
    open_ = false;
    std::lock_guard<std::mutex> lock(vm_.mutex_);
    Thread& thread = vm_.threads_[env_];
    if (thread.frames.size() != depth_) {
        vm_.error_locked("native frame closed with %zu frames above it", thread.frames.size() - depth_);
    }
    std::size_t live = 0;
    while (thread.frames.size() >= depth_) live += vm_.pop_frame_locked(thread);
    return live;
}

MockJvm::Ref MockJvm::find_class(Env env, const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_locked(env, "FindClass") == nullptr) return 0;
    Class* cls = class_locked(name);
    if (cls == nullptr) {
        throw_locked(env, "java/lang/NoClassDefFoundError", name);
        return 0;
    }
    return new_local_locked(env, cls->object);
}

MockJvm::Ref MockJvm::get_superclass(Env env, Ref cls) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "GetSuperclass");
    if (c == nullptr || c->super.empty()) return 0;
    return new_local_locked(env, class_locked(c->super)->object);
}

bool MockJvm::is_assignable_from(Env env, Ref from, Ref to) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* a = class_ref_locked(env, from, "IsAssignableFrom");
    Class* b = class_ref_locked(env, to, "IsAssignableFrom");
    return a != nullptr && b != nullptr && is_subclass_locked(a->name, b->name);
}

MockJvm::Id MockJvm::get_method_id(Env env, Ref cls, const char* name, const char* signature, bool is_static) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "GetMethodID");
    if (c == nullptr) return 0;
    for (std::string owner = c->name; !owner.empty(); owner = classes_[owner].super) {
        for (std::size_t i = 0; i < methods_.size(); ++i) {
            const Method& m = methods_[i];
            if (m.cls == owner && m.name == name && m.signature == signature && m.is_static == is_static) {
                return kMethodBase + 8 * i;
            }
        }
        if (std::strcmp(name, "<init>") == 0) break;
    }
    throw_locked(env, "java/lang/NoSuchMethodError", std::string(name) + signature);
    return 0;
}

MockJvm::Id MockJvm::get_field_id(Env env, Ref cls, const char* name, const char* signature, bool is_static) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "GetFieldID");
    if (c == nullptr) return 0;
    for (std::string owner = c->name; !owner.empty(); owner = classes_[owner].super) {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            const Field& f = fields_[i];
            if (f.cls == owner && f.name == name && f.signature == signature && f.is_static == is_static) {
                return kFieldBase + 8 * i;
            }
        }
    }
    throw_locked(env, "java/lang/NoSuchFieldError", name);
    return 0;
}

MockJvm::Id MockJvm::from_reflected_method(Env env, Ref method, std::string& signature) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, method, "FromReflectedMethod");
    if (id == 0 || objects_[id - 1].reflected == 0 || objects_[id - 1].reflected_field) {
        error_locked("FromReflectedMethod: not a method object");
        return 0;
    }
    const std::size_t index = objects_[id - 1].reflected - 1;
    signature = methods_[index].signature;
    return kMethodBase + 8 * index;
}

MockJvm::Id MockJvm::from_reflected_field(Env env, Ref field) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, field, "FromReflectedField");
    if (id == 0 || objects_[id - 1].reflected == 0 || !objects_[id - 1].reflected_field) {
        error_locked("FromReflectedField: not a field object");
        return 0;
    }
    return kFieldBase + 8 * (objects_[id - 1].reflected - 1);
}

MockJvm::Ref MockJvm::to_reflected_method(Env env, Ref cls, Id method, bool is_static) {
    std::lock_guard<std::mutex> lock(mutex_);
    Method* m = method_locked(method, "ToReflectedMethod");
    if (class_ref_locked(env, cls, "ToReflectedMethod") == nullptr || m == nullptr) return 0;
    if (m->is_static != is_static) error_locked("ToReflectedMethod: wrong isStatic for %s", m->name.c_str());
    const ObjectId id = new_object_locked("java/lang/reflect/Method");
    objects_[id - 1].reflected = static_cast<std::size_t>((method - kMethodBase) / 8) + 1;
    return new_local_locked(env, id);
}

MockJvm::Ref MockJvm::to_reflected_field(Env env, Ref cls, Id field, bool is_static) {
    std::lock_guard<std::mutex> lock(mutex_);
    Field* f = field_locked(field, "ToReflectedField");
    if (class_ref_locked(env, cls, "ToReflectedField") == nullptr || f == nullptr) return 0;
    if (f->is_static != is_static) error_locked("ToReflectedField: wrong isStatic for %s", f->name.c_str());
    const ObjectId id = new_object_locked("java/lang/reflect/Field");
    objects_[id - 1].reflected = static_cast<std::size_t>((field - kFieldBase) / 8) + 1;
    objects_[id - 1].reflected_field = true;
    return new_local_locked(env, id);
}

NativeLookupStatus MockJvm::find_declared_natives(Env env, const char* cls, const char* name,
                                                  const char* arguments, Ref& class_ref,
                                                  std::vector<DeclaredNativeMethod>& methods) {
    std::lock_guard<std::mutex> lock(mutex_);
    class_ref = 0;
    methods.clear();
    if (thread_locked(env, "find declared natives") == nullptr) return NativeLookupStatus::Error;
    Class* found = class_locked(cls);
    if (found == nullptr) return NativeLookupStatus::MissingClass;
    // Injected failures are reported before the class reference exists, so a skipped export leaks
    // nothing.
    if (arguments == nullptr) {
        const auto failure = failed_enumerations_.find(found->name);
        if (failure != failed_enumerations_.end()) return failure->second;
    } else {
        for (const std::string& type : unresolvable_types_) {
            if (std::string(arguments).find(type) != std::string::npos) return NativeLookupStatus::Unresolvable;
        }
    }
    class_ref = new_local_locked(env, found->object);
    if (class_ref == 0) return NativeLookupStatus::Error;
    for (const Method& method : methods_) {
        if (method.cls != found->name || method.name != name || !method.is_native) continue;
        // Long form resolves one signature, like Class.getDeclaredMethod; short form enumerates.
        if (arguments != nullptr && method.signature.compare(0, std::strlen(arguments), arguments) != 0) continue;
        methods.push_back({method.signature, method.is_static});
    }
    return NativeLookupStatus::Found;
}

MockJvm::Ref MockJvm::alloc_object(Env env, Ref cls) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "AllocObject");
    if (c == nullptr) return 0;
    return new_local_locked(env, new_object_locked(c->name));
}

MockJvm::Ref MockJvm::get_object_class(Env env, Ref obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, obj, "GetObjectClass");
    if (id == 0) {
        error_locked("GetObjectClass: null object");
        return 0;
    }
    return new_local_locked(env, class_locked(objects_[id - 1].cls)->object);
}

bool MockJvm::is_instance_of(Env env, Ref obj, Ref cls) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, obj, "IsInstanceOf");
    Class* c = class_ref_locked(env, cls, "IsInstanceOf");
    if (c == nullptr) return false;
    return id == 0 || is_subclass_locked(objects_[id - 1].cls, c->name);
}

bool MockJvm::is_same_object(Env env, Ref a, Ref b) {
    std::lock_guard<std::mutex> lock(mutex_);
    return object_locked(env, a, "IsSameObject") == object_locked(env, b, "IsSameObject");
}

JValue MockJvm::call_method(Env env, JniCallKind kind, char type, Ref obj, Ref cls, Id method, const JValue* args) {
    Body body;
    MockCall call{*this, env, 0, {}};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Thread* thread = thread_locked(env, "CallMethodA");
        Method* m = method_locked(method, "CallMethodA");
        if (thread == nullptr || m == nullptr) return JValue{};
        if (thread->exception != 0) {
            error_locked("CallMethodA %s: called with a pending exception", m->name.c_str());
            return JValue{};
        }
        const char declared = return_letter(m->signature);
        if (kind == JniCallKind::NewObject) {
            Class* c = class_ref_locked(env, cls, "NewObject");
            if (c == nullptr) return JValue{};
            if (m->name != "<init>" || type != 'L') error_locked("NewObject: %s is not a constructor", m->name.c_str());
            call.self = new_object_locked(c->name);
        } else if (kind == JniCallKind::Static) {
            if (!m->is_static) error_locked("CallStatic*Method on instance method %s", m->name.c_str());
            call.self = class_locked(m->cls)->object;
        } else {
            if (m->is_static) error_locked("Call*Method on static method %s", m->name.c_str());
            call.self = object_locked(env, obj, "CallMethodA");
            if (call.self == 0) {
                throw_locked(env, "java/lang/NullPointerException", "null receiver");
                return JValue{};
            }
            if (kind == JniCallKind::Nonvirtual) class_ref_locked(env, cls, "CallNonvirtualMethodA");
            if (kind == JniCallKind::Virtual) {
                for (std::string owner = objects_[call.self - 1].cls; !owner.empty(); owner = classes_[owner].super) {
                    Method* found = nullptr;
                    for (Method& candidate : methods_) {
                        if (candidate.cls == owner && candidate.name == m->name &&
                            candidate.signature == m->signature && !candidate.is_static) {
                            found = &candidate;
                            break;
                        }
                    }
                    if (found != nullptr) {
                        m = found;
                        break;
                    }
                }
            }
        }
        if (kind != JniCallKind::NewObject && declared != type) {
            error_locked("Call*Method %s: result type %c, method returns %c", m->name.c_str(), type, declared);
        }
        if (m->is_native) {
            error_locked("the mock cannot call native method %s through JNI", m->name.c_str());
            return JValue{};
        }
        const std::optional<std::string> shorty = shorty_from_signature(m->signature);
        for (std::size_t i = 1; i < shorty->size(); ++i) {
            JValue value = args[i - 1];
            if ((*shorty)[i] == 'L') value.l = object_locked(env, value.l, "CallMethodA argument");
            call.args.push_back(value);
        }
        body = m->body;
    }
    JValue result = body(call);
    std::lock_guard<std::mutex> lock(mutex_);
    if (threads_[env].exception != 0) return JValue{};
    if (kind == JniCallKind::NewObject) {
        JValue created{};
        created.l = new_local_locked(env, call.self);
        return created;
    }
    if (type == 'L') result.l = new_local_locked(env, result.l);
    return result;
}

JValue MockJvm::get_field(Env env, bool is_static, char type, Ref obj, Id field) {
    std::lock_guard<std::mutex> lock(mutex_);
    Field* f = field_locked(field, "GetField");
    if (thread_locked(env, "GetField") == nullptr || f == nullptr) return JValue{};
    if (f->is_static != is_static || type_letter(f->signature) != type) {
        error_locked("Get*Field %s: wrong static flag or type %c", f->name.c_str(), type);
    }
    JValue value{};
    if (is_static) {
        class_ref_locked(env, obj, "GetStaticField");
        value = f->value;
    } else {
        const ObjectId id = object_locked(env, obj, "GetField");
        if (id == 0) {
            error_locked("GetField %s: null object", f->name.c_str());
            return JValue{};
        }
        value = objects_[id - 1].fields[static_cast<std::size_t>((field - kFieldBase) / 8)];
    }
    if (type == 'L') value.l = new_local_locked(env, value.l);
    return value;
}

void MockJvm::set_field(Env env, bool is_static, char type, Ref obj, Id field, JValue value) {
    std::lock_guard<std::mutex> lock(mutex_);
    Field* f = field_locked(field, "SetField");
    if (thread_locked(env, "SetField") == nullptr || f == nullptr) return;
    if (f->is_static != is_static || type_letter(f->signature) != type) {
        error_locked("Set*Field %s: wrong static flag or type %c", f->name.c_str(), type);
    }
    if (type == 'L') value.l = object_locked(env, value.l, "SetField value");
    if (is_static) {
        class_ref_locked(env, obj, "SetStaticField");
        f->value = value;
        return;
    }
    const ObjectId id = object_locked(env, obj, "SetField");
    if (id == 0) {
        error_locked("SetField %s: null object", f->name.c_str());
        return;
    }
    objects_[id - 1].fields[static_cast<std::size_t>((field - kFieldBase) / 8)] = value;
}

MockJvm::Ref MockJvm::new_string(Env env, const std::uint16_t* chars, std::int32_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_locked(env, "NewString") == nullptr) return 0;
    if (length < 0) {
        error_locked("NewString: negative length");
        return 0;
    }
    std::u16string text(static_cast<std::size_t>(length), u'\0');
    if (length > 0) std::memcpy(text.data(), chars, static_cast<std::size_t>(length) * 2);
    const ObjectId id = new_object_locked("java/lang/String");
    objects_[id - 1].text = std::move(text);
    return new_local_locked(env, id);
}

MockJvm::Ref MockJvm::new_string_utf(Env env, const char* utf) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_locked(env, "NewStringUTF") == nullptr) return 0;
    const ObjectId id = new_object_locked("java/lang/String");
    objects_[id - 1].text = decode_utf(utf);
    return new_local_locked(env, id);
}

std::int32_t MockJvm::get_string_length(Env env, Ref str) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, str, "GetStringLength");
    return id == 0 ? 0 : static_cast<std::int32_t>(objects_[id - 1].text.size());
}

std::int32_t MockJvm::get_string_utf_length(Env env, Ref str) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, str, "GetStringUTFLength");
    if (id == 0) return 0;
    const std::u16string& text = objects_[id - 1].text;
    return static_cast<std::int32_t>(encode_utf(text.data(), text.size()).size());
}

void MockJvm::get_string_region(Env env, Ref str, std::int32_t start, std::int32_t length, void* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, str, "GetStringRegion");
    if (id == 0) return;
    const std::u16string& text = objects_[id - 1].text;
    if (start < 0 || length < 0 || static_cast<std::size_t>(start) + static_cast<std::size_t>(length) > text.size()) {
        throw_locked(env, "java/lang/StringIndexOutOfBoundsException", "GetStringRegion");
        return;
    }
    if (length > 0) std::memcpy(out, text.data() + start, static_cast<std::size_t>(length) * 2);
}

bool MockJvm::get_string_utf_region(Env env, Ref str, std::int32_t start, std::int32_t length, std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, str, "GetStringUTFRegion");
    if (id == 0) return false;
    const std::u16string& text = objects_[id - 1].text;
    if (start < 0 || length < 0 || static_cast<std::size_t>(start) + static_cast<std::size_t>(length) > text.size()) {
        throw_locked(env, "java/lang/StringIndexOutOfBoundsException", "GetStringUTFRegion");
        return false;
    }
    out = encode_utf(text.data() + start, static_cast<std::size_t>(length));
    return true;
}

std::int32_t MockJvm::get_array_length(Env env, Ref array) {
    std::lock_guard<std::mutex> lock(mutex_);
    Object* object = array_locked(env, array, "GetArrayLength");
    return object == nullptr ? 0 : object->length;
}

char MockJvm::get_array_element_type(Env env, Ref array) {
    std::lock_guard<std::mutex> lock(mutex_);
    Object* object = array_locked(env, array, "GetArrayElementType");
    return object == nullptr ? 0 : object->element;
}

MockJvm::Ref MockJvm::new_object_array(Env env, std::int32_t length, Ref element_class, Ref initial) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* element = class_ref_locked(env, element_class, "NewObjectArray");
    if (element == nullptr) return 0;
    if (length < 0) {
        throw_locked(env, "java/lang/NegativeArraySizeException", "NewObjectArray");
        return 0;
    }
    const ObjectId value = object_locked(env, initial, "NewObjectArray initial");
    const std::string name = element->name[0] == '[' ? "[" + element->name : "[L" + element->name + ";";
    class_locked(name);
    const ObjectId id = new_object_locked(name);
    objects_[id - 1].element = 'L';
    objects_[id - 1].length = length;
    objects_[id - 1].elements.assign(static_cast<std::size_t>(length), value);
    return new_local_locked(env, id);
}

MockJvm::Ref MockJvm::get_object_array_element(Env env, Ref array, std::int32_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    Object* object = array_locked(env, array, "GetObjectArrayElement");
    if (object == nullptr || object->element != 'L') return 0;
    if (index < 0 || index >= object->length) {
        throw_locked(env, "java/lang/ArrayIndexOutOfBoundsException", "GetObjectArrayElement");
        return 0;
    }
    return new_local_locked(env, object->elements[static_cast<std::size_t>(index)]);
}

void MockJvm::set_object_array_element(Env env, Ref array, std::int32_t index, Ref value) {
    std::lock_guard<std::mutex> lock(mutex_);
    Object* object = array_locked(env, array, "SetObjectArrayElement");
    if (object == nullptr || object->element != 'L') return;
    const ObjectId id = object_locked(env, value, "SetObjectArrayElement value");
    if (index < 0 || index >= object->length) {
        throw_locked(env, "java/lang/ArrayIndexOutOfBoundsException", "SetObjectArrayElement");
        return;
    }
    object->elements[static_cast<std::size_t>(index)] = id;
}

MockJvm::Ref MockJvm::new_primitive_array(Env env, char type, std::int32_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_locked(env, "NewPrimitiveArray") == nullptr) return 0;
    if (element_size(type) == 0) {
        error_locked("NewPrimitiveArray: bad type %c", type);
        return 0;
    }
    if (length < 0) {
        throw_locked(env, "java/lang/NegativeArraySizeException", "NewPrimitiveArray");
        return 0;
    }
    const ObjectId id = new_object_locked(std::string("[") + type);
    objects_[id - 1].element = type;
    objects_[id - 1].length = length;
    objects_[id - 1].bytes.assign(static_cast<std::size_t>(length) * element_size(type), 0);
    return new_local_locked(env, id);
}

MockJvm::ObjectId MockJvm::new_sparse_primitive_array(char type, std::int32_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (element_size(type) == 0 || length < 0) return 0;
    const ObjectId id = new_object_locked(std::string("[") + type);
    objects_[id - 1].element = type;
    objects_[id - 1].length = length;
    return id;
}

void MockJvm::get_primitive_array_region(Env env, char type, Ref array, std::int32_t start, std::int32_t length,
                                         void* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    Object* object = array_locked(env, array, "GetPrimitiveArrayRegion");
    if (object == nullptr) return;
    if (object->element != type) {
        error_locked("Get%cArrayRegion on a %c array", type, object->element);
        return;
    }
    if (start < 0 || length < 0 || start > object->length - length) {
        throw_locked(env, "java/lang/ArrayIndexOutOfBoundsException", "GetPrimitiveArrayRegion");
        return;
    }
    const std::size_t size = element_size(type);
    if (length > 0) std::memcpy(out, object->bytes.data() + start * size, static_cast<std::size_t>(length) * size);
}

void MockJvm::set_primitive_array_region(Env env, char type, Ref array, std::int32_t start, std::int32_t length,
                                         const void* in) {
    std::lock_guard<std::mutex> lock(mutex_);
    Object* object = array_locked(env, array, "SetPrimitiveArrayRegion");
    if (object == nullptr) return;
    if (object->element != type) {
        error_locked("Set%cArrayRegion on a %c array", type, object->element);
        return;
    }
    if (start < 0 || length < 0 || start > object->length - length) {
        throw_locked(env, "java/lang/ArrayIndexOutOfBoundsException", "SetPrimitiveArrayRegion");
        return;
    }
    const std::size_t size = element_size(type);
    if (length > 0) std::memcpy(object->bytes.data() + start * size, in, static_cast<std::size_t>(length) * size);
}

MockJvm::Ref MockJvm::new_global_ref(Env env, Ref obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    return new_ref_locked(object_locked(env, obj, "NewGlobalRef"), RefKind::Global, 0);
}

void MockJvm::delete_global_ref(Env env, Ref ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    RefEntry* entry = entry_locked(ref);
    if (thread_locked(env, "DeleteGlobalRef") == nullptr) return;
    if (entry == nullptr || !entry->live || entry->kind != RefKind::Global) {
        error_locked("DeleteGlobalRef: 0x%llx is not a live global reference", static_cast<unsigned long long>(ref));
        return;
    }
    entry->live = false;
}

MockJvm::Ref MockJvm::new_weak_global_ref(Env env, Ref obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    return new_ref_locked(object_locked(env, obj, "NewWeakGlobalRef"), RefKind::Weak, 0);
}

void MockJvm::delete_weak_global_ref(Env env, Ref ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    RefEntry* entry = entry_locked(ref);
    if (thread_locked(env, "DeleteWeakGlobalRef") == nullptr) return;
    if (entry == nullptr || !entry->live || entry->kind != RefKind::Weak) {
        error_locked("DeleteWeakGlobalRef: 0x%llx is not a live weak reference", static_cast<unsigned long long>(ref));
        return;
    }
    entry->live = false;
}

MockJvm::Ref MockJvm::new_local_ref(Env env, Ref obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    return new_local_locked(env, object_locked(env, obj, "NewLocalRef"));
}

void MockJvm::delete_local_ref(Env env, Ref ref) {
    std::lock_guard<std::mutex> lock(mutex_);
    RefEntry* entry = entry_locked(ref);
    if (thread_locked(env, "DeleteLocalRef") == nullptr) return;
    if (entry == nullptr || !entry->live || entry->kind != RefKind::Local || entry->env != env) {
        error_locked("DeleteLocalRef: 0x%llx is not a live local reference", static_cast<unsigned long long>(ref));
        return;
    }
    if (entry->argument) {
        error_locked("DeleteLocalRef: 0x%llx is a native method argument", static_cast<unsigned long long>(ref));
        return;
    }
    entry->live = false;
}

std::int32_t MockJvm::ensure_local_capacity(Env env, std::int32_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_locked(env, "EnsureLocalCapacity") == nullptr) return -1;
    return capacity < 0 ? -1 : 0;
}

std::int32_t MockJvm::push_local_frame(Env env, std::int32_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    Thread* thread = thread_locked(env, "PushLocalFrame");
    if (thread == nullptr || capacity < 0) return -1;
    thread->frames.emplace_back();
    return 0;
}

MockJvm::Ref MockJvm::pop_local_frame(Env env, Ref result) {
    std::lock_guard<std::mutex> lock(mutex_);
    Thread* thread = thread_locked(env, "PopLocalFrame");
    if (thread == nullptr) return 0;
    const ObjectId kept = object_locked(env, result, "PopLocalFrame result");
    if (thread->frames.empty()) {
        error_locked("PopLocalFrame without a frame");
        return 0;
    }
    pop_frame_locked(*thread);
    return new_local_locked(env, kept);
}

std::int32_t MockJvm::throw_exception(Env env, Ref throwable) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, throwable, "Throw");
    if (id == 0 || !is_subclass_locked(objects_[id - 1].cls, "java/lang/Throwable")) {
        error_locked("Throw: not a throwable");
        return -1;
    }
    threads_[env].exception = id;
    return 0;
}

std::int32_t MockJvm::throw_new(Env env, Ref cls, const char* message) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "ThrowNew");
    if (c == nullptr || !is_subclass_locked(c->name, "java/lang/Throwable")) {
        error_locked("ThrowNew: not a throwable class");
        return -1;
    }
    throw_locked(env, c->name, message != nullptr ? message : "");
    return 0;
}

MockJvm::Ref MockJvm::exception_occurred(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    Thread* thread = thread_locked(env, "ExceptionOccurred");
    return thread == nullptr ? 0 : new_local_locked(env, thread->exception);
}

void MockJvm::exception_describe(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    Thread* thread = thread_locked(env, "ExceptionDescribe");
    if (thread == nullptr || thread->exception == 0) return;
    const Object& exception = objects_[thread->exception - 1];
    std::fprintf(stderr, "mock exception: %s: %s\n", exception.cls.c_str(),
                 encode_utf(exception.text.data(), exception.text.size()).c_str());
    thread->exception = 0;  // ExceptionDescribe clears the exception, as in ART
}

void MockJvm::exception_clear(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Thread* thread = thread_locked(env, "ExceptionClear")) thread->exception = 0;
}

bool MockJvm::exception_check(Env env) {
    std::lock_guard<std::mutex> lock(mutex_);
    Thread* thread = thread_locked(env, "ExceptionCheck");
    return thread != nullptr && thread->exception != 0;
}

void MockJvm::fatal_error(Env, const char* message) {
    std::fprintf(stderr, "mock FatalError: %s\n", message);
    std::fflush(stderr);
    std::_Exit(kFatalExitStatus);
}

std::int32_t MockJvm::monitor_enter(Env env, Ref obj) {
    std::unique_lock<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, obj, "MonitorEnter");
    if (id == 0) return -1;
    const std::thread::id self = std::this_thread::get_id();
    monitor_cv_.wait(lock, [&] { return objects_[id - 1].monitor_count == 0 || objects_[id - 1].owner == self; });
    objects_[id - 1].owner = self;
    ++objects_[id - 1].monitor_count;
    return 0;
}

std::int32_t MockJvm::monitor_exit(Env env, Ref obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, obj, "MonitorExit");
    if (id == 0) return -1;
    Object& object = objects_[id - 1];
    if (object.monitor_count == 0 || object.owner != std::this_thread::get_id()) {
        error_locked("MonitorExit: monitor not owned by the calling thread");
        return -1;
    }
    if (--object.monitor_count == 0) {
        object.owner = std::thread::id();
        monitor_cv_.notify_all();
    }
    return 0;
}

std::int32_t MockJvm::register_native(Env env, Ref cls, const char* name, const char* signature, void* function) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "RegisterNatives");
    if (c == nullptr) return -1;
    if (failed_registration_ && *failed_registration_ == signature) {
        failed_registration_.reset();
        throw_locked(env, "java/lang/NoSuchMethodError", std::string("rejected native method ") + name + signature);
        return -1;
    }
    for (Method& method : methods_) {
        if (method.is_native && method.cls == c->name && method.name == name && method.signature == signature) {
            method.function = function;
            return 0;
        }
    }
    throw_locked(env, "java/lang/NoSuchMethodError", std::string("no native method ") + name + signature);
    return -1;
}

std::int32_t MockJvm::unregister_natives(Env env, Ref cls) {
    std::lock_guard<std::mutex> lock(mutex_);
    Class* c = class_ref_locked(env, cls, "UnregisterNatives");
    if (c == nullptr) return -1;
    for (Method& method : methods_) {
        if (method.is_native && method.cls == c->name) method.function = nullptr;
    }
    return 0;
}

MockJvm::Ref MockJvm::new_direct_byte_buffer(Env env, void* address, std::int64_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_locked(env, "NewDirectByteBuffer") == nullptr) return 0;
    if (capacity < 0 || (address == nullptr && capacity != 0)) {
        throw_locked(env, "java/lang/IllegalArgumentException", "NewDirectByteBuffer");
        return 0;
    }
    const ObjectId id = new_object_locked("java/nio/DirectByteBuffer");
    objects_[id - 1].direct = true;
    objects_[id - 1].address = address;
    objects_[id - 1].capacity = capacity;
    return new_local_locked(env, id);
}

void* MockJvm::get_direct_buffer_address(Env env, Ref buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, buffer, "GetDirectBufferAddress");
    return id != 0 && objects_[id - 1].direct ? objects_[id - 1].address : nullptr;
}

std::int64_t MockJvm::get_direct_buffer_capacity(Env env, Ref buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ObjectId id = object_locked(env, buffer, "GetDirectBufferCapacity");
    return id != 0 && objects_[id - 1].direct ? objects_[id - 1].capacity : -1;
}

MockJvm::Env MockJvm::attach_current_thread(bool daemon, const char* name, Ref group) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (group != 0) {
        RefEntry* entry = entry_locked(group);
        if (entry == nullptr || !entry->live || entry->kind != RefKind::Global) {
            error_locked("AttachCurrentThread: the group must be a global reference");
            return 0;
        }
    }
    return attach_locked(daemon, name != nullptr ? name : "");
}

std::int32_t MockJvm::detach_current_thread() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = thread_envs_.find(current_thread_token());
    if (it == thread_envs_.end()) return -1;
    Thread& thread = threads_[it->second];
    while (!thread.frames.empty()) pop_frame_locked(thread);
    threads_.erase(it->second);
    thread_envs_.erase(it);
    return 0;
}

}  // namespace zb::mock
