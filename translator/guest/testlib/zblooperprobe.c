#include <android/looper.h>
#include <sys/eventfd.h>
#include <stdint.h>
#include <unistd.h>

#define PROBE_CHECK(condition) \
    do {                       \
        if (!(condition)) return __LINE__; \
    } while (0)

static int calls;

static int keep_callback(int fd, int events, void* data) {
    uint64_t value = 0;
    if (!(events & ALOOPER_EVENT_INPUT)) return 0;
    if (read(fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) return 0;
    calls += (int)value;
    if (data == (void*)(uintptr_t)0x1234) ++calls;
    return 1;
}

static int remove_callback(int fd, int events, void* data) {
    uint64_t value = 0;
    (void)data;
    if (!(events & ALOOPER_EVENT_INPUT)) return 0;
    if (read(fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) return 0;
    ++calls;
    return 0;
}

__attribute__((visibility("default"))) int zb_looper_probe(void) {
    PROBE_CHECK(ALooper_forThread() == NULL);
    ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    PROBE_CHECK(looper != NULL);
    PROBE_CHECK(ALooper_forThread() == looper);
    ALooper_acquire(looper);

    const int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    PROBE_CHECK(fd >= 0);
    PROBE_CHECK(ALooper_addFd(looper, fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                              keep_callback, (void*)(uintptr_t)0x1234) == 1);
    const uint64_t one = 1;
    PROBE_CHECK(write(fd, &one, sizeof(one)) == (ssize_t)sizeof(one));
    PROBE_CHECK(ALooper_pollOnce(1000, NULL, NULL, NULL) == ALOOPER_POLL_CALLBACK);
    PROBE_CHECK(calls == 2);
    PROBE_CHECK(ALooper_removeFd(looper, fd) == 1);

    PROBE_CHECK(ALooper_addFd(looper, fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                              remove_callback, NULL) == 1);
    PROBE_CHECK(write(fd, &one, sizeof(one)) == (ssize_t)sizeof(one));
    PROBE_CHECK(ALooper_pollOnce(1000, NULL, NULL, NULL) == ALOOPER_POLL_CALLBACK);
    PROBE_CHECK(calls == 3);
    PROBE_CHECK(ALooper_removeFd(looper, fd) == 0);

    ALooper_release(looper);
    PROBE_CHECK(close(fd) == 0);
    return 0;
}
