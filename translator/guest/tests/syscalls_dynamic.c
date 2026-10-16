/* Wider syscall coverage: files and directories, poll/select, sockets with SCM_RIGHTS,
 * epoll/eventfd, interval timers, resource queries, and the synthesized /proc files that
 * bionic and apps parse. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <unistd.h>

static volatile sig_atomic_t alarms;

static void on_alarm(int sig) {
    (void)sig;
    alarms++;
}

static void report(const char* name, int ok) {
    printf("%s=%s\n", name, ok ? "PASS" : "FAIL");
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "zb_syscalls_tmp";
    char path[512];
    char path2[512];
    char cwd[512];
    struct stat st;
    char buf[64];
    int ok;

    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/a.txt", dir);
    snprintf(path2, sizeof path2, "%s/b.txt", dir);

    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    ok = fd >= 0 && write(fd, "hello", 5) == 5 && ftruncate(fd, 100) == 0 && fstat(fd, &st) == 0 && st.st_size == 100;
    if (fd >= 0) close(fd);
    report("ftruncate", ok);

    report("rename", rename(path, path2) == 0 && access(path2, F_OK) == 0 && access(path, F_OK) != 0);

    struct timespec times2[2] = {{1000000, 0}, {2000000, 0}};
    report("utimensat", utimensat(AT_FDCWD, path2, times2, 0) == 0 && stat(path2, &st) == 0 && st.st_mtime == 2000000);

    DIR* d = opendir(dir);
    int found = 0;
    struct dirent* entry;
    while (d != NULL && (entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, "b.txt") == 0) found = 1;
    }
    if (d != NULL) closedir(d);
    report("readdir", found);

    ok = getcwd(cwd, sizeof cwd) != NULL && chdir(dir) == 0 && access("b.txt", F_OK) == 0 && chdir(cwd) == 0;
    report("chdir", ok);

    unlink(path2);
    report("rmdir", rmdir(dir) == 0);

    int p[2];
    ok = pipe(p) == 0 && write(p[1], "x", 1) == 1;
    struct pollfd pf = {p[0], POLLIN, 0};
    ok = ok && poll(&pf, 1, 1000) == 1 && (pf.revents & POLLIN);
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(p[0], &readable);
    struct timeval tv = {1, 0};
    ok = ok && select(p[0] + 1, &readable, NULL, NULL, &tv) == 1 && FD_ISSET(p[0], &readable);
    report("poll_select", ok);
    close(p[0]);
    close(p[1]);

    int sv[2];
    ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0;
    struct iovec out_iov[2] = {{"ab", 2}, {"cd", 2}};
    struct msghdr out_msg;
    memset(&out_msg, 0, sizeof out_msg);
    out_msg.msg_iov = out_iov;
    out_msg.msg_iovlen = 2;
    ok = ok && sendmsg(sv[0], &out_msg, 0) == 4;
    memset(buf, 0, sizeof buf);
    struct iovec in_iov = {buf, sizeof buf};
    struct msghdr in_msg;
    memset(&in_msg, 0, sizeof in_msg);
    in_msg.msg_iov = &in_iov;
    in_msg.msg_iovlen = 1;
    ok = ok && recvmsg(sv[1], &in_msg, 0) == 4 && memcmp(buf, "abcd", 4) == 0;
    ok = ok && send(sv[1], "zz", 2, 0) == 2 && recv(sv[0], buf, sizeof buf, 0) == 2;

    int passed_pipe[2];
    ok = ok && pipe(passed_pipe) == 0;
    char control[CMSG_SPACE(sizeof(int))];
    memset(control, 0, sizeof control);
    struct iovec one = {"f", 1};
    struct msghdr fd_msg;
    memset(&fd_msg, 0, sizeof fd_msg);
    fd_msg.msg_iov = &one;
    fd_msg.msg_iovlen = 1;
    fd_msg.msg_control = control;
    fd_msg.msg_controllen = sizeof control;
    struct cmsghdr* cm = CMSG_FIRSTHDR(&fd_msg);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type = SCM_RIGHTS;
    cm->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &passed_pipe[1], sizeof(int));
    ok = ok && sendmsg(sv[0], &fd_msg, 0) == 1;

    char received_control[CMSG_SPACE(sizeof(int))];
    char one_byte;
    struct iovec one_in = {&one_byte, 1};
    struct msghdr fd_in;
    memset(&fd_in, 0, sizeof fd_in);
    fd_in.msg_iov = &one_in;
    fd_in.msg_iovlen = 1;
    fd_in.msg_control = received_control;
    fd_in.msg_controllen = sizeof received_control;
    int received_fd = -1;
    if (ok && recvmsg(sv[1], &fd_in, 0) == 1) {
        struct cmsghdr* rcm = CMSG_FIRSTHDR(&fd_in);
        if (rcm != NULL && rcm->cmsg_type == SCM_RIGHTS) memcpy(&received_fd, CMSG_DATA(rcm), sizeof(int));
    }
    ok = ok && received_fd >= 0 && write(received_fd, "q", 1) == 1 && read(passed_pipe[0], buf, 1) == 1 && buf[0] == 'q';
    report("sockets", ok);
    close(sv[0]);
    close(sv[1]);

    int efd = eventfd(0, 0);
    int ep = epoll_create1(0);
    struct epoll_event ev;
    memset(&ev, 0, sizeof ev);
    ev.events = EPOLLIN;
    ev.data.u64 = 0x1122334455667788ull;
    uint64_t one64 = 1;
    struct epoll_event got;
    ok = efd >= 0 && ep >= 0 && epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev) == 0 && write(efd, &one64, 8) == 8 &&
         epoll_wait(ep, &got, 1, 1000) == 1 && got.data.u64 == 0x1122334455667788ull;
    report("epoll", ok);

    signal(SIGALRM, on_alarm);
    struct itimerval it = {{0, 0}, {0, 20000}};
    setitimer(ITIMER_REAL, &it, NULL);
    for (int i = 0; i < 400 && alarms == 0; ++i) usleep(5000);
    report("setitimer", alarms == 1);

    struct tms t;
    report("times", times(&t) != (clock_t)-1);

    struct rusage ru;
    report("getrusage", getrusage(RUSAGE_SELF, &ru) == 0 && ru.ru_maxrss > 0);

    struct sysinfo si;
    report("sysinfo", sysinfo(&si) == 0 && (unsigned long long)si.totalram * si.mem_unit > (1ull << 30));

    pthread_attr_t attr;
    void* stack = NULL;
    size_t size = 0;
    ok = pthread_getattr_np(pthread_self(), &attr) == 0 && pthread_attr_getstack(&attr, &stack, &size) == 0 &&
         (char*)&attr > (char*)stack && (char*)&attr < (char*)stack + size;
    report("main_stack", ok);

    FILE* f = fopen("/proc/cpuinfo", "r");
    char line[256];
    int features = 0;
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "Features", 8) == 0 && strstr(line, "neon") != NULL) features = 1;
    }
    if (f != NULL) fclose(f);
    report("cpuinfo", features);

    report("nprocs", sysconf(_SC_NPROCESSORS_ONLN) > 0);
    return 0;
}
