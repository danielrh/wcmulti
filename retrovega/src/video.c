/* video.c: VESA mode 100h (640x400x256), the band buffer and the flip.
 *
 * The screen is drawn a run of dirty rows at a time (at most BAND_ROWS,
 * vid_dirty_band) into one 64,000-byte buffer; every run is drawn whole,
 * back to front, so nothing older is needed.  vid_flip copies the run to
 * the screen through the 64 KB bank window with XMS moves (handle 0 to
 * handle 0), split where a bank ends.  DOSBox does a move as one host-side
 * copy, so the flip costs the emulated CPU a few instructions per piece
 * (docs/engine.md).  One buffer instead of four leaves 192 KB of
 * conventional memory to the flight's dense sprite buffers. */
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <malloc.h>
#include <string.h>
#include "rv.h"

#define BAND_ROWS 100

static unsigned char __far *g_buf;      /* the run being drawn: rows g_run_y0 .. */
static int g_run_y0, g_run_y1;
static unsigned char g_dirty[SCR_H];
static unsigned g_gran_kb = 64;         /* window granularity */
static unsigned g_win_seg = 0xA000;
static int g_bank = -1;
static unsigned char g_pal[768];        /* the palette as set, before fading */
static int g_fade = 64;
extern void (*rv_fail_hook)(void);

#pragma pack(1)
static struct {
    unsigned short attr;
    unsigned char win_a, win_b;
    unsigned short gran, size, seg_a, seg_b;
    unsigned long func;
    unsigned short pitch, xres, yres;
    unsigned char rest[238];
} g_mode;
#pragma pack()

static void text_mode(void)
{
    union REGS r;
    r.x.ax = 0x0003;
    int86(0x10, &r, &r);
}

int vid_init(void)
{
    union REGS r;
    struct SREGS s;
    g_buf = (unsigned char __far *)rv_alloc_high((unsigned long)BAND_ROWS * SCR_W);   /* upper memory first (64 KB
                                           of DOSBox's 128 KB of UMBs; the dense slab takes the other half) */
    rv_log("video: the band buffer in %s memory", g_buf ? "upper" : "conventional");
    if (!g_buf)
        g_buf = (unsigned char __far *)_fmalloc((size_t)BAND_ROWS * SCR_W);
    if (!g_buf) {
        rv_log("video: no memory for the band buffer");
        return 0;
    }
    _fmemset(g_buf, 0, (size_t)BAND_ROWS * SCR_W);
    segread(&s);
    s.es = FP_SEG((void __far *)&g_mode);
    r.x.di = FP_OFF((void __far *)&g_mode);
    r.x.ax = 0x4F01;
    r.x.cx = 0x100;
    int86x(0x10, &r, &r, &s);
    if (r.x.ax != 0x004F) {
        rv_log("video: no VESA mode 100h (%04X)", r.x.ax);
        return 0;
    }
    g_gran_kb = g_mode.gran ? g_mode.gran : 64;
    g_win_seg = g_mode.seg_a ? g_mode.seg_a : 0xA000;
    rv_log("video: mode 100h %ux%u, pitch %u, window %04X, granularity %u KB",
           g_mode.xres, g_mode.yres, g_mode.pitch, g_win_seg, g_gran_kb);
    r.x.ax = 0x4F02;
    r.x.bx = 0x100;
    int86(0x10, &r, &r);
    if (r.x.ax != 0x004F) {
        rv_log("video: setting mode 100h failed (%04X)", r.x.ax);
        return 0;
    }
    rv_fail_hook = text_mode;
    g_bank = -1;
    memset(g_dirty, 1, sizeof g_dirty);
    return 1;
}

void vid_shutdown(void)
{
    text_mode();
    rv_fail_hook = 0;
}

unsigned char __far *vid_row(int y)
{
    RV_CHECK(y - g_run_y0, g_run_y1 - g_run_y0);
    return g_buf + (unsigned)(y - g_run_y0) * SCR_W;
}

int vid_run(int *y0, int *y1)
{
    *y0 = g_run_y0;
    *y1 = g_run_y1;
    return g_run_y1 > g_run_y0;
}

void vid_dirty(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (y0 < y1)
        memset(g_dirty + y0, 1, y1 - y0);
}

/* the next run of dirty rows, no longer than the buffer: the caller draws
 * them (vid_row answers for those rows only), then flips before the next */
int vid_dirty_band(int *y0, int *y1)
{
    int y = 0, e;
    while (y < SCR_H && g_dirty[y] != 1)
        y++;
    if (y >= SCR_H)
        return 0;
    e = y;
    while (e < SCR_H && g_dirty[e] == 1 && e - y < BAND_ROWS)
        e++;
    memset(g_dirty + y, 2, e - y);      /* 2: drawn, to be copied */
    g_run_y0 = *y0 = y;
    g_run_y1 = *y1 = e;
    return 1;
}

static void bank(int b)
{
    union REGS r;
    if (b == g_bank)
        return;
    r.x.ax = 0x4F05;
    r.x.bx = 0;
    r.x.dx = (unsigned)(b * (64 / g_gran_kb));
    int86(0x10, &r, &r);
    g_bank = b;
}

/* the run drawn (rows g_run_y0 .. g_run_y1-1) to the screen, in pieces that
 * stay within a bank */
void vid_flip(void)
{
    int y = g_run_y0, e = g_run_y1;
    unsigned long off;
    if (y >= e)
        return;
    memset(g_dirty + y, 0, e - y);
    off = (unsigned long)y * SCR_W;
    while (off < (unsigned long)e * SCR_W) {
        unsigned long end = (unsigned long)e * SCR_W;
        unsigned long bank_end = (off | 0xFFFFUL) + 1;
        unsigned long n = (end < bank_end ? end : bank_end) - off;
        bank((int)(off >> 16));
        xms_copy_conv(MK_FP(g_win_seg, (unsigned)(off & 0xFFFF)), g_buf + (unsigned)(off - (unsigned long)g_run_y0 * SCR_W), n);
        off += n;
    }
    g_run_y0 = g_run_y1 = 0;
}

/* a row of the screen as shown (for screenshots), through the bank window */
void vid_read_row(int y, unsigned char __far *dst)
{
    unsigned long off = (unsigned long)y * SCR_W, bank_end = (off | 0xFFFFUL) + 1;
    unsigned n = bank_end - off < SCR_W ? (unsigned)(bank_end - off) : SCR_W;
    bank((int)(off >> 16));
    xms_copy_conv(dst, MK_FP(g_win_seg, (unsigned)(off & 0xFFFF)), n);
    if (n < SCR_W) {
        bank((int)((off + n) >> 16));
        xms_copy_conv(dst + n, MK_FP(g_win_seg, (unsigned)((off + n) & 0xFFFF)), SCR_W - n);
    }
}

static void put_palette(void)
{
    int i;
    outp(0x3C8, 0);
    for (i = 0; i < 768; i++)
        outp(0x3C9, (unsigned char)((g_pal[i] * g_fade) >> 6));
}

void vid_palette(const unsigned char __far *rgb6, int first, int n)
{
    RV_ASSERT(first >= 0 && first + n <= 256);
    _fmemcpy(g_pal + first * 3, rgb6, n * 3);
    put_palette();
}

void vid_fade(int level)
{
    g_fade = level < 0 ? 0 : level > 64 ? 64 : level;
    put_palette();
}

const unsigned char *shot_palette(void) { return g_pal; }

void vid_wait_retrace(void)
{
    while (inp(0x3DA) & 8);
    while (!(inp(0x3DA) & 8));
}
