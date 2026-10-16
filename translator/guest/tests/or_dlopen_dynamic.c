/* T5: load all six Orange Roulette native libraries through the real arm32 linker. */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: or_dlopen_dynamic <dir with lib*.so>\n");
        return 2;
    }
    const char* dir = argv[1];

    /* Old libraries need the pre-M linker behaviour (basename DT_NEEDED, text relocations). */
    void* dl_android = dlopen("libdl_android.so", RTLD_NOW);
    void (*set_target_sdk)(unsigned) =
        dl_android != NULL ? (void (*)(unsigned))dlsym(dl_android, "android_set_application_target_sdk_version") : NULL;
    printf("target_sdk=%s\n", set_target_sdk != NULL ? "PASS" : "FAIL");
    if (set_target_sdk != NULL) set_target_sdk(16);

    printf("compat=%s\n", dlopen("libzbcompat.so", RTLD_NOW | RTLD_GLOBAL) != NULL ? "PASS" : "FAIL");

    static const char* const libs[] = {"std", "regexp", "zlib", "openal", "lime", "ApplicationMain"};
    void* handles[6] = {0};
    int all_loaded = 1;
    for (int i = 0; i < 6; ++i) {
        char path[512];
        snprintf(path, sizeof path, "%s/lib%s.so", dir, libs[i]);
        handles[i] = dlopen(path, RTLD_NOW);
        printf("dlopen %s=%s\n", libs[i], handles[i] != NULL ? "PASS" : "FAIL");
        if (handles[i] == NULL) {
            all_loaded = 0;
            fprintf(stderr, "dlopen %s: %s\n", path, dlerror());
        }
    }

    printf("jni_onload=%s\n", handles[4] != NULL && dlsym(handles[4], "JNI_OnLoad") != NULL ? "PASS" : "FAIL");
    printf("hxcpp_main=%s\n", handles[5] != NULL && dlsym(handles[5], "Java_org_haxe_HXCPP_main") != NULL ? "PASS" : "FAIL");
    return all_loaded ? 0 : 1;
}
