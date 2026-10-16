/* Phase 3 acceptance helper: guest __android_log_print goes through the arm32 liblog to logd.
 * On a phone check with: logcat -d -s zbguest   (the message must appear there).
 * Off-device logd is usually absent, so the result value is not part of the expected output. */
#include <android/log.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int result = __android_log_print(ANDROID_LOG_INFO, "zbguest", "hello from arm32 guest pid %d", (int)getpid());
    fprintf(stderr, "__android_log_print returned %d\n", result);
    printf("log=called\n");
    return 0;
}
