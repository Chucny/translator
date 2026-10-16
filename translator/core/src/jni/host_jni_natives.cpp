// Native registration and the Java -> guest dispatcher.
#include "host_jni_internal.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

#include "zb/jni_shorty.h"
#include "zb/log.h"
#include "zb/runtime_report.h"

namespace zb {

namespace {

std::atomic<HostJni::Impl*> g_jni{nullptr};

// Receives every thunk call: runs the slot's guest function for the calling Java thread.
void dispatch_native(std::uint32_t slot, NativeRegs& regs) {
    HostJni::Impl* jni = g_jni.load();
    const NativeTarget* target = jni != nullptr ? jni->slots.target(slot) : nullptr;
    if (target == nullptr) {
        log("native thunk slot %u has no target", slot);
        std::abort();
    }
    const char return_type = target->shorty[0];
    const std::optional<HostJni::NativeResult> result = jni->owner->call_native(
        regs.x[0], return_type, target->guest_function,
        [&](std::uint32_t guest_env, const RefToHandle& to_handle) {
            return marshal_native_args(target->shorty, regs, guest_env, to_handle);
        },
        target->counter);
    if (!result) {
        // The frame could not be opened because a Java exception is pending; ART throws it when
        // the native method returns. Any other failure cannot be reported to Java.
        if (!jni->backend.exception_check(regs.x[0])) {
            jni->fatal(regs.x[0], "native method in thunk slot %u: the guest call failed", slot);
        }
        regs.x[0] = 0;
        regs.d[0] = 0;
        return;
    }
    store_native_result(return_type, result->guest.r0, result->guest.r1, regs,
                        [&](std::uint32_t) { return result->ref; });
}

}  // namespace

void install_native_dispatcher(HostJni::Impl* jni) {
    HostJni::Impl* expected = nullptr;
    if (!g_jni.compare_exchange_strong(expected, jni)) {
        log("a second HostJni was created; the JNI bridge is one per process");
        std::abort();
    }
    set_native_dispatcher(&dispatch_native);
}

std::int32_t HostJni::register_native(JniBackend::Env env, JniBackend::Ref cls, const char* name,
                                      const char* signature, std::uint32_t guest_function, bool is_static,
                                      const char* class_name) {
    Impl& jni = *impl_;
    std::string_view stripped = signature;
    if (!stripped.empty() && stripped.front() == '!') stripped.remove_prefix(1);  // pre-O fast JNI marker
    const std::string descriptor(stripped);
    const std::optional<std::string> shorty = shorty_from_signature(descriptor);
    if (!shorty) {
        log("RegisterNatives: malformed signature %s for %s", signature, name);
        const JniBackend::Ref error = jni.backend.find_class(env, "java/lang/NoSuchMethodError");
        if (error != 0) jni.backend.throw_new(env, error, (std::string(name) + signature).c_str());
        return -1;
    }
    // The native-call census (runtime_report.h) counts by "Class.method" when the caller (the
    // loader) knows the class name, else by "method" alone (a bare guest RegisterNatives call).
    std::string label = class_name != nullptr && class_name[0] != '\0' ? std::string(class_name) + "." + name
                                                                        : std::string(name);
    NativeCallCounter& counter = runtime_report().native_call_counter(label);
    const std::int32_t slot =
        jni.slots.allocate(NativeTarget{guest_function, *shorty, is_static, std::move(label), &counter});
    if (slot < 0) {
        log("RegisterNatives: the native thunk pool is exhausted (%s%s)", name, signature);
        return -1;
    }
    const std::int32_t rc =
        jni.backend.register_native(env, cls, name, descriptor.c_str(), native_thunk_address(static_cast<std::uint32_t>(slot)));
    if (rc != 0) {
        jni.slots.release(static_cast<std::uint32_t>(slot));
        return rc;
    }
    // Counts both statically named Java_* exports and guest RegisterNatives.
    runtime_report().note_registered_native();
    return rc;
}

bool HostJni::Impl::serve_natives(JniCall& call) {
    JniThread& state = call.state();
    const char* name = jni_host_call_name(call.index());
    switch (call.index()) {
    case ZB_JNI_HC_RegisterNatives: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref cls = resolve(state, call.arg(0), name);
        const auto count = static_cast<std::int32_t>(call.arg(2));
        if (count < 0) {
            call.set(static_cast<std::uint32_t>(-1));
            return true;
        }
        const std::uint8_t* table =
            count != 0 ? readable(env, call.arg(1), sizeof(zb_jni_native_method) * static_cast<std::uint64_t>(count), name)
                       : nullptr;
        for (std::int32_t i = 0; i < count; ++i) {
            zb_jni_native_method method;
            std::memcpy(&method, table + sizeof method * static_cast<std::size_t>(i), sizeof method);
            const std::string method_name = read_string(env, method.name, name);
            const std::string signature = read_string(env, method.signature, name);
            // One backend call per method; stop at the first failure like ART, which keeps the
            // methods it has already bound.
            if (owner->register_native(env, cls, method_name.c_str(), signature.c_str(), method.fn) != 0) {
                call.set(static_cast<std::uint32_t>(-1));
                return true;
            }
        }
        call.set(0);
        return true;
    }
    case ZB_JNI_HC_UnregisterNatives:
        // Slots stay allocated: Java may still hold the thunk address.
        call.set(static_cast<std::uint32_t>(backend.unregister_natives(call.env(), resolve(state, call.arg(0), name))));
        return true;
    default:
        return false;
    }
}

}  // namespace zb
