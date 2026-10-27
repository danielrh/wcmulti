/* twins_test.c: RVT.EXE /twins's self-test (src/twins.c) on the host, where rv_x is its C twin (asm_twins.c): every
 * comparison agrees by construction, and what counts is the sums it logs, which tests/host/twins_test.sh compares
 * with the DOS run's.  The few things it needs of the game are here: the log (to stdout) and a megabyte of "XMS". */
#include <stdio.h>
#include <stdarg.h>
#include "rv.h"
#include "twins.h"

void rv_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);                     /* (all of it out before a sanitizer stops the run) */
}

void rv_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);                     /* (all of it out before a sanitizer stops the run) */
}

void rv_fail(const char *file, int line, const char *what, long value, long limit)
{
    printf("FAIL %s:%d: %s (value %ld, limit %ld)\n", file, line, what, value, limit);
    exit(3);
}

/* XMS: one arena, blocks bump-allocated, as xms.c's */
static unsigned char g_arena[1 << 20];
static unsigned long g_top, g_off[8];
static unsigned g_n;

int xms_init(unsigned long min_kb, unsigned long max_kb) { g_top = 0; g_n = 0; return 1; }
void xms_shutdown(void) {}
XmsBlock xms_alloc(unsigned long bytes, int level)
{
    if (g_top + bytes > sizeof g_arena || g_n + 1 >= 8)
        return 0;
    g_off[++g_n] = g_top;
    g_top += (bytes + 15) & ~15UL;
    return (XmsBlock)g_n;
}
void xms_free_level(int level) { g_top = 0; g_n = 0; }
int xms_put(XmsBlock b, unsigned long off, const void *src, unsigned long n)
{
    memcpy(g_arena + g_off[b] + off, src, n);
    return 1;
}
void xms_where(XmsBlock b, unsigned *handle, unsigned long *off) { *handle = 1; *off = g_off[b]; }
int xms_get_at(unsigned handle, unsigned long off, void *dst, unsigned long n)
{
    if (handle != 1 || off + n > sizeof g_arena)
        rv_fail(__FILE__, __LINE__, "xms_get_at", (long)off, (long)sizeof g_arena);
    memcpy(dst, g_arena + off, n);
    return 1;
}

int main(void)
{
    return twins_selftest() < 0 ? 1 : 0;
}
