/* Compiled by the NDK for armeabi-v7a only (tools/build_guest.sh, -c).
 * Asserts that bionic's arm layouts match core/include/zb/guest_abi.h. */
#include <signal.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <stddef.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <time.h>

_Static_assert(sizeof(void*) == 4, "must be built for 32-bit arm");
_Static_assert(sizeof(struct timespec) == 8, "timespec32");
_Static_assert(sizeof(struct timeval) == 8, "timeval32");
_Static_assert(sizeof(struct iovec) == 8, "iovec32");
_Static_assert(sizeof(stack_t) == 12, "stack32");
_Static_assert(offsetof(stack_t, ss_flags) == 4, "stack32.ss_flags");
_Static_assert(offsetof(stack_t, ss_size) == 8, "stack32.ss_size");
_Static_assert(sizeof(struct stat64) == 104, "stat64 size");
_Static_assert(offsetof(struct stat64, st_mode) == 16, "stat64.st_mode");
_Static_assert(offsetof(struct stat64, st_rdev) == 32, "stat64.st_rdev");
_Static_assert(offsetof(struct stat64, st_size) == 48, "stat64.st_size");
_Static_assert(offsetof(struct stat64, st_blksize) == 56, "stat64.st_blksize");
_Static_assert(offsetof(struct stat64, st_blocks) == 64, "stat64.st_blocks");
_Static_assert(offsetof(struct stat64, st_atim) == 72, "stat64.st_atim");
_Static_assert(offsetof(struct stat64, st_ino) == 96, "stat64.st_ino");
/* bionic's struct is unpacked (88 bytes); the kernel writes the packed 84-byte layout,
 * whose fields sit at the same offsets. */
_Static_assert(sizeof(struct statfs64) == 88, "statfs64 size");
_Static_assert(offsetof(struct statfs64, f_blocks) == 8, "statfs64.f_blocks");
_Static_assert(offsetof(struct statfs64, f_fsid) == 48, "statfs64.f_fsid");
_Static_assert(offsetof(struct statfs64, f_flags) == 64, "statfs64.f_flags");
_Static_assert(sizeof(struct itimerval) == 16, "itimerval32");
_Static_assert(sizeof(struct tms) == 16, "tms32");
_Static_assert(sizeof(struct rusage) == 72, "rusage32");
_Static_assert(offsetof(struct rusage, ru_maxrss) == 16, "rusage32.ru_maxrss");
_Static_assert(sizeof(struct sysinfo) == 64, "sysinfo32");
_Static_assert(offsetof(struct sysinfo, totalram) == 16, "sysinfo32.totalram");
_Static_assert(offsetof(struct sysinfo, procs) == 40, "sysinfo32.procs");
_Static_assert(offsetof(struct sysinfo, mem_unit) == 52, "sysinfo32.mem_unit");
_Static_assert(sizeof(struct msghdr) == 28, "msghdr32");
_Static_assert(offsetof(struct msghdr, msg_control) == 16, "msghdr32.msg_control");
_Static_assert(sizeof(struct cmsghdr) == 12, "cmsghdr32");
