// Exports whose Java side cannot be inspected: the JNI loader must skip them one by one and still
// bind the rest of the library. Modelled on a guest that bundles unresolvable classes (AdMob next
// to lime) in the same dex as the classes it does need.
#include <jni.h>

// Long form: the export names a parameter type the plugin cannot resolve.
JNIEXPORT jint JNICALL Java_zb_Skip_ads__Lcom_google_ads_Ad_2(JNIEnv* env, jclass cls, jobject ad) {
    (void)env;
    (void)cls;
    (void)ad;
    return 1;
}

// Short form: enumerating the declaring class throws.
JNIEXPORT jint JNICALL Java_zb_Broken_enumerate(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return 2;
}

// Ordinary export: must bind even though the other two were skipped.
JNIEXPORT jint JNICALL Java_zb_Skip_fine(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return 3;
}
