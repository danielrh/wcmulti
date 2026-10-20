/* util.c: the log (RVLOG.TXT) and the bounds-failure stop. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "rv.h"

static int g_log_started;
unsigned char rv_filebuf[1024];

/* a line in RVLOG.TXT; the file is closed after each (DOSBox writes a file
 * out to its host only on a close: the browser page reads it while the game
 * runs) */
void rv_log(const char *fmt, ...)
{
    va_list ap;
    FILE *f = fopen("RVLOG.TXT", g_log_started ? "a" : "w");
    if (!f)
        return;
    g_log_started = 1;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
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
