/* video.c: VESA mode 100h (640x400x256), the back buffer and the flip.
 *
 * The back buffer is four far blocks of 100 rows (64,000 bytes each), so a
 * row never straddles a segment.  Drawing marks rows dirty; vid_flip copies
 * each dirty run to the screen through the 64 KB bank window with XMS moves
 * (handle 0 to handle 0), split where a bank or a band ends.  DOSBox does a
 * move as one host-side copy, so the flip costs the emulated CPU a few
 * instructions per piece (docs/engine.md). */
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <malloc.h>
#include <string.h>
#include "rv.h"

#define BAND_ROWS 100
#define BANDS (SCR_H / BAND_ROWS)

static unsigned char __far *g_band[BANDS];
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
    int i;
    for (i = 0; i < BANDS; i++) {
        g_band[i] = (unsigned char __far *)_fmalloc((size_t)BAND_ROWS * SCR_W);
        if (!g_band[i]) {
            rv_log("video: no memory for the back buffer");
            return 0;
        }
        _fmemset(g_band[i], 0, (size_t)BAND_ROWS * SCR_W);
    }
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
    RV_CHECK(y, SCR_H);
    return g_band[y / BAND_ROWS] + (unsigned)(y % BAND_ROWS) * SCR_W;
}

void vid_dirty(int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (y0 < y1)
        memset(g_dirty + y0, 1, y1 - y0);
}

/* the next run of dirty rows, no longer than a band: the caller draws them */
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
    *y0 = y;
    *y1 = e;
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

void vid_flip(void)
{
    int y = 0;
    while (y < SCR_H) {
        int e;
        unsigned long off;
        if (g_dirty[y] != 2) {
            y++;
            continue;
        }
        e = y;
        while (e < SCR_H && g_dirty[e] == 2 && (e == y || e % BAND_ROWS))
            e++;
        memset(g_dirty + y, 0, e - y);
        /* rows y .. e-1, one band: copy in pieces that stay within a bank */
        off = (unsigned long)y * SCR_W;
        while (y < e) {
            unsigned long end = (unsigned long)e * SCR_W;
            unsigned long bank_end = (off | 0xFFFFUL) + 1;
            unsigned long n = (end < bank_end ? end : bank_end) - off;
            unsigned char __far *src = g_band[y / BAND_ROWS] + (unsigned)(off - (unsigned long)(y / BAND_ROWS) * BAND_ROWS * SCR_W);
            bank((int)(off >> 16));
            xms_copy_conv(MK_FP(g_win_seg, (unsigned)(off & 0xFFFF)), src, n);
            off += n;
            y = (int)(off / SCR_W);
            if (off % SCR_W) {
                /* a row straddles the bank boundary: finish it */
                unsigned long rest = SCR_W - off % SCR_W;
                src = g_band[y / BAND_ROWS] + (unsigned)(off - (unsigned long)(y / BAND_ROWS) * BAND_ROWS * SCR_W);
                bank((int)(off >> 16));
                xms_copy_conv(MK_FP(g_win_seg, (unsigned)(off & 0xFFFF)), src, rest);
                off += rest;
                y++;
            }
        }
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
