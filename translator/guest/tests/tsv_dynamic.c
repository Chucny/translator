// Thread-specific values, in the shape Unity/il2cpp uses them: a key created once, then the
// get -> create -> set -> get sequence that a per-thread singleton goes through.
//
// The last two checks pin down the *degraded* behaviour, because that is what a guest sees when a
// key was never created (the key stays 0). bionic treats index 0 as never-in-use, so set returns
// EINVAL and get returns NULL -- and code that trusts the value writes through a null pointer.
// Chucny's Translator has to reproduce that faithfully rather than invent an object.
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

static pthread_key_t g_key;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static int g_obj;

static void create_key(void) {
    const int r = pthread_key_create(&g_key, NULL);
    printf("key_create=%d\n", r);
}

int main(void) {
    const int o = pthread_once(&g_once, create_key);
    printf("once=%d\n", o);

    // bionic sets bit 31 of the key when the slot is in use, and only indices 1..0x81 are handed
    // out, so a live key is never 0.
    printf("key_is_zero=%d\n", g_key == 0);

    const void* before = pthread_getspecific(g_key);
    printf("get_before=%s\n", before == NULL ? "null" : "non-null");

    const int set = pthread_setspecific(g_key, &g_obj);
    printf("set=%d\n", set);

    const void* after = pthread_getspecific(g_key);
    printf("get_after=%s\n", after == &g_obj ? "same" : (after == NULL ? "null" : "other"));

    // The uninitialized / never-created key case.
    const pthread_key_t zero = 0;
    printf("set_zero=%d\n", pthread_setspecific(zero, &g_obj));
    printf("get_zero=%s\n", pthread_getspecific(zero) == NULL ? "null" : "non-null");

    // A second, independent key must not alias the first.
    pthread_key_t other = 0;
    printf("key_create2=%d\n", pthread_key_create(&other, NULL));
    printf("other_differs=%d\n", other != g_key);
    pthread_setspecific(other, &g_once);
    printf("other_value=%s\n", pthread_getspecific(other) == &g_once ? "same" : "other");
    printf("first_intact=%s\n", pthread_getspecific(g_key) == &g_obj ? "yes" : "no");
    return 0;
}
