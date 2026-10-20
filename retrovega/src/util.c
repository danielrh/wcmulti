/* util.c: the log (RVLOG.TXT) and the bounds-failure stop. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dos.h>
#include "rv.h"

static FILE *g_log;

void rv_log(const char *fmt, ...)
{
    va_list ap;
    if (!g_log)
        g_log = fopen("RVLOG.TXT", "w");
    if (!g_log)
        return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

void (*rv_fail_hook)(void);   /* the video module restores text mode here */

void rv_fail(const char *file, int line, const char *what, long value, long limit)
{
    if (rv_fail_hook)
        rv_fail_hook();
    rv_log("FAIL %s:%d: %s (value %ld, limit %ld)", file, line, what, value, limit);
    printf("RetroVega stopped: %s:%d: %s (value %ld, limit %ld)\n", file, line, what, value, limit);
    exit(3);
}
