// JNI host calls: classes, member ids, reflection, objects, references, monitors, exceptions.
#include "host_jni_internal.h"

#include <cstdlib>

#include "zb/jni_shorty.h"
#include "zb/log.h"

namespace zb {

bool HostJni::Impl::serve_objects(JniCall& call) {
    JniThread& state = call.state();
    const char* name = jni_host_call_name(call.index());
    const auto ref = [&](unsigned position) { return resolve(state, call.arg(position), name); };
    switch (call.index()) {
    case ZB_JNI_HC_FindClass: {
        const JniBackend::Env env = call.env();
        const std::string class_name = read_string(env, call.arg(0), name);
        call.set(local(state, backend.find_class(env, class_name.c_str())));
        return true;
    }
    case ZB_JNI_HC_GetSuperclass:
        call.set(local(state, backend.get_superclass(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_IsAssignableFrom:
        call.set(backend.is_assignable_from(call.env(), ref(0), ref(1)) ? 1 : 0);
        return true;
    case ZB_JNI_HC_GetMethodID: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref cls = ref(0);
        const std::string method = read_string(env, call.arg(1), name);
        const std::string signature = read_string(env, call.arg(2), name);
        const JniBackend::Id id = backend.get_method_id(env, cls, method.c_str(), signature.c_str(), call.arg(3) != 0);
        if (id == 0) return true;
        const std::optional<std::string> shorty = shorty_from_signature(signature);
        if (!shorty) fatal(env, "GetMethodID: the backend accepted a malformed signature %s", signature.c_str());
        write_shorty(env, call.arg(4), *shorty, name);
        call.set(intern_method(id, *shorty));
        return true;
    }
    case ZB_JNI_HC_GetFieldID: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref cls = ref(0);
        const std::string field = read_string(env, call.arg(1), name);
        const std::string signature = read_string(env, call.arg(2), name);
        call.set(fields.intern(backend.get_field_id(env, cls, field.c_str(), signature.c_str(), call.arg(3) != 0)));
        return true;
    }
    case ZB_JNI_HC_GetMethodShorty: {
        const JniBackend::Env env = call.env();
        const std::optional<std::string> shorty = method_shorty(call.arg(0));
        if (!shorty) fatal(env, "invalid jmethodID 0x%08x", call.arg(0));
        write_shorty(env, call.arg(1), *shorty, name);
        call.set(1);
        return true;
    }
    case ZB_JNI_HC_FromReflectedMethod: {
        const JniBackend::Env env = call.env();
        std::string signature;
        const JniBackend::Id id = backend.from_reflected_method(env, ref(0), signature);
        if (id == 0) return true;
        const std::optional<std::string> shorty = shorty_from_signature(signature);
        if (!shorty) fatal(env, "FromReflectedMethod: malformed signature %s", signature.c_str());
        write_shorty(env, call.arg(1), *shorty, name);
        call.set(intern_method(id, *shorty));
        return true;
    }
    case ZB_JNI_HC_FromReflectedField:
        call.set(fields.intern(backend.from_reflected_field(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_ToReflectedMethod: {
        const JniBackend::Env env = call.env();
        const JniBackend::Id id = method_id(env, call.arg(1), name);
        call.set(local(state, backend.to_reflected_method(env, ref(0), id, call.arg(2) != 0)));
        return true;
    }
    case ZB_JNI_HC_ToReflectedField: {
        const JniBackend::Env env = call.env();
        const JniBackend::Id id = field_id(env, call.arg(1), name);
        call.set(local(state, backend.to_reflected_field(env, ref(0), id, call.arg(2) != 0)));
        return true;
    }
    case ZB_JNI_HC_AllocObject:
        call.set(local(state, backend.alloc_object(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_GetObjectClass:
        call.set(local(state, backend.get_object_class(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_IsInstanceOf:
        call.set(backend.is_instance_of(call.env(), ref(0), ref(1)) ? 1 : 0);
        return true;
    case ZB_JNI_HC_IsSameObject:
        call.set(backend.is_same_object(call.env(), ref(0), ref(1)) ? 1 : 0);
        return true;
    case ZB_JNI_HC_GetObjectRefType: {
        // JNIInvalidRefType 0, JNILocalRefType 1, JNIGlobalRefType 2, JNIWeakGlobalRefType 3.
        const std::uint32_t handle = call.arg(0);
        const std::uint32_t kind = handle & 3u;
        const bool valid = (kind == 1 && state.locals.get(handle)) || (kind == 2 && globals.get(handle)) ||
                           (kind == 3 && weaks.get(handle));
        call.set(handle != 0 && valid ? kind : 0);
        return true;
    }
    case ZB_JNI_HC_NewGlobalRef: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref obj = ref(0);
        call.set(obj == 0 ? 0 : globals.add(backend.new_global_ref(env, obj)));
        return true;
    }
    case ZB_JNI_HC_DeleteGlobalRef: {
        const JniBackend::Env env = call.env();
        const std::uint32_t handle = call.arg(0);
        if (handle == 0) return true;
        const std::optional<std::uint64_t> removed = globals.remove(handle);
        if (!removed) fatal(env, "DeleteGlobalRef: invalid global reference 0x%08x", handle);
        backend.delete_global_ref(env, *removed);
        return true;
    }
    case ZB_JNI_HC_NewWeakGlobalRef: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref obj = ref(0);
        call.set(obj == 0 ? 0 : weaks.add(backend.new_weak_global_ref(env, obj)));
        return true;
    }
    case ZB_JNI_HC_DeleteWeakGlobalRef: {
        const JniBackend::Env env = call.env();
        const std::uint32_t handle = call.arg(0);
        if (handle == 0) return true;
        const std::optional<std::uint64_t> removed = weaks.remove(handle);
        if (!removed) fatal(env, "DeleteWeakGlobalRef: invalid weak global reference 0x%08x", handle);
        backend.delete_weak_global_ref(env, *removed);
        return true;
    }
    case ZB_JNI_HC_NewLocalRef: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref obj = ref(0);
        call.set(obj == 0 ? 0 : local(state, backend.new_local_ref(env, obj)));
        return true;
    }
    case ZB_JNI_HC_DeleteLocalRef: {
        const JniBackend::Env env = call.env();
        const std::uint32_t handle = call.arg(0);
        if (handle == 0) return true;
        const std::optional<std::uint64_t> removed = state.locals.remove(handle);
        if (!removed) fatal(env, "DeleteLocalRef: invalid local reference 0x%08x", handle);
        backend.delete_local_ref(env, *removed);
        return true;
    }
    case ZB_JNI_HC_EnsureLocalCapacity:
        call.set(static_cast<std::uint32_t>(backend.ensure_local_capacity(call.env(), static_cast<std::int32_t>(call.arg(0)))));
        return true;
    case ZB_JNI_HC_PushLocalFrame: {
        const JniBackend::Env env = call.env();
        const std::int32_t rc = backend.push_local_frame(env, static_cast<std::int32_t>(call.arg(0)));
        if (rc == 0) {
            state.locals.push_frame();
            ++state.user_frames;
        }
        call.set(static_cast<std::uint32_t>(rc));
        return true;
    }
    case ZB_JNI_HC_PopLocalFrame: {
        const JniBackend::Env env = call.env();
        if (state.user_frames == 0) fatal(env, "PopLocalFrame without a matching PushLocalFrame");
        const JniBackend::Ref result = ref(0);
        // The backend frees the frame's host references itself.
        state.locals.pop_frame();
        --state.user_frames;
        call.set(local(state, backend.pop_local_frame(env, result)));
        return true;
    }
    case ZB_JNI_HC_MonitorEnter:
        call.set(static_cast<std::uint32_t>(backend.monitor_enter(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_MonitorExit:
        call.set(static_cast<std::uint32_t>(backend.monitor_exit(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_Throw:
        call.set(static_cast<std::uint32_t>(backend.throw_exception(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_ThrowNew: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref cls = ref(0);
        const std::string message = call.arg(1) != 0 ? read_string(env, call.arg(1), name) : std::string();
        call.set(static_cast<std::uint32_t>(
            backend.throw_new(env, cls, call.arg(1) != 0 ? message.c_str() : nullptr)));
        return true;
    }
    case ZB_JNI_HC_ExceptionOccurred:
        call.set(local(state, backend.exception_occurred(call.env())));
        return true;
    case ZB_JNI_HC_ExceptionDescribe:
        backend.exception_describe(call.env());
        return true;
    case ZB_JNI_HC_ExceptionClear:
        backend.exception_clear(call.env());
        return true;
    case ZB_JNI_HC_ExceptionCheck:
        call.set(backend.exception_check(call.env()) ? 1 : 0);
        return true;
    case ZB_JNI_HC_FatalError: {
        const JniBackend::Env env = call.env();
        const std::string message = read_string(env, call.arg(0), name);
        log("guest FatalError: %s", message.c_str());
        backend.fatal_error(env, message.c_str());
        std::abort();
    }
    default:
        return false;
    }
}

}  // namespace zb
