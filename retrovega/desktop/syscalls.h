/* syscalls.h: every system call rv-core may make once the game starts (core.c's seccomp filter; the 32-bit table).
 * Measured with strace on the walk and a flight (desktop/audit.sh run): what the game does is files in its own
 * directory, its two pipes, memory, the clock and sleeping.  Where glibc versions differ in how they do one of
 * these (statx or fstat64, clock_nanosleep or its 64-bit-time twin), both are here.  Any other system call (a
 * socket, a process, ptrace, mprotect, ioctl, ...) stops the game.  mmap2 is not here: core.c allows it only
 * without PROT_EXEC (no memory the game could run code from). */

/* its files and its pipes */
#ifdef __NR_read
ALLOW(__NR_read),
#endif
#ifdef __NR_write
ALLOW(__NR_write),
#endif
#ifdef __NR_openat
ALLOW(__NR_openat),                     /* (which names: host/fsguard.c; which directories: run.sh's bubblewrap) */
#endif
#ifdef __NR_close
ALLOW(__NR_close),
#endif
#ifdef __NR__llseek
ALLOW(__NR__llseek),
#endif
#ifdef __NR_lseek
ALLOW(__NR_lseek),
#endif
#ifdef __NR_statx
ALLOW(__NR_statx),                      /* (stdio asks an open file's size: glibc's fstat) */
#endif
#ifdef __NR_fstat64
ALLOW(__NR_fstat64),
#endif
#ifdef __NR_fstatat64
ALLOW(__NR_fstatat64),
#endif

/* memory (malloc) */
#ifdef __NR_brk
ALLOW(__NR_brk),
#endif
#ifdef __NR_munmap
ALLOW(__NR_munmap),
#endif
#ifdef __NR_mremap
ALLOW(__NR_mremap),
#endif

/* the clock and waiting (the clock itself is the vDSO's: these only where it is not) */
#ifdef __NR_clock_nanosleep
ALLOW(__NR_clock_nanosleep),
#endif
#ifdef __NR_clock_nanosleep_time64
ALLOW(__NR_clock_nanosleep_time64),
#endif
#ifdef __NR_nanosleep
ALLOW(__NR_nanosleep),
#endif
#ifdef __NR_clock_gettime
ALLOW(__NR_clock_gettime),
#endif
#ifdef __NR_clock_gettime64
ALLOW(__NR_clock_gettime64),
#endif

/* the end */
#ifdef __NR_exit_group
ALLOW(__NR_exit_group),
#endif
#ifdef __NR_exit
ALLOW(__NR_exit),
#endif
