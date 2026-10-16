#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char** argv) {
    printf("hello arm32\n");
    printf("argc=%d argv1=%s\n", argc, argc > 1 ? argv[1] : "(none)");

    char* big = malloc(1 << 20);
    memset(big, 0x5a, 1 << 20);
    printf("malloc=%s\n", big[12345] == 0x5a ? "PASS" : "FAIL");
    free(big);

    void* p = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("mmap=%s\n", p != MAP_FAILED ? "PASS" : "FAIL");
    ((char*)p)[65535] = 1;
    munmap(p, 65536);

    struct timespec ts;
    printf("clock=%s\n", clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ? "PASS" : "FAIL");
    printf("pid=%s\n", getpid() > 0 ? "PASS" : "FAIL");
    return 7;
}
