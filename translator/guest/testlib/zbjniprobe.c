/* Guest probe library for jni_bridge_test: drives the guest JNIEnv (libzbjni.so) against the
 * mock JVM. Every probe is called like a native method (JNIEnv*, jobject argument) and returns 0,
 * or the source line of the first failed check. The mock model is defined in
 * tests/host/jni_bridge_test.cpp. */
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "zb/jni_hostcalls.h"

#define CHECK(cond)                   \
    do {                              \
        if (!(cond)) return __LINE__; \
    } while (0)

#define SAME(a, b) ((a) == (b))
#define SAME_REF(a, b) ((*env)->IsSameObject(env, (a), (b)) == JNI_TRUE)

/* ---- Classes, ids, references, frames, monitors, exceptions --------------------------------- */

static jobject held_global;
static jweak held_weak;

/* held: a zb/Probe instance from Java, which the mock collects before objects_after_gc. */
JNIEXPORT jint JNICALL zbjniprobe_objects(JNIEnv* env, jobject held) {
    CHECK((*env)->GetVersion(env) == JNI_VERSION_1_6);
    CHECK((*env)->DefineClass(env, "zb/Defined", NULL, NULL, 0) == NULL);

    const jclass probe = (*env)->FindClass(env, "zb/Probe");
    const jclass child_class = (*env)->FindClass(env, "zb/ProbeChild");
    CHECK(probe != NULL && child_class != NULL);
    CHECK(SAME_REF((*env)->GetSuperclass(env, child_class), probe));
    CHECK((*env)->IsAssignableFrom(env, child_class, probe) && !(*env)->IsAssignableFrom(env, probe, child_class));
    const jobject child = (*env)->AllocObject(env, child_class);
    CHECK(child != NULL && SAME_REF((*env)->GetObjectClass(env, child), child_class));
    CHECK((*env)->IsInstanceOf(env, child, probe) && !(*env)->IsInstanceOf(env, held, child_class));
    CHECK((*env)->IsInstanceOf(env, NULL, probe) && SAME_REF(NULL, NULL) && !SAME_REF(child, held));

    /* Member ids and reflection round trips. */
    const jmethodID echo_i = (*env)->GetMethodID(env, probe, "echoI", "(I)I");
    const jmethodID secho_i = (*env)->GetStaticMethodID(env, probe, "sechoI", "(I)I");
    const jfieldID field_i = (*env)->GetFieldID(env, probe, "i", "I");
    const jfieldID field_si = (*env)->GetStaticFieldID(env, probe, "si", "I");
    CHECK(echo_i != NULL && secho_i != NULL && field_i != NULL && field_si != NULL && echo_i != secho_i);
    CHECK((*env)->GetMethodID(env, probe, "echoI", "(I)I") == echo_i); /* ids are deduplicated */
    const jobject method = (*env)->ToReflectedMethod(env, probe, secho_i, JNI_TRUE);
    CHECK(method != NULL && (*env)->FromReflectedMethod(env, method) == secho_i);
    const jobject field = (*env)->ToReflectedField(env, probe, field_si, JNI_TRUE);
    CHECK(field != NULL && (*env)->FromReflectedField(env, field) == field_si);
    CHECK((*env)->FindClass(env, "zb/Missing") == NULL && (*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionClear(env);
    CHECK((*env)->GetMethodID(env, probe, "missing", "()V") == NULL && (*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionClear(env);
    CHECK((*env)->GetFieldID(env, probe, "missing", "I") == NULL && (*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionClear(env);

    /* Local frames and local references. */
    CHECK((*env)->EnsureLocalCapacity(env, 100) == 0);
    CHECK((*env)->PushLocalFrame(env, 8) == 0);
    const jobject inner = (*env)->AllocObject(env, probe);
    const jobject kept = (*env)->AllocObject(env, child_class);
    CHECK((*env)->GetObjectRefType(env, inner) == JNILocalRefType);
    const jobject result = (*env)->PopLocalFrame(env, kept);
    CHECK(result != NULL && (*env)->IsInstanceOf(env, result, child_class));
    CHECK((*env)->GetObjectRefType(env, inner) == JNIInvalidRefType); /* popped with its frame */
    const jobject copy = (*env)->NewLocalRef(env, result);
    CHECK(SAME_REF(copy, result));
    (*env)->DeleteLocalRef(env, copy);
    CHECK((*env)->GetObjectRefType(env, copy) == JNIInvalidRefType);
    CHECK((*env)->NewLocalRef(env, NULL) == NULL && (*env)->NewGlobalRef(env, NULL) == NULL);
    (*env)->DeleteLocalRef(env, NULL);

    /* Global and weak global references, kept until objects_after_gc. */
    held_global = (*env)->NewGlobalRef(env, child);
    held_weak = (*env)->NewWeakGlobalRef(env, held);
    CHECK((*env)->GetObjectRefType(env, held_global) == JNIGlobalRefType);
    CHECK((*env)->GetObjectRefType(env, held_weak) == JNIWeakGlobalRefType);
    CHECK(SAME_REF(held_global, child) && SAME_REF(held_weak, held));
    CHECK(SAME_REF((*env)->NewLocalRef(env, held_weak), held));

    /* Monitors are recursive. */
    CHECK((*env)->MonitorEnter(env, child) == 0 && (*env)->MonitorEnter(env, child) == 0);
    CHECK((*env)->MonitorExit(env, child) == 0 && (*env)->MonitorExit(env, child) == 0);

    /* Exceptions raised and inspected by guest code. */
    const jclass illegal_state = (*env)->FindClass(env, "java/lang/IllegalStateException");
    const jclass runtime_exception = (*env)->FindClass(env, "java/lang/RuntimeException");
    CHECK((*env)->ExceptionCheck(env) == JNI_FALSE && (*env)->ExceptionOccurred(env) == NULL);
    CHECK((*env)->ThrowNew(env, illegal_state, "guest") == 0 && (*env)->ExceptionCheck(env) == JNI_TRUE);
    const jthrowable thrown = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    CHECK(thrown != NULL && (*env)->IsInstanceOf(env, thrown, runtime_exception));
    CHECK((*env)->Throw(env, thrown) == 0 && (*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionDescribe(env); /* prints and clears */
    CHECK((*env)->ExceptionCheck(env) == JNI_FALSE);
    return 0;
}

/* Runs after the mock collected the held object: the global survives, the weak reads as null. */
JNIEXPORT jint JNICALL zbjniprobe_objects_after_gc(JNIEnv* env, jobject unused) {
    CHECK(held_global != NULL && !SAME_REF(held_global, NULL));
    CHECK(SAME_REF(held_weak, NULL) && (*env)->NewLocalRef(env, held_weak) == NULL);
    (*env)->DeleteWeakGlobalRef(env, held_weak);
    (*env)->DeleteGlobalRef(env, held_global);
    CHECK((*env)->GetObjectRefType(env, held_global) == JNIInvalidRefType);
    CHECK((*env)->GetObjectRefType(env, held_weak) == JNIInvalidRefType);
    held_global = NULL;
    held_weak = NULL;
    return 0;
}

/* Returns with a pending exception, which Java sees after the native method returns. */
JNIEXPORT jint JNICALL zbjniprobe_throw(JNIEnv* env, jobject unused) {
    const jclass illegal = (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    CHECK((*env)->ThrowNew(env, illegal, "from guest") == 0);
    return 0;
}

/* Invalid handle: the host reports a fatal error and does not return. */
JNIEXPORT jint JNICALL zbjniprobe_bad_handle(JNIEnv* env, jobject unused) {
    (*env)->GetObjectClass(env, (jobject)(uintptr_t)0x12345);
    return __LINE__;
}

/* ---- Calls: every return type, every call form -------------------------------------------- */

/* The mock's echo<T> returns arg+1 (Z: !arg, F/D: arg*2, L: arg) in zb/Probe and arg+2 (Z: arg,
 * F/D: arg*4, L: null) in zb/ProbeChild; the static secho<T> behaves like zb/Probe. */
#define PROBE_CALLS(Name, jtype, letter, desc, member, same, value, base_result, child_result)               \
    static jtype virtual_v_##Name(JNIEnv* env, jobject obj, jmethodID method, ...) {                         \
        va_list ap;                                                                                         \
        va_start(ap, method);                                                                               \
        const jtype result = (*env)->Call##Name##MethodV(env, obj, method, ap);                             \
        va_end(ap);                                                                                         \
        return result;                                                                                      \
    }                                                                                                       \
    static jtype nonvirtual_v_##Name(JNIEnv* env, jobject obj, jclass cls, jmethodID method, ...) {         \
        va_list ap;                                                                                         \
        va_start(ap, method);                                                                               \
        const jtype result = (*env)->CallNonvirtual##Name##MethodV(env, obj, cls, method, ap);              \
        va_end(ap);                                                                                         \
        return result;                                                                                      \
    }                                                                                                       \
    static jtype static_v_##Name(JNIEnv* env, jclass cls, jmethodID method, ...) {                          \
        va_list ap;                                                                                         \
        va_start(ap, method);                                                                               \
        const jtype result = (*env)->CallStatic##Name##MethodV(env, cls, method, ap);                       \
        va_end(ap);                                                                                         \
        return result;                                                                                      \
    }                                                                                                       \
    static int calls_##Name(JNIEnv* env, jclass probe, jobject child, jstring text) {                       \
        const jmethodID method = (*env)->GetMethodID(env, probe, "echo" #letter, "(" desc ")" desc);         \
        const jmethodID smethod = (*env)->GetStaticMethodID(env, probe, "secho" #letter, "(" desc ")" desc); \
        CHECK(method != NULL && smethod != NULL);                                                           \
        const jtype arg = value;                                                                            \
        jvalue args[1];                                                                                     \
        args[0].member = arg;                                                                               \
        CHECK(same((*env)->Call##Name##Method(env, child, method, arg), child_result));                     \
        CHECK(same(virtual_v_##Name(env, child, method, arg), child_result));                               \
        CHECK(same((*env)->Call##Name##MethodA(env, child, method, args), child_result));                   \
        CHECK(same((*env)->CallNonvirtual##Name##Method(env, child, probe, method, arg), base_result));      \
        CHECK(same(nonvirtual_v_##Name(env, child, probe, method, arg), base_result));                      \
        CHECK(same((*env)->CallNonvirtual##Name##MethodA(env, child, probe, method, args), base_result));    \
        CHECK(same((*env)->CallStatic##Name##Method(env, probe, smethod, arg), base_result));               \
        CHECK(same(static_v_##Name(env, probe, smethod, arg), base_result));                                \
        CHECK(same((*env)->CallStatic##Name##MethodA(env, probe, smethod, args), base_result));             \
        return 0;                                                                                           \
    }

PROBE_CALLS(Boolean, jboolean, Z, "Z", z, SAME, JNI_TRUE, JNI_FALSE, JNI_TRUE)
PROBE_CALLS(Byte, jbyte, B, "B", b, SAME, -5, -4, -3)
PROBE_CALLS(Char, jchar, C, "C", c, SAME, 0x1234, 0x1235, 0x1236)
PROBE_CALLS(Short, jshort, S, "S", s, SAME, -300, -299, -298)
PROBE_CALLS(Int, jint, I, "I", i, SAME, 41, 42, 43)
PROBE_CALLS(Long, jlong, J, "J", j, SAME, INT64_C(0x1122334455667788), INT64_C(0x1122334455667789),
            INT64_C(0x112233445566778a))
PROBE_CALLS(Float, jfloat, F, "F", f, SAME, 1.5f, 3.0f, 6.0f)
PROBE_CALLS(Double, jdouble, D, "D", d, SAME, -2.25, -4.5, -9.0)
PROBE_CALLS(Object, jobject, L, "Ljava/lang/String;", l, SAME_REF, text, text, NULL)

static void virtual_v_Void(JNIEnv* env, jobject obj, jmethodID method, ...) {
    va_list ap;
    va_start(ap, method);
    (*env)->CallVoidMethodV(env, obj, method, ap);
    va_end(ap);
}

static void nonvirtual_v_Void(JNIEnv* env, jobject obj, jclass cls, jmethodID method, ...) {
    va_list ap;
    va_start(ap, method);
    (*env)->CallNonvirtualVoidMethodV(env, obj, cls, method, ap);
    va_end(ap);
}

static void static_v_Void(JNIEnv* env, jclass cls, jmethodID method, ...) {
    va_list ap;
    va_start(ap, method);
    (*env)->CallStaticVoidMethodV(env, cls, method, ap);
    va_end(ap);
}

static jobject new_object_v(JNIEnv* env, jclass cls, jmethodID method, ...) {
    va_list ap;
    va_start(ap, method);
    const jobject result = (*env)->NewObjectV(env, cls, method, ap);
    va_end(ap);
    return result;
}

static jint mix_v(JNIEnv* env, jclass cls, jmethodID method, ...) {
    va_list ap;
    va_start(ap, method);
    const jint result = (*env)->CallStaticIntMethodV(env, cls, method, ap);
    va_end(ap);
    return result;
}

/* uint32_t raw_get_method_shorty(uint32_t id, char* out): the GetMethodShorty host call, which
 * libzbjni.so only makes when its shorty cache cannot hold an id. */
_Static_assert(ZB_JNI_HC_GetMethodShorty == 0xfc06u, "GetMethodShorty index");
__asm__(".text\n.arm\n.p2align 2\n.global raw_get_method_shorty\n.hidden raw_get_method_shorty\n"
        ".type raw_get_method_shorty, %function\nraw_get_method_shorty:\n    svc #0x5afc06\n    bx lr\n");
uint32_t raw_get_method_shorty(uint32_t id, char* out) __attribute__((visibility("hidden")));

/* text: a java/lang/String "text" from Java. */
JNIEXPORT jint JNICALL zbjniprobe_calls(JNIEnv* env, jobject text) {
    const jclass probe = (*env)->FindClass(env, "zb/Probe");
    const jclass child_class = (*env)->FindClass(env, "zb/ProbeChild");
    CHECK(probe != NULL && child_class != NULL && text != NULL);
    const jobject child = (*env)->AllocObject(env, child_class);
    CHECK(child != NULL);
    int line;
    if ((line = calls_Boolean(env, probe, child, text)) != 0) return line;
    if ((line = calls_Byte(env, probe, child, text)) != 0) return line;
    if ((line = calls_Char(env, probe, child, text)) != 0) return line;
    if ((line = calls_Short(env, probe, child, text)) != 0) return line;
    if ((line = calls_Int(env, probe, child, text)) != 0) return line;
    if ((line = calls_Long(env, probe, child, text)) != 0) return line;
    if ((line = calls_Float(env, probe, child, text)) != 0) return line;
    if ((line = calls_Double(env, probe, child, text)) != 0) return line;
    if ((line = calls_Object(env, probe, child, text)) != 0) return line;

    /* Void: echoV adds 1 (zb/Probe) or 2 (zb/ProbeChild) to the static "calls"; sechoV adds 4. */
    const jmethodID echo_v = (*env)->GetMethodID(env, probe, "echoV", "()V");
    const jmethodID secho_v = (*env)->GetStaticMethodID(env, probe, "sechoV", "()V");
    const jfieldID calls = (*env)->GetStaticFieldID(env, probe, "calls", "I");
    CHECK(echo_v != NULL && secho_v != NULL && calls != NULL);
    (*env)->CallVoidMethod(env, child, echo_v);
    virtual_v_Void(env, child, echo_v);
    (*env)->CallVoidMethodA(env, child, echo_v, NULL);
    (*env)->CallNonvirtualVoidMethod(env, child, probe, echo_v);
    nonvirtual_v_Void(env, child, probe, echo_v);
    (*env)->CallNonvirtualVoidMethodA(env, child, probe, echo_v, NULL);
    (*env)->CallStaticVoidMethod(env, probe, secho_v);
    static_v_Void(env, probe, secho_v);
    (*env)->CallStaticVoidMethodA(env, probe, secho_v, NULL);
    const jint call_count = (*env)->GetStaticIntField(env, probe, calls);
    if (call_count != 3 * 2 + 3 * 1 + 3 * 4) {
        fprintf(stderr, "zbjniprobe_calls: void call count=%d, expected=21\n", call_count);
        return __LINE__;
    }

    /* Every argument type through ..., va_list and jvalue[]: mix returns 42 when all match. */
    const jmethodID mix = (*env)->GetStaticMethodID(env, probe, "mix", "(ZBCSIJFDLjava/lang/String;)I");
    CHECK(mix != NULL);
    CHECK((*env)->CallStaticIntMethod(env, probe, mix, JNI_TRUE, (jbyte)-2, (jchar)0x1234, (jshort)-3, 4,
                                      INT64_C(0x1122334455667788), 1.5f, -2.25, text) == 42);
    CHECK(mix_v(env, probe, mix, JNI_TRUE, (jbyte)-2, (jchar)0x1234, (jshort)-3, 4, INT64_C(0x1122334455667788),
                1.5f, -2.25, text) == 42);
    jvalue mix_args[9];
    mix_args[0].z = JNI_TRUE;
    mix_args[1].b = -2;
    mix_args[2].c = 0x1234;
    mix_args[3].s = -3;
    mix_args[4].i = 4;
    mix_args[5].j = INT64_C(0x1122334455667788);
    mix_args[6].f = 1.5f;
    mix_args[7].d = -2.25;
    mix_args[8].l = text;
    CHECK((*env)->CallStaticIntMethodA(env, probe, mix, mix_args) == 42);

    /* Constructors: <init>(ILjava/lang/String;)V stores its arguments in i and l. */
    const jmethodID init = (*env)->GetMethodID(env, probe, "<init>", "(ILjava/lang/String;)V");
    const jfieldID field_i = (*env)->GetFieldID(env, probe, "i", "I");
    const jfieldID field_l = (*env)->GetFieldID(env, probe, "l", "Ljava/lang/String;");
    CHECK(init != NULL && field_i != NULL && field_l != NULL);
    jvalue init_args[2];
    init_args[0].i = 3;
    init_args[1].l = text;
    const jobject made[3] = {
        (*env)->NewObject(env, probe, init, 1, text),
        new_object_v(env, probe, init, 2, text),
        (*env)->NewObjectA(env, probe, init, init_args),
    };
    for (int i = 0; i < 3; ++i) {
        CHECK(made[i] != NULL && (*env)->IsInstanceOf(env, made[i], probe));
        CHECK((*env)->GetIntField(env, made[i], field_i) == i + 1);
        CHECK(SAME_REF((*env)->GetObjectField(env, made[i], field_l), text));
    }

    /* An id from reflection calls like the original; the host keeps every id's shorty. */
    const jmethodID secho_j = (*env)->GetStaticMethodID(env, probe, "sechoJ", "(J)J");
    const jobject reflected = (*env)->ToReflectedMethod(env, probe, secho_j, JNI_TRUE);
    const jmethodID back = (*env)->FromReflectedMethod(env, reflected);
    CHECK(back == secho_j && (*env)->CallStaticLongMethod(env, probe, back, INT64_C(-8)) == INT64_C(-7));
    char shorty[257];
    CHECK(raw_get_method_shorty((uint32_t)(uintptr_t)mix, shorty) == 1 && strcmp(shorty, "IZBCSIJFDL") == 0);
    return 0;
}

#define PROBE_FIELD(Name, jtype, name, desc, same, value)                             \
    do {                                                                              \
        const jfieldID field = (*env)->GetFieldID(env, probe, name, desc);            \
        const jfieldID sfield = (*env)->GetStaticFieldID(env, probe, "s" name, desc); \
        CHECK(field != NULL && sfield != NULL);                                       \
        const jtype v = value;                                                        \
        (*env)->Set##Name##Field(env, object, field, v);                              \
        CHECK(same((*env)->Get##Name##Field(env, object, field), v));                 \
        (*env)->SetStatic##Name##Field(env, probe, sfield, v);                        \
        CHECK(same((*env)->GetStatic##Name##Field(env, probe, sfield), v));           \
    } while (0)

/* text: a java/lang/String "field" from Java. */
JNIEXPORT jint JNICALL zbjniprobe_fields(JNIEnv* env, jobject text) {
    const jclass probe = (*env)->FindClass(env, "zb/Probe");
    CHECK(probe != NULL);
    const jobject object = (*env)->AllocObject(env, probe);
    CHECK(object != NULL && text != NULL);
    PROBE_FIELD(Boolean, jboolean, "z", "Z", SAME, JNI_TRUE);
    PROBE_FIELD(Byte, jbyte, "b", "B", SAME, -7);
    PROBE_FIELD(Char, jchar, "c", "C", SAME, 0xBEEF);
    PROBE_FIELD(Short, jshort, "s", "S", SAME, -1234);
    PROBE_FIELD(Int, jint, "i", "I", SAME, -123456789);
    PROBE_FIELD(Long, jlong, "j", "J", SAME, INT64_C(-0x0123456789abcdef));
    PROBE_FIELD(Float, jfloat, "f", "F", SAME, -0.375f);
    PROBE_FIELD(Double, jdouble, "d", "D", SAME, 6.02214076e23);
    PROBE_FIELD(Object, jobject, "l", "Ljava/lang/String;", SAME_REF, text);
    return 0;
}

/* Java -> guest -> Java (throws) -> guest (inspects, clears, rethrows) -> Java sees it pending. */
JNIEXPORT jint JNICALL zbjniprobe_exceptions(JNIEnv* env, jobject unused) {
    const jclass probe = (*env)->FindClass(env, "zb/Probe");
    const jmethodID fail = (*env)->GetStaticMethodID(env, probe, "fail", "()V");
    CHECK(fail != NULL);
    (*env)->CallStaticVoidMethod(env, probe, fail);
    CHECK((*env)->ExceptionCheck(env) == JNI_TRUE);
    const jthrowable thrown = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    const jclass illegal_state = (*env)->FindClass(env, "java/lang/IllegalStateException");
    CHECK(thrown != NULL && (*env)->IsInstanceOf(env, thrown, illegal_state));
    CHECK((*env)->Throw(env, thrown) == 0);
    return 0;
}

/* ---- Strings, arrays, direct buffers -------------------------------------------------------- */

JNIEXPORT jint JNICALL zbjniprobe_strings(JNIEnv* env, jobject unused) {
    const jstring hello = (*env)->NewStringUTF(env, "h\xc3\xa9llo");
    CHECK(hello != NULL);
    CHECK((*env)->GetStringLength(env, hello) == 5 && (*env)->GetStringUTFLength(env, hello) == 6);
    jboolean copy = JNI_FALSE;
    const char* utf = (*env)->GetStringUTFChars(env, hello, &copy);
    CHECK(utf != NULL && copy == JNI_TRUE && strcmp(utf, "h\xc3\xa9llo") == 0);
    (*env)->ReleaseStringUTFChars(env, hello, utf);

    /* UTF-16 with an embedded NUL and a surrogate pair. */
    static const jchar units[4] = {'a', 0, 0xD83D, 0xDE00};
    const jstring wide = (*env)->NewString(env, units, 4);
    CHECK(wide != NULL && (*env)->GetStringLength(env, wide) == 4);
    copy = JNI_FALSE;
    const jchar* chars = (*env)->GetStringChars(env, wide, &copy);
    CHECK(chars != NULL && copy == JNI_TRUE && memcmp(chars, units, sizeof units) == 0);
    (*env)->ReleaseStringChars(env, wide, chars);
    const char* modified = (*env)->GetStringUTFChars(env, wide, NULL);
    CHECK(modified != NULL && strcmp(modified, "a\xc0\x80\xed\xa0\xbd\xed\xb8\x80") == 0);
    (*env)->ReleaseStringUTFChars(env, wide, modified);
    const jchar* critical = (*env)->GetStringCritical(env, wide, NULL);
    CHECK(critical != NULL && critical[2] == 0xD83D);
    (*env)->ReleaseStringCritical(env, wide, critical);

    jchar region[2] = {0, 0};
    (*env)->GetStringRegion(env, wide, 2, 2, region);
    CHECK(region[0] == 0xD83D && region[1] == 0xDE00);
    char utf_region[8];
    memset(utf_region, 'x', sizeof utf_region);
    (*env)->GetStringUTFRegion(env, wide, 2, 2, utf_region);
    CHECK(memcmp(utf_region, "\xed\xa0\xbd\xed\xb8\x80", 7) == 0); /* bytes and NUL, as in ART */

    /* Out of range: a pending exception, cleared here. */
    (*env)->GetStringRegion(env, wide, 3, 2, region);
    CHECK((*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionClear(env);
    CHECK((*env)->NewStringUTF(env, NULL) == NULL);
    CHECK((*env)->GetStringUTFChars(env, NULL, NULL) == NULL);
    return 0;
}

#define PROBE_ARRAY(Name, jtype, v0, v1, v2)                                                                \
    do {                                                                                                    \
        const jtype##Array array = (*env)->New##Name##Array(env, 3);                                        \
        CHECK(array != NULL && (*env)->GetArrayLength(env, array) == 3);                                    \
        const jtype in[3] = {v0, v1, v2};                                                                   \
        jtype out[3];                                                                                       \
        (*env)->Set##Name##ArrayRegion(env, array, 0, 3, in);                                               \
        jboolean copy = JNI_FALSE;                                                                          \
        jtype* elems = (*env)->Get##Name##ArrayElements(env, array, &copy);                                 \
        CHECK(elems != NULL && copy == JNI_TRUE && elems[0] == v0 && elems[1] == v1 && elems[2] == v2);     \
        elems[0] = v2; /* JNI_COMMIT copies back and keeps the buffer */                                    \
        (*env)->Release##Name##ArrayElements(env, array, elems, JNI_COMMIT);                                \
        (*env)->Get##Name##ArrayRegion(env, array, 0, 3, out);                                              \
        CHECK(out[0] == v2 && out[1] == v1);                                                                \
        elems[1] = v2; /* JNI_ABORT frees without copying */                                                \
        (*env)->Release##Name##ArrayElements(env, array, elems, JNI_ABORT);                                 \
        (*env)->Get##Name##ArrayRegion(env, array, 0, 3, out);                                              \
        CHECK(out[1] == v1);                                                                                \
        elems = (*env)->Get##Name##ArrayElements(env, array, NULL);                                         \
        CHECK(elems != NULL && elems[0] == v2);                                                             \
        elems[2] = v0; /* mode 0 copies back and frees */                                                   \
        (*env)->Release##Name##ArrayElements(env, array, elems, 0);                                         \
        (*env)->Get##Name##ArrayRegion(env, array, 0, 3, out);                                              \
        CHECK(out[0] == v2 && out[1] == v1 && out[2] == v0);                                                \
    } while (0)

JNIEXPORT jint JNICALL zbjniprobe_arrays(JNIEnv* env, jobject unused) {
    PROBE_ARRAY(Boolean, jboolean, JNI_TRUE, JNI_FALSE, 2);
    PROBE_ARRAY(Byte, jbyte, -1, 2, -3);
    PROBE_ARRAY(Char, jchar, 0xFFFF, 2, 3);
    PROBE_ARRAY(Short, jshort, -1000, 2000, -3000);
    PROBE_ARRAY(Int, jint, -100000, 200000, -300000);
    PROBE_ARRAY(Long, jlong, INT64_C(-1), INT64_C(0x100000000), INT64_C(-0x100000000));
    PROBE_ARRAY(Float, jfloat, 0.5f, -1.5f, 2.5f);
    PROBE_ARRAY(Double, jdouble, 0.25, -1.25, 1e300);

    const jdoubleArray doubles = (*env)->NewDoubleArray(env, 2);
    jboolean copy = JNI_FALSE;
    jdouble* critical = (*env)->GetPrimitiveArrayCritical(env, doubles, &copy);
    CHECK(critical != NULL && copy == JNI_TRUE && critical[0] == 0.0);
    critical[1] = 7.5;
    (*env)->ReleasePrimitiveArrayCritical(env, doubles, critical, 0);
    jdouble back[2];
    (*env)->GetDoubleArrayRegion(env, doubles, 0, 2, back);
    CHECK(back[1] == 7.5);

    const jclass string_class = (*env)->FindClass(env, "java/lang/String");
    const jstring first = (*env)->NewStringUTF(env, "first");
    const jstring second = (*env)->NewStringUTF(env, "second");
    const jobjectArray strings = (*env)->NewObjectArray(env, 3, string_class, first);
    CHECK(strings != NULL && (*env)->GetArrayLength(env, strings) == 3);
    CHECK((*env)->GetPrimitiveArrayCritical(env, strings, NULL) == NULL); /* not a primitive array */
    CHECK(SAME_REF((*env)->GetObjectArrayElement(env, strings, 1), first));
    (*env)->SetObjectArrayElement(env, strings, 2, second);
    CHECK(SAME_REF((*env)->GetObjectArrayElement(env, strings, 2), second));

    const jintArray ints = (*env)->NewIntArray(env, 2);
    jint two[2];
    (*env)->GetIntArrayRegion(env, ints, 1, 2, two);
    CHECK((*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionClear(env);
    return 0;
}

/* A long array this large cannot have a copied elements buffer in an arm32 process. */
JNIEXPORT jint JNICALL zbjniprobe_buffer_overflow(JNIEnv* env, jobject array) {
    jlong* elements = (*env)->GetLongArrayElements(env, (jlongArray)array, NULL);
    if (elements != NULL) (*env)->ReleaseLongArrayElements(env, (jlongArray)array, elements, JNI_ABORT);
    return elements == NULL ? 0 : __LINE__;
}

static char direct_storage[64];

/* foreign: a direct buffer (capacity 16) whose memory is outside the guest reservation. */
JNIEXPORT jint JNICALL zbjniprobe_direct_buffers(JNIEnv* env, jobject foreign) {
    const jobject buffer = (*env)->NewDirectByteBuffer(env, direct_storage, sizeof direct_storage);
    CHECK(buffer != NULL);
    CHECK((*env)->GetDirectBufferAddress(env, buffer) == direct_storage);
    CHECK((*env)->GetDirectBufferCapacity(env, buffer) == (jlong)sizeof direct_storage);
    /* A buffer Java allocated lives outside the guest address space; the bridge mirrors it into
     * guest memory, so the address is usable and holds Java's bytes. The mirror is stable, and
     * guest writes reach Java when this native call returns. */
    unsigned char* mirror = (unsigned char*)(*env)->GetDirectBufferAddress(env, foreign);
    CHECK(mirror != NULL);
    CHECK((*env)->GetDirectBufferCapacity(env, foreign) == 16);
    CHECK((unsigned char*)(*env)->GetDirectBufferAddress(env, foreign) == mirror);
    mirror[0] = (unsigned char)(mirror[1] + mirror[2]);
    mirror[15] = 0x5A;
    return 0;
}

JNIEXPORT jint JNICALL zbjniprobe_bad_direct_capacity(JNIEnv* env, jobject unused) {
    (void)(*env)->NewDirectByteBuffer(env, NULL, (jlong)INT32_MAX + 1);
    return __LINE__;
}

/* ---- Native registration and Java -> guest calls -------------------------------------------- */

static jint native_add(JNIEnv* env, jclass cls, jint a, jfloat b, jfloat c, jint d, jfloat e, jfloat f) {
    return (jint)(a + 2 * d + 4 * b + 8 * c + 16 * e + 32 * f);
}

static void native_wide(JNIEnv* env, jclass cls, jlong value) {
    const jfieldID field = (*env)->GetStaticFieldID(env, cls, "wide", "J");
    (*env)->SetStaticLongField(env, cls, field, value);
}

/* nest(0) = 1000; nest(n) = callback(n - 1) + 1, where Java's callback(n) = nest(n) + 10. */
static jint native_nest(JNIEnv* env, jclass cls, jint depth) {
    if (depth == 0) return 1000;
    const jmethodID callback = (*env)->GetStaticMethodID(env, cls, "callback", "(I)I");
    return (*env)->CallStaticIntMethod(env, cls, callback, depth - 1) + 1;
}

static jstring native_echo(JNIEnv* env, jobject self, jstring text) {
    const char* chars = (*env)->GetStringUTFChars(env, text, NULL);
    char buffer[128];
    snprintf(buffer, sizeof buffer, "%s!", chars != NULL ? chars : "");
    (*env)->ReleaseStringUTFChars(env, text, chars);
    return (*env)->NewStringUTF(env, buffer);
}

static jint native_tid(JNIEnv* env, jclass cls) {
    return gettid();
}

JNIEXPORT jint JNICALL zbjniprobe_register(JNIEnv* env, jobject unused) {
    const jclass natives = (*env)->FindClass(env, "zb/Natives");
    CHECK(natives != NULL);
    const JNINativeMethod good[] = {
        {"add", "(IFFIFF)I", (void*)native_add},
        {"wide", "!(J)V", (void*)native_wide}, /* pre-O fast JNI marker */
        {"nest", "(I)I", (void*)native_nest},
        {"echo", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_echo},
    };
    CHECK((*env)->RegisterNatives(env, natives, good, 4) == JNI_OK);
    /* Stops at the missing method: tid is not bound, and the missing method's slot is released. */
    const JNINativeMethod bad[] = {
        {"missing", "(I)I", (void*)native_nest},
        {"tid", "()I", (void*)native_tid},
    };
    CHECK((*env)->RegisterNatives(env, natives, bad, 2) == JNI_ERR);
    CHECK((*env)->ExceptionCheck(env) == JNI_TRUE);
    (*env)->ExceptionClear(env);
    const JNINativeMethod tid[] = {{"tid", "()I", (void*)native_tid}};
    CHECK((*env)->RegisterNatives(env, natives, tid, 1) == JNI_OK);
    return 0;
}

/* ---- JavaVM ----------------------------------------------------------------------------- */

JNIEXPORT jint JNICALL zbjniprobe_vm(JNIEnv* env, jobject unused) {
    JavaVM* vm = NULL;
    CHECK((*env)->GetJavaVM(env, &vm) == JNI_OK && vm != NULL);
    JNIEnv* current = NULL;
    CHECK((*vm)->GetEnv(vm, (void**)&current, JNI_VERSION_1_6) == JNI_OK && current == env);
    CHECK((*vm)->GetEnv(vm, (void**)&current, 0x7fff) == JNI_EVERSION && current == NULL);
    JNIEnv* attached = NULL;
    CHECK((*vm)->AttachCurrentThread(vm, &attached, NULL) == JNI_OK && attached == env);
    CHECK((*vm)->DetachCurrentThread(vm) == JNI_ERR); /* Java frames are on this thread */
    CHECK((*vm)->DestroyJavaVM(vm) == JNI_ERR);
    return 0;
}

struct attach_job {
    JavaVM* vm;
    int line;
    int32_t tid;
};

static int attach_worker(struct attach_job* job) {
    JavaVM* vm = job->vm;
    JNIEnv* env = NULL;
    job->tid = gettid();
    CHECK((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED && env == NULL);
    JavaVMAttachArgs args = {JNI_VERSION_1_6, "zbjni-worker", NULL};
    CHECK((*vm)->AttachCurrentThreadAsDaemon(vm, &env, &args) == JNI_OK && env != NULL);
    JNIEnv* again = NULL;
    CHECK((*vm)->GetEnv(vm, (void**)&again, JNI_VERSION_1_6) == JNI_OK && again == env);
    const jclass probe = (*env)->FindClass(env, "zb/Probe");
    const jmethodID thread_name = (*env)->GetStaticMethodID(env, probe, "threadName", "()Ljava/lang/String;");
    const jmethodID thread_daemon = (*env)->GetStaticMethodID(env, probe, "threadDaemon", "()Z");
    CHECK(thread_name != NULL && thread_daemon != NULL);
    const jstring name = (*env)->CallStaticObjectMethod(env, probe, thread_name);
    const char* chars = (*env)->GetStringUTFChars(env, name, NULL);
    CHECK(chars != NULL && strcmp(chars, "zbjni-worker") == 0);
    (*env)->ReleaseStringUTFChars(env, name, chars);
    CHECK((*env)->CallStaticBooleanMethod(env, probe, thread_daemon) == JNI_TRUE);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, probe);
    CHECK((*vm)->DetachCurrentThread(vm) == JNI_OK);
    CHECK((*vm)->GetEnv(vm, (void**)&again, JNI_VERSION_1_6) == JNI_EDETACHED);
    CHECK((*vm)->DetachCurrentThread(vm) == JNI_ERR);
    return 0;
}

static void* attach_main(void* arg) {
    struct attach_job* job = arg;
    job->line = attach_worker(job);
    return NULL;
}

/* A guest pthread attaches, calls Java, and detaches. */
JNIEXPORT jint JNICALL zbjniprobe_attach(JNIEnv* env, jobject unused) {
    struct attach_job job = {NULL, -1, 0};
    CHECK((*env)->GetJavaVM(env, &job.vm) == JNI_OK);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, attach_main, &job) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(job.tid != 0 && job.tid != gettid());
    return job.line;
}
