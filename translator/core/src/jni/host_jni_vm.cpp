// JavaVM host calls: GetEnv, AttachCurrentThread(AsDaemon), DetachCurrentThread.
#include "host_jni_internal.h"

#include <cstring>

#include "zb/log.h"

namespace zb {

namespace {

constexpr std::uint32_t kJniOk = 0;
constexpr std::uint32_t kJniErr = static_cast<std::uint32_t>(-1);

}  // namespace

bool HostJni::Impl::serve_vm(JniCall& call) {
    JniThread& state = call.state();
    const char* name = jni_host_call_name(call.index());
    switch (call.index()) {
    case ZB_JNI_HC_GetEnv:
        call.set(state.env != 0 ? state.guest_env : 0);
        return true;
    case ZB_JNI_HC_AttachCurrentThread: {
        if (state.env == 0) {
            std::string thread_name;
            JniBackend::Ref group = 0;
            if (call.arg(1) != 0) {
                zb_jni_attach_args args;
                std::memcpy(&args, readable(0, call.arg(1), sizeof args, name), sizeof args);
                if (args.name != 0) thread_name = read_string(0, args.name, name);
                group = resolve(state, args.group, name);
            }
            const JniBackend::Env env =
                backend.attach_current_thread(call.arg(2) != 0, call.arg(1) != 0 ? thread_name.c_str() : nullptr, group);
            if (env == 0) {
                call.set(kJniErr);
                return true;
            }
            state.env = env;
            state.attached = true;
        }
        if (!ensure_guest_env(state)) {
            call.set(kJniErr);
            return true;
        }
        const std::uint32_t guest_env = state.guest_env;
        std::memcpy(writable(state.env, call.arg(0), sizeof guest_env, name), &guest_env, sizeof guest_env);
        call.set(kJniOk);
        return true;
    }
    case ZB_JNI_HC_DetachCurrentThread: {
        if (!state.attached || state.native_depth != 0) {
            // Not attached through the guest, or detaching while Java code is on the stack.
            call.set(kJniErr);
            return true;
        }
        const std::int32_t rc = backend.detach_current_thread();
        state.locals = LocalHandles();
        state.user_frames = 0;
        if (state.guest_env != 0) {
            GuestCall args;
            args.regs = {state.guest_env, 0, 0, 0};
            (void)invoke(state, api.free_env_fn, args);
        }
        state.guest_env = 0;
        state.env = 0;
        state.attached = false;
        call.set(static_cast<std::uint32_t>(rc));
        return true;
    }
    default:
        return false;
    }
}

}  // namespace zb
