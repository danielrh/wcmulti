/* seccomp.c: rv-core's seccomp filter (desktop/README.md): once in, the process may make only the system calls in
 * syscalls.h (and mmap2 without PROT_EXEC); any other one stops it with a line naming its number (SIGSYS, exit 70).
 * The 32-bit table only.  rv-sandbox-test (sandboxtest.c) installs the same filter to show it refusing. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>

void rv_seccomp(const char *who);

static const char *g_who = "rv-core";

static void blocked(int sig, siginfo_t *si, void *ctx)
{
    char msg[96];
    char num[12];
    int n = si->si_syscall, i = 0, k;
    (void)sig; (void)ctx;
    k = 0;
    while (g_who[k] && k < 30) {
        msg[k] = g_who[k];
        k++;
    }
    {
        static const char said[] = ": stopped: a system call not on its list, number ";
        memcpy(msg + k, said, sizeof said - 1);
        k += (int)sizeof said - 1;
    }
    do num[i++] = (char)('0' + n % 10); while ((n /= 10) && i < 11);
    while (i && k < 94)
        msg[k++] = num[--i];
    msg[k++] = '\n';
    if (write(2, msg, (size_t)k) < 0) {}
    _exit(70);
}

#define ALLOW(nr) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

void rv_seccomp(const char *who)
{
    struct sock_filter filter[] = {
        /* the 32-bit ABI only (a 64-bit system call from this process would be another table) */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#include "syscalls.h"
        /* mmap2 (malloc's large blocks), never with PROT_EXEC: no memory the game could run code from */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mmap2, 0, 4),                /* (not mmap2: to the last line) */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),                          /* (PROT_EXEC) */
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        /* anything else */
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
    };
    struct sock_fprog prog = { (unsigned short)(sizeof filter / sizeof filter[0]), filter };
    struct sigaction sa;
    g_who = who;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = blocked;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSYS, &sa, 0);
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
        perror("the seccomp filter");
        exit(1);
    }
}

