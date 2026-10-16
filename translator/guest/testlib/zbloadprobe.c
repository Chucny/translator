#include <jni.h>

#ifndef ZB_LOAD_VERSION
#define ZB_LOAD_VERSION JNI_VERSION_1_6
#endif

JNIEXPORT jint JNICALL Java_zb_Load_shortExport(JNIEnv* env, jclass cls) {
    return 17;
}

JNIEXPORT jint JNICALL Java_zb_Load_over__I(JNIEnv* env, jclass cls, jint value) {
    return value + 1;
}

JNIEXPORT jint JNICALL Java_zb_Load_over__Ljava_lang_String_2(JNIEnv* env, jobject self, jstring value) {
    return value != NULL ? 23 : -1;
}

JNIEXPORT void JNICALL Java_zb_Missing_skip(JNIEnv* env, jclass cls) {}

#ifdef ZB_LOAD_UNKNOWN_EXPORT
JNIEXPORT void JNICALL Java_zb_Load_unknown(JNIEnv* env, jclass cls) {}
#endif

#ifndef ZB_LOAD_NO_ONLOAD
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || env == NULL) return JNI_ERR;
    if ((*env)->GetVersion(env) != JNI_VERSION_1_6) return JNI_ERR;
    return ZB_LOAD_VERSION;
}
#endif
