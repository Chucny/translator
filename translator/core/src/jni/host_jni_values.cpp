// JNI host calls: method calls and fields.
#include "host_jni_internal.h"

#include <array>
#include <cstring>
#include <string>

#include "zb/log.h"

namespace zb {

namespace {

// Bytes of one element of a primitive type letter; 0 for anything else.
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

// A guest word pair (low, high) as a backend value of the given type.
JValue value_from_bits(char type, std::uint64_t bits) {
    JValue value{};
    switch (type) {
    case 'Z': value.z = static_cast<std::uint8_t>(bits); break;
    case 'B': value.b = static_cast<std::int8_t>(bits); break;
    case 'C': value.c = static_cast<std::uint16_t>(bits); break;
    case 'S': value.s = static_cast<std::int16_t>(bits); break;
    case 'I': value.i = static_cast<std::int32_t>(bits); break;
    case 'F': {
        const auto low = static_cast<std::uint32_t>(bits);
        std::memcpy(&value.f, &low, sizeof value.f);
        break;
    }
    case 'J': value.j = static_cast<std::int64_t>(bits); break;
    case 'D': std::memcpy(&value.d, &bits, sizeof value.d); break;
    default: value.l = bits; break;
    }
    return value;
}

// A backend value as the guest's 64-bit result (r0:r1). References are converted by the caller.
std::uint64_t bits_from_value(char type, const JValue& value) {
    switch (type) {
    case 'Z': return value.z;
    case 'B': return static_cast<std::uint8_t>(value.b);
    case 'C': return value.c;
    case 'S': return static_cast<std::uint16_t>(value.s);
    case 'I': return static_cast<std::uint32_t>(value.i);
    case 'F': {
        std::uint32_t bits;
        std::memcpy(&bits, &value.f, sizeof bits);
        return bits;
    }
    case 'J': return static_cast<std::uint64_t>(value.j);
    case 'D': {
        std::uint64_t bits;
        std::memcpy(&bits, &value.d, sizeof bits);
        return bits;
    }
    default: return 0;
    }
}

bool is_value_type(char type) {
    return element_size(type) != 0 || type == 'L';
}

}  // namespace

bool HostJni::Impl::serve_guest_library_load(JniBackend::Env env, std::uint32_t kind, char type,
                                             JniBackend::Id id, JValue* values, std::size_t count) {
    if (kind != ZB_JNI_CALL_STATIC || type != 'V' || count != 1) return false;
    GuestLibraryLoader loader;
    {
        std::lock_guard<std::mutex> lock(load_mutex);
        if (!library_loader) return false;
        if (!system_load_resolved) {
            const JniBackend::Ref system = backend.find_class(env, "java/lang/System");
            if (system != 0) {
                const JniBackend::Id load =
                    backend.get_method_id(env, system, "load", "(Ljava/lang/String;)V", true);
                if (load != 0) {
                    java_lang_system = backend.new_global_ref(env, system);
                    system_load = load;
                    system_load_resolved = true;
                }
                backend.delete_local_ref(env, system);
            }
        }
        if (!system_load_resolved || id != system_load) return false;
        loader = library_loader;
    }
    const std::int32_t length = backend.get_string_utf_length(env, values[0].l);
    if (length <= 0) return false;
    std::string path;
    if (!backend.get_string_utf_region(env, values[0].l, 0, length, path)) return false;
    std::string error;
    if (!loader(env, path, error)) {
        // Left to ART, which fails the call the way the guest's own code expects
        // (UnsatisfiedLinkError); the reason is logged because ART's message cannot name it.
        if (!error.empty()) log("guest System.load(%s) is not served: %s", path.c_str(), error.c_str());
        return false;
    }
    return true;
}

bool HostJni::Impl::serve_values(JniCall& call) {
    JniThread& state = call.state();
    const char* name = jni_host_call_name(call.index());
    const auto ref = [&](unsigned position) { return resolve(state, call.arg(position), name); };
    switch (call.index()) {
    case ZB_JNI_HC_CallMethodA: {
        const JniBackend::Env env = call.env();
        const std::uint32_t kind = call.arg(0);
        const char type = static_cast<char>(call.arg(1));
        const std::uint32_t method = call.arg(4);
        if (kind > ZB_JNI_CALL_NEW_OBJECT || (!is_value_type(type) && type != 'V')) {
            fatal(env, "CallMethodA: bad call kind %u or type 0x%x", kind, call.arg(1));
        }
        const JniBackend::Id id = method_id(env, method, name);
        const std::optional<std::string> shorty = method_shorty(method);
        if (!shorty) fatal(env, "CallMethodA: jmethodID 0x%08x has no shorty", method);
        // A constructor returns V; NewObject asks for the new object ('L').
        const bool matches = kind == ZB_JNI_CALL_NEW_OBJECT ? (*shorty)[0] == 'V' && type == 'L' : (*shorty)[0] == type;
        if (!matches) {
            fatal(env, "CallMethodA: result type %c does not match the method shorty %s", type, shorty->c_str());
        }
        const JniBackend::Ref obj = ref(2);
        const JniBackend::Ref cls = ref(3);
        const std::size_t count = shorty->size() - 1;
        std::array<JValue, ZB_JNI_SHORTY_SIZE> values{};
        if (count != 0) {
            const std::uint8_t* raw = readable(env, call.arg(5), 8 * count, name);
            for (std::size_t i = 0; i < count; ++i) {
                std::uint64_t bits;
                std::memcpy(&bits, raw + 8 * i, sizeof bits);
                const char letter = (*shorty)[i + 1];
                values[i] = letter == 'L'
                                ? value_from_bits('L', resolve(state, static_cast<std::uint32_t>(bits), name))
                                : value_from_bits(letter, bits);
            }
        }
        // The guest's own System.load(path) is served by the guest runtime: the path names a
        // library of the plugin, which ART cannot open (see HostJni::set_guest_library_loader).
        if (serve_guest_library_load(env, kind, type, id, values.data(), count)) return true;
        const JValue result =
            backend.call_method(env, static_cast<JniCallKind>(kind), type, obj, cls, id, values.data());
        if (type == 'L') {
            call.set(local(state, result.l));
        } else if (type != 'V') {
            call.set64(bits_from_value(type, result));
        }
        return true;
    }
    case ZB_JNI_HC_GetField: {
        const JniBackend::Env env = call.env();
        const char type = static_cast<char>(call.arg(1));
        if (!is_value_type(type)) fatal(env, "GetField: bad type 0x%x", call.arg(1));
        const JniBackend::Ref obj = ref(2);
        const JniBackend::Id id = field_id(env, call.arg(3), name);
        const JValue value = backend.get_field(env, call.arg(0) != 0, type, obj, id);
        if (type == 'L') {
            call.set(local(state, value.l));
        } else {
            call.set64(bits_from_value(type, value));
        }
        return true;
    }
    case ZB_JNI_HC_SetField: {
        const JniBackend::Env env = call.env();
        const char type = static_cast<char>(call.arg(1));
        if (!is_value_type(type)) fatal(env, "SetField: bad type 0x%x", call.arg(1));
        const JniBackend::Ref obj = ref(2);
        const JniBackend::Id id = field_id(env, call.arg(3), name);
        const std::uint64_t bits = type == 'L' ? ref(4) : (call.arg(4) | (static_cast<std::uint64_t>(call.arg(5)) << 32));
        backend.set_field(env, call.arg(0) != 0, type, obj, id, value_from_bits(type, bits));
        return true;
    }
    default:
        return false;
    }
}

}  // namespace zb
