#include <jni.h>

#include <vector>

#include "jni_env_backend.h"

extern "C" JNIEXPORT jint JNICALL zb_jni_reflection_compile_test(
    JNIEnv* env, JavaVM* vm, jobject class_loader, const char* class_name, const char* method_name) {
    zb::JniEnvBackend backend(vm);
    if (!backend.set_class_loader(env, class_loader)) return -1;
    zb::JniBackend::Ref cls = 0;
    std::vector<zb::DeclaredNativeMethod> methods;
    return static_cast<jint>(backend.find_declared_natives(
        reinterpret_cast<zb::JniBackend::Env>(env), class_name, method_name, nullptr, cls, methods));
}
