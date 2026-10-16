/* T4 signals: SIGSEGV handler + siglongjmp, raise, blocking, SA_ONSTACK, default abort. */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sigjmp_buf jump_target;
static volatile sig_atomic_t segv_seen;
static void* volatile segv_addr;
static volatile sig_atomic_t usr1_seen;
static volatile sig_atomic_t usr2_on_altstack;
static volatile double handler_float;
static char altstack_buffer[64 * 1024];

static void on_segv(int sig, siginfo_t* info, void* context) {
    (void)context;
    segv_seen = sig;
    segv_addr = info->si_addr;
    siglongjmp(jump_target, 1);
}

static void on_usr1(int sig) {
    usr1_seen = sig;
}

static void on_usr2(int sig, siginfo_t* info, void* context) {
    (void)sig;
    (void)info;
    (void)context;
    char local;
    usr2_on_altstack = &local >= altstack_buffer && &local < altstack_buffer + sizeof altstack_buffer;
    handler_float = 12345.678; /* clobbers VFP registers inside the handler */
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    if (sigsetjmp(jump_target, 1) == 0) {
        volatile int* bad = (int*)0x40;
        *bad = 1;
        printf("segv=FAIL\n");
    } else {
        printf("segv=%s\n", segv_seen == SIGSEGV && segv_addr == (void*)0x40 ? "PASS" : "FAIL");
    }

    signal(SIGUSR1, on_usr1);
    raise(SIGUSR1);
    printf("raise=%s\n", usr1_seen == SIGUSR1 ? "PASS" : "FAIL");

    usr1_seen = 0;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);
    raise(SIGUSR1);
    int blocked = usr1_seen == 0;
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    printf("mask=%s\n", blocked && usr1_seen == SIGUSR1 ? "PASS" : "FAIL");

    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_sp = altstack_buffer;
    ss.ss_size = sizeof altstack_buffer;
    sigaltstack(&ss, NULL);
    sa.sa_sigaction = on_usr2;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGUSR2, &sa, NULL);
    volatile double kept = 3.25;
    raise(SIGUSR2);
    printf("altstack=%s\n", usr2_on_altstack ? "PASS" : "FAIL");
    printf("float=%s\n", kept * 2.0 == 6.5 && handler_float > 12345.0 ? "PASS" : "FAIL");

    fflush(stdout);
    abort();
}
