/* util.c: the log (RVLOG.TXT), the bounds-failure stop, upper memory. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dos.h>
#include <i86.h>
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

/* a block of upper memory (a UMB, above 640 KB) from DOS, or 0 when there is
 * none: DOSBox has 128 KB of them at D000 with EMS off (64 KB with it on).
 * The allocation strategy and the UMBs' link are set for the call and put
 * back; DOS frees the block when the program ends. */
void __far *rv_alloc_high(unsigned long bytes)
{
    union REGS r;
    unsigned strat, link, seg = 0;
    if (bytes > 0xFFF0UL)
        return 0;
    r.x.ax = 0x5800;
    int86(0x21, &r, &r);
    strat = r.x.ax;
    r.x.ax = 0x5802;
    int86(0x21, &r, &r);
    link = r.h.al;
    r.x.ax = 0x5803;                    /* the UMBs linked to the chain */
    r.x.bx = 1;
    int86(0x21, &r, &r);
    r.x.ax = 0x5801;                    /* high memory only, first fit */
    r.x.bx = 0x40;
    int86(0x21, &r, &r);
    r.h.ah = 0x48;
    r.x.bx = (unsigned)((bytes + 15) >> 4);
    int86(0x21, &r, &r);
    if (!r.x.cflag)
        seg = r.x.ax;
    r.x.ax = 0x5801;
    r.x.bx = strat;
    int86(0x21, &r, &r);
    r.x.ax = 0x5803;
    r.x.bx = link;
    int86(0x21, &r, &r);
    return seg ? MK_FP(seg, 0) : 0;
}
