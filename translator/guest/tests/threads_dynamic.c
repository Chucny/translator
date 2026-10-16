/* T3 threads: pthread_create/join, exclusive-monitor atomics, futex mutexes, TLS. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define THREADS 8
#define ITERATIONS 20000

static atomic_int atomic_counter;
static int mutex_counter;
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int tls_value;
static int tls_ok[THREADS];

static void* worker(void* arg) {
    int index = (int)(intptr_t)arg;
    tls_value = index + 1;
    for (int i = 0; i < ITERATIONS; ++i) {
        atomic_fetch_add(&atomic_counter, 1);
        pthread_mutex_lock(&counter_lock);
        ++mutex_counter;
        pthread_mutex_unlock(&counter_lock);
    }
    tls_ok[index] = tls_value == index + 1;
    return (void*)(intptr_t)(index * 2);
}

int main(void) {
    pthread_t ids[THREADS];
    for (int i = 0; i < THREADS; ++i) {
        if (pthread_create(&ids[i], NULL, worker, (void*)(intptr_t)i) != 0) {
            printf("create=FAIL\n");
            return 1;
        }
    }
    int joined = 1;
    for (int i = 0; i < THREADS; ++i) {
        void* result = NULL;
        if (pthread_join(ids[i], &result) != 0 || (intptr_t)result != i * 2) joined = 0;
    }
    int tls = 1;
    int distinct = 1;
    for (int i = 0; i < THREADS; ++i) {
        tls &= tls_ok[i];
        for (int j = i + 1; j < THREADS; ++j) {
            if (pthread_equal(ids[i], ids[j])) distinct = 0;
        }
    }
    printf("join=%s\n", joined ? "PASS" : "FAIL");
    printf("atomic=%s\n", atomic_counter == THREADS * ITERATIONS ? "PASS" : "FAIL");
    printf("mutex=%s\n", mutex_counter == THREADS * ITERATIONS ? "PASS" : "FAIL");
    printf("tls=%s\n", tls ? "PASS" : "FAIL");
    printf("distinct=%s\n", distinct ? "PASS" : "FAIL");
    return 0;
}
