#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char** argv) {
    printf("hello dynamic arm32\n");
    printf("argc=%d\n", argc);

    const char* path = argc > 1 ? argv[1] : "zb_io.tmp";
    FILE* f = fopen(path, "w");
    int ok = f != NULL && fputs("chucnys-translator\n", f) >= 0;
    if (f != NULL) ok = fclose(f) == 0 && ok;
    char line[64] = {0};
    f = fopen(path, "r");
    ok = ok && f != NULL && fgets(line, sizeof line, f) != NULL && strcmp(line, "chucnys-translator\n") == 0;
    if (f != NULL) fclose(f);
    unlink(path);
    printf("file=%s\n", ok ? "PASS" : "FAIL");

    void* libm = dlopen("libm.so", RTLD_NOW);
    double (*cos_fn)(double) = libm != NULL ? (double (*)(double))dlsym(libm, "cos") : NULL;
    printf("dlopen=%s\n", cos_fn != NULL && cos_fn(0.0) == 1.0 ? "PASS" : "FAIL");

    struct timespec ts;
    printf("clock=%s\n", clock_gettime(CLOCK_REALTIME, &ts) == 0 && ts.tv_sec > 1700000000 ? "PASS" : "FAIL");

    char* p = malloc(100000);
    memset(p, 1, 100000);
    printf("malloc=%s\n", p[99999] == 1 ? "PASS" : "FAIL");
    free(p);
    return 3;
}
