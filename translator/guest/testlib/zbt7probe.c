/* Real-ART JNI probe for Phase 4d T7. Safe JNI operations only: deliberate invalid-JNI cases stay
 * in the mock-backed zbjniprobe test because CheckJNI may abort a real app process. */
#include <jni.h>
#include <stdint.h>
#include <string.h>

#define CHECK(cond)                   \
    do {                              \
        if (!(cond)) return __LINE__; \
    } while (0)

extern jint zbjniprobe_calls(JNIEnv*, jobject);
extern jint zbjniprobe_fields(JNIEnv*, jobject);
extern jint zbjniprobe_exceptions(JNIEnv*, jobject);
extern jint zbjniprobe_direct_buffers(JNIEnv*, jobject);
extern jint zbjniprobe_register(JNIEnv*, jobject);
extern jint zbjniprobe_vm(JNIEnv*, jobject);
extern jint zbjniprobe_attach(JNIEnv*, jobject);

JNIEXPORT jint JNICALL Java_zb_T7_calls(JNIEnv* env, jclass cls, jstring text) {
    (void)cls;
    return zbjniprobe_calls(env, text);
}

JNIEXPORT jint JNICALL Java_zb_T7_fields(JNIEnv* env, jclass cls, jstring text) {
    (void)cls;
    return zbjniprobe_fields(env, text);
}

JNIEXPORT jint JNICALL Java_zb_T7_exceptions(JNIEnv* env, jclass cls) {
    (void)cls;
    return zbjniprobe_exceptions(env, NULL);
}

JNIEXPORT jint JNICALL Java_zb_T7_vm(JNIEnv* env, jclass cls) {
    (void)cls;
    return zbjniprobe_vm(env, NULL);
}

JNIEXPORT jint JNICALL Java_zb_T7_attach(JNIEnv* env, jclass cls) {
    (void)cls;
    return zbjniprobe_attach(env, NULL);
}

JNIEXPORT jint JNICALL Java_zb_T7_directBuffers(JNIEnv* env, jclass cls, jobject foreign) {
    (void)cls;
    return zbjniprobe_direct_buffers(env, foreign);
}

JNIEXPORT jint JNICALL Java_zb_T7_references(JNIEnv* env, jclass cls) {
    (void)cls;
    const jclass probe = (*env)->FindClass(env, "zb/Probe");
    CHECK(probe != NULL);
    CHECK((*env)->PushLocalFrame(env, 8) == JNI_OK);
    const jobject object = (*env)->AllocObject(env, probe);
    CHECK(object != NULL && (*env)->GetObjectRefType(env, object) == JNILocalRefType);
    const jobject global = (*env)->NewGlobalRef(env, object);
    const jweak weak = (*env)->NewWeakGlobalRef(env, object);
    CHECK(global != NULL && weak != NULL);
    CHECK((*env)->GetObjectRefType(env, global) == JNIGlobalRefType);
    CHECK((*env)->GetObjectRefType(env, weak) == JNIWeakGlobalRefType);
    CHECK((*env)->IsSameObject(env, object, global) && (*env)->IsSameObject(env, object, weak));
    CHECK((*env)->MonitorEnter(env, object) == JNI_OK && (*env)->MonitorEnter(env, object) == JNI_OK);
    CHECK((*env)->MonitorExit(env, object) == JNI_OK && (*env)->MonitorExit(env, object) == JNI_OK);
    const jobject kept = (*env)->PopLocalFrame(env, object);
    CHECK(kept != NULL && (*env)->IsSameObject(env, kept, global));
    (*env)->DeleteWeakGlobalRef(env, weak);
    (*env)->DeleteGlobalRef(env, global);
    (*env)->DeleteLocalRef(env, kept);
    return 0;
}

JNIEXPORT jint JNICALL Java_zb_T7_strings(JNIEnv* env, jclass cls) {
    (void)cls;
    const jstring hello = (*env)->NewStringUTF(env, "h\xc3\xa9llo");
    CHECK(hello != NULL && (*env)->GetStringLength(env, hello) == 5);
    CHECK((*env)->GetStringUTFLength(env, hello) == 6);
    jboolean copy = JNI_FALSE;
    const char* utf = (*env)->GetStringUTFChars(env, hello, &copy);
    CHECK(utf != NULL && strcmp(utf, "h\xc3\xa9llo") == 0);
    (*env)->ReleaseStringUTFChars(env, hello, utf);

    static const jchar units[4] = {'a', 0, 0xD83D, 0xDE00};
    const jstring wide = (*env)->NewString(env, units, 4);
    CHECK(wide != NULL && (*env)->GetStringLength(env, wide) == 4);
    const jchar* chars = (*env)->GetStringChars(env, wide, &copy);
    CHECK(chars != NULL && memcmp(chars, units, sizeof units) == 0);
    (*env)->ReleaseStringChars(env, wide, chars);
    jchar region[2] = {0, 0};
    (*env)->GetStringRegion(env, wide, 2, 2, region);
    CHECK(region[0] == 0xD83D && region[1] == 0xDE00);
    char utf_region[8] = {0};
    (*env)->GetStringUTFRegion(env, wide, 2, 2, utf_region);
    /* ART intentionally emits the supplementary code point as four-byte UTF-8 here, despite the
     * JNI specification requiring two three-byte Modified UTF-8 surrogate encodings. */
    CHECK(memcmp(utf_region, "\xf0\x9f\x98\x80", 4) == 0);
    return 0;
}

#define PROBE_ARRAY(Name, jtype, v0, v1, v2)                                                        \
    do {                                                                                            \
        const jtype##Array array = (*env)->New##Name##Array(env, 3);                                \
        const jtype input[3] = {v0, v1, v2};                                                        \
        jtype output[3] = {0, 0, 0};                                                                \
        CHECK(array != NULL && (*env)->GetArrayLength(env, array) == 3);                            \
        (*env)->Set##Name##ArrayRegion(env, array, 0, 3, input);                                    \
        jtype* elements = (*env)->Get##Name##ArrayElements(env, array, NULL);                       \
        CHECK(elements != NULL && elements[0] == v0 && elements[1] == v1 && elements[2] == v2);    \
        elements[0] = v2;                                                                           \
        (*env)->Release##Name##ArrayElements(env, array, elements, JNI_COMMIT);                     \
        (*env)->Get##Name##ArrayRegion(env, array, 0, 3, output);                                   \
        CHECK(output[0] == v2 && output[1] == v1);                                                  \
        elements[1] = v2;                                                                           \
        (*env)->Release##Name##ArrayElements(env, array, elements, JNI_ABORT);                      \
        (*env)->Get##Name##ArrayRegion(env, array, 0, 3, output);                                   \
        CHECK(output[1] == v1);                                                                      \
        elements = (*env)->Get##Name##ArrayElements(env, array, NULL);                              \
        CHECK(elements != NULL);                                                                     \
        elements[2] = v0;                                                                            \
        (*env)->Release##Name##ArrayElements(env, array, elements, 0);                              \
        (*env)->Get##Name##ArrayRegion(env, array, 0, 3, output);                                   \
        CHECK(output[2] == v0);                                                                      \
    } while (0)

JNIEXPORT jint JNICALL Java_zb_T7_arrays(JNIEnv* env, jclass cls) {
    (void)cls;
    PROBE_ARRAY(Boolean, jboolean, JNI_TRUE, JNI_FALSE, JNI_TRUE);
    PROBE_ARRAY(Byte, jbyte, -1, 2, -3);
    PROBE_ARRAY(Char, jchar, 0xFFFF, 2, 3);
    PROBE_ARRAY(Short, jshort, -1000, 2000, -3000);
    PROBE_ARRAY(Int, jint, -100000, 200000, -300000);
    PROBE_ARRAY(Long, jlong, INT64_C(-1), INT64_C(0x100000000), INT64_C(-0x100000000));
    PROBE_ARRAY(Float, jfloat, 0.5f, -1.5f, 2.5f);
    PROBE_ARRAY(Double, jdouble, 0.25, -1.25, 1e300);

    const jdoubleArray doubles = (*env)->NewDoubleArray(env, 2);
    jdouble* critical = (*env)->GetPrimitiveArrayCritical(env, doubles, NULL);
    CHECK(critical != NULL);
    critical[1] = 7.5;
    (*env)->ReleasePrimitiveArrayCritical(env, doubles, critical, 0);
    jdouble result[2] = {0, 0};
    (*env)->GetDoubleArrayRegion(env, doubles, 0, 2, result);
    CHECK(result[1] == 7.5);

    const jclass strings = (*env)->FindClass(env, "java/lang/String");
    const jstring first = (*env)->NewStringUTF(env, "first");
    const jstring second = (*env)->NewStringUTF(env, "second");
    const jobjectArray array = (*env)->NewObjectArray(env, 2, strings, first);
    CHECK(array != NULL && (*env)->IsSameObject(env, (*env)->GetObjectArrayElement(env, array, 0), first));
    (*env)->SetObjectArrayElement(env, array, 1, second);
    CHECK((*env)->IsSameObject(env, (*env)->GetObjectArrayElement(env, array, 1), second));
    return 0;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || env == NULL) return JNI_ERR;
    return zbjniprobe_register(env, NULL) == 0 ? JNI_VERSION_1_6 : JNI_ERR;
}
