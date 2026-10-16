// A JNI shim: Java loads this one, and its JNI_OnLoad opens the library that does the work.
// Native application plugins often use this pattern to load their implementation library.
#include <dlfcn.h>
#include <jni.h>

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    if (dlopen("libzbloadhidden.so", RTLD_NOW) == NULL) return JNI_ERR;
    return JNI_VERSION_1_6;
}
