/* sandboxtest.c: rv-sandbox-test, the audit's (audit.sh forbidden): rv-core's seccomp filter (seccomp.c) put in,
 * then one thing it must refuse.  Run inside rv-core's bubblewrap sandbox (policy.sh), it shows both refusing:
 *   socket   a network socket               stopped by the filter (exit 70)
 *   exec     starting a program (/bin/sh)   stopped by the filter (exit 70)
 *   execmem  memory it could run code from  stopped by the filter (exit 70)
 *   outside  files written outside the directory, straight to the system (past fsguard.c): refused by the
 *            sandbox (exit 71; any written: exit 1)
 * rv-core itself has none of this: it never calls syscall(). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

void rv_seccomp(const char *who);

int main(int argc, char **argv)
{
    const char *what = argc == 2 ? argv[1] : "";
    rv_seccomp("rv-sandbox-test");
    if (!strcmp(what, "socket")) {
        syscall(__NR_socket, 2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
    } else if (!strcmp(what, "exec")) {
        char *args[] = { (char *)"/bin/sh", 0 };
        syscall(__NR_execve, "/bin/sh", args, args + 1);
    } else if (!strcmp(what, "execmem")) {
        mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    } else if (!strcmp(what, "outside")) {
        /* (straight to the system: past fsguard.c, which would refuse the name before the system saw it) */
        const char *paths[] = { "/etc/rv-sandbox-test-was-here", "/usr/rv-sandbox-test-was-here",
                                "/tmp/rv-sandbox-test-was-here", "../rv-sandbox-test-was-here" };
        int i, fd, bad = 0;
        for (i = 0; i < 4; i++) {
            fd = (int)syscall(__NR_openat, AT_FDCWD, paths[i], O_WRONLY | O_CREAT, 0600);
            fprintf(stderr, "rv-sandbox-test: writing %s: %s\n", paths[i], fd < 0 ? strerror(errno) : "IT WAS WRITTEN");
            bad |= fd >= 0;
        }
        return bad ? 1 : 71;
    }
    fprintf(stderr, "rv-sandbox-test: %s was not stopped (or is not one of socket, exec, execmem, outside)\n", what);
    return 1;
}

