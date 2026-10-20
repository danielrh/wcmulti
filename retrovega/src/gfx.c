/* gfx.c: drawing into the back buffer, every call clipped to the rows being
 * drawn (y0 .. y1-1) and to the screen.
 *
 * Images come out of XMS a row (or, full width, a band) at a time straight
 * into the back buffer.  Sprites are run-length rows (tools/rvpack.py):
 * the row table and the rows needed are moved into a scratch buffer, then
 * copied span by span.  Text is drawn from 2-bit glyphs: full coverage in the
 * text colour, partial through the palette's mix tables. */
#include <string.h>
#include <malloc.h>
#include "rv.h"

#define SCRATCH 32768U
static unsigned char __far *g_scratch;
static unsigned char g_shade[256];
static unsigned char g_mix[3][3][256];
static const unsigned char g_text_colour[3] = { UI_WHITE, UI_AMBER4, UI_GREY5 };

typedef struct {
    unsigned char height, ascent, first, count;
    unsigned char __far *table;        /* count x (offset word, width, advance) */
    unsigned char __far *bits;
} Font;
static Font g_font[2];

static void need_scratch(void)
{
    if (!g_scratch) {
        g_scratch = (unsigned char __far *)_fmalloc(SCRATCH);
        RV_ASSERT(g_scratch != 0);
    }
}

unsigned char __far *gfx_scratch(void)
{
    need_scratch();
    return g_scratch;
}

void gfx_use_palette(unsigned a)
{
    const RvAsset __far *as = dat_asset(a);
    unsigned char __far *rgb;
    RV_ASSERT(as->kind == RV_KIND_PAL);
    need_scratch();
    rgb = g_scratch;
    dat_read(as->at, rgb, RV_PAL_BYTES);
    _fmemcpy(g_shade, rgb + RV_PAL_SHADE, 256);
    _fmemcpy(g_mix, rgb + RV_PAL_MIX, sizeof g_mix);
    vid_palette(rgb, 0, 256);
}

void gfx_use_part(unsigned a, int with_text)
{
    const RvAsset __far *as = dat_asset(a);
    unsigned char __far *p;
    unsigned first, n, c, k;
    RV_ASSERT(as->kind == RV_KIND_PALPART);
    need_scratch();
    p = g_scratch;
    dat_read(as->at, p, as->size);
    first = p[0];
    n = p[1];
    RV_ASSERT(first + n <= 256);
    vid_palette(p + 2, first, n);
    if (with_text && as->size >= 2 + n * 3 + 9 * 64) {
        unsigned char __far *m = p + 2 + n * 3;
        for (c = 0; c < 3; c++)
            for (k = 0; k < 3; k++)
                _fmemcpy(g_mix[c][k], m + (c * 3 + k) * 64, 64);
    }
}

void gfx_image(unsigned a, int x, int y, int y0, int y1)
{
    const RvAsset __far *as = dat_asset(a);
    int w = as->w, h = as->h, r, cx0, cx1;
    RV_ASSERT(as->kind == RV_KIND_IMG);
    if (x == AT_ASSET) x = as->x;
    if (y == AT_ASSET) y = as->y;
    if (y0 < y) y0 = y;
    if (y1 > y + h) y1 = y + h;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    cx0 = x < 0 ? 0 : x;
    cx1 = x + w > SCR_W ? SCR_W : x + w;
    if (cx0 >= cx1)
        return;
    for (r = y0; r < y1;) {
        if (cx0 == 0 && cx1 == SCR_W && w == SCR_W) {
            /* full width: the rest of this band in one move */
            int e = r + (100 - r % 100);
            if (e > y1) e = y1;
            dat_read(as->at + (unsigned long)(r - y) * w, vid_row(r), (unsigned long)(e - r) * SCR_W);
            r = e;
        } else {
            dat_read(as->at + (unsigned long)(r - y) * w + (cx0 - x), vid_row(r) + cx0, cx1 - cx0);
            r++;
        }
    }
}

/* the rows r0 .. r1-1 of a sprite into the scratch buffer (r1 comes down
 * when they do not all fit): returns the row table, offsets relative to row
 * r0's data, which starts at *data */
static unsigned long __far *sprite_rows(const RvAsset __far *as, int r0, int *r1, unsigned char __far **data)
{
    unsigned long __far *tab = (unsigned long __far *)g_scratch;
    unsigned long base = as->at + 4UL * (as->h + 1), first;
    unsigned n = *r1 - r0 + 1, room;
    dat_read(as->at + 4UL * r0, tab, 4UL * n);
    /* as many rows as fit after the table */
    room = SCRATCH - 4 * n - 16;
    while (tab[*r1 - r0] - tab[0] > room && *r1 > r0 + 1)
        (*r1)--;
    first = tab[0];
    *data = g_scratch + 4 * n;
    dat_read(base + first, *data, tab[*r1 - r0] - first);
    return tab;
}

static int g_dither;    /* 0: every pixel; 1, 2: only the pixels of one checkerboard colour */

void gfx_sprite_dither(unsigned a, int dx, int dy, int y0, int y1, int which)
{
    g_dither = which;
    gfx_sprite(a, dx, dy, y0, y1);
    g_dither = 0;
}

void gfx_sprite(unsigned a, int dx, int dy, int y0, int y1)
{
    const RvAsset __far *as = dat_asset(a);
    int x, y, r0, r1;
    RV_ASSERT(as->kind == RV_KIND_SPR);
    need_scratch();
    x = as->x + dx;
    y = as->y + dy;
    if (y0 < y) y0 = y;
    if (y1 > y + (int)as->h) y1 = y + as->h;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    r0 = y0 - y;
    while (r0 < y1 - y) {
        int stop = y1 - y, r;
        unsigned char __far *data;
        unsigned long __far *tab = sprite_rows(as, r0, &stop, &data);
        r1 = stop;
        for (r = r0; r < r1; r++) {
            unsigned char __far *p = data + (unsigned)(tab[r - r0] - tab[0]);
            unsigned char __far *dst = vid_row(y + r);
            int cx = x;
            for (;;) {
                unsigned skip = p[0], n = p[1];
                p += 2;
                if (!skip && !n)
                    break;
                cx += skip;
                if (n) {
                    int a0 = cx < 0 ? -cx : 0;
                    int a1 = cx + (int)n > SCR_W ? SCR_W - cx : (int)n;
                    if (a1 > a0) {
                        if (!g_dither) {
                            _fmemcpy(dst + cx + a0, p + a0, a1 - a0);
                        } else {
                            int i = a0 + ((cx + a0 + y + r + g_dither) & 1);
                            for (; i < a1; i += 2)
                                dst[cx + i] = p[i];
                        }
                    }
                    p += n;
                    cx += n;
                }
            }
        }
        r0 = r1;
    }
}

int gfx_sprite_hit(unsigned a, int dx, int dy, int px, int py)
{
    const RvAsset __far *as = dat_asset(a);
    int x = as->x + dx, y = as->y + dy, r, stop, cx;
    unsigned long __far *tab;
    unsigned char __far *p;
    if (px < x || py < y || px >= x + (int)as->w || py >= y + (int)as->h)
        return 0;
    need_scratch();
    r = py - y;
    stop = r + 1;
    tab = sprite_rows(as, r, &stop, &p);
    (void)tab;
    cx = x;
    for (;;) {
        unsigned skip = p[0], n = p[1];
        p += 2;
        if (!skip && !n)
            return 0;
        cx += skip;
        if (px >= cx && px < cx + (int)n)
            return 1;
        p += n;
        cx += n;
    }
}

static int clip(int *x, int *y, int *w, int *h, int y0, int y1)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*x + *w > SCR_W) *w = SCR_W - *x;
    if (*y < y0) { *h -= y0 - *y; *y = y0; }
    if (*y + *h > y1) *h = y1 - *y;
    return *w > 0 && *h > 0;
}

void gfx_fill(int x, int y, int w, int h, unsigned char c, int y0, int y1)
{
    int r;
    if (!clip(&x, &y, &w, &h, y0, y1))
        return;
    for (r = y; r < y + h; r++)
        _fmemset(vid_row(r) + x, c, w);
}

void gfx_shade(int x, int y, int w, int h, int y0, int y1)
{
    int r, i;
    if (!clip(&x, &y, &w, &h, y0, y1))
        return;
    for (r = y; r < y + h; r++) {
        unsigned char __far *p = vid_row(r) + x;
        for (i = 0; i < w; i++)
            p[i] = g_shade[p[i]];
    }
}

void gfx_mix_to_black(int x, int y, int w, int h, int y0, int y1)
{
    gfx_shade(x, y, w, h, y0, y1);
    gfx_shade(x, y, w, h, y0, y1);
}

/* --- text --- */

int gfx_font_load(void)
{
    const RvDatHeader __far *hd = dat_header();
    unsigned ids[2], i;
    ids[0] = hd->font_body;
    ids[1] = hd->font_title;
    for (i = 0; i < 2; i++) {
        const RvAsset __far *as = dat_asset(ids[i]);
        unsigned char __far *p;
        if (g_font[i].table)
            _ffree(g_font[i].table - 4);       /* a new base brings its own */
        p = (unsigned char __far *)_fmalloc((size_t)as->size + 1);
        RV_ASSERT(p != 0 && as->kind == RV_KIND_FONT);
        dat_read(as->at, p, as->size);
        g_font[i].height = p[0];
        g_font[i].ascent = p[1];
        g_font[i].first = p[2];
        g_font[i].count = p[3];
        g_font[i].table = p + 4;
        g_font[i].bits = p + 4 + 4 * p[3];
    }
    return 1;
}

int gfx_font_height(int font) { RV_CHECK(font, 2); return g_font[font].height; }

int gfx_text_width(int font, const char *s)
{
    Font *f;
    int w = 0;
    RV_CHECK(font, 2);
    f = &g_font[font];
    for (; *s; s++) {
        unsigned c = (unsigned char)*s - f->first;
        if (c < f->count)
            w += f->table[c * 4 + 3];
    }
    return w;
}

/* colour: RV_TEXT_*, plus 0x10 for a drop shadow */
int gfx_text(int font, int x, int y, const char *s, int colour, int y0, int y1)
{
    Font *f;
    int start = x, pass;
    RV_CHECK(font, 2);
    f = &g_font[font];
    if (y + f->height <= y0 || y >= y1)
        return gfx_text_width(font, s);
    for (pass = (colour & 0x10) ? 0 : 1; pass < 2; pass++) {
        const char *t = s;
        int c3 = colour & 3, ox = pass ? 0 : 1, oy = pass ? 0 : 1;
        unsigned char solid = pass ? g_text_colour[c3] : UI_BLACK;
        RV_CHECK(c3, 3);
        x = start;
        for (; *t; t++) {
            unsigned c = (unsigned char)*t - f->first;
            unsigned w, adv, gx, gy;
            unsigned char __far *g;
            if (c >= f->count)
                continue;
            g = f->bits + *(unsigned short __far *)(f->table + c * 4);
            w = f->table[c * 4 + 2];
            adv = f->table[c * 4 + 3];
            for (gy = 0; gy < f->height; gy++) {
                int ry = y + oy + (int)gy;
                unsigned char __far *row;
                if (ry < y0 || ry >= y1 || ry < 0 || ry >= SCR_H)
                    continue;
                row = vid_row(ry);
                for (gx = 0; gx < w; gx++) {
                    unsigned v = g[gy * w + gx];
                    int rx = x + ox + (int)gx;
                    if (!v || rx < 0 || rx >= SCR_W)
                        continue;
                    if (!pass) {
                        if (v >= 2)
                            row[rx] = g_shade[g_shade[row[rx]]];
                    } else if (v == 3) {
                        row[rx] = solid;
                    } else {
                        row[rx] = g_mix[c3][v == 1 ? 0 : 2][row[rx]];
                    }
                }
            }
            x += adv;
        }
    }
    return x - start;
}

int gfx_wrap(int font, int x, int y, int w, const char *s, int colour, int line_gap, int y0, int y1)
{
    char line[160];
    int lines = 0, h = gfx_font_height(font) + line_gap;
    while (*s) {
        int n = 0, last_space = -1;
        while (s[n] && n < (int)sizeof line - 1) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (s[n] == ' ')
                last_space = n;
            if (gfx_text_width(font, line) > w && last_space > 0) {
                n = last_space;
                break;
            }
            n++;
        }
        line[n] = 0;
        gfx_text(font, x, y + lines * h, line, colour, y0, y1);
        lines++;
        s += n;
        while (*s == ' ')
            s++;
    }
    return lines;
}

/* --- lines and circles (the situation display) --- */

static void plot(int x, int y, unsigned char c, int y0, int y1)
{
    if (x >= 0 && x < SCR_W && y >= y0 && y < y1 && y >= 0 && y < SCR_H)
        vid_row(y)[x] = c;
}

/* a line; dash > 0 draws dash pixels on, dash off */
void gfx_line(int xa, int ya, int xb, int yb, unsigned char c, int dash, int y0, int y1)
{
    int dx = xb > xa ? xb - xa : xa - xb, sx = xa < xb ? 1 : -1;
    int dy = yb > ya ? ya - yb : yb - ya, sy = ya < yb ? 1 : -1;
    int err = dx + dy, k = 0;
    if ((ya < y0 && yb < y0) || (ya >= y1 && yb >= y1))
        return;
    for (;;) {
        if (!dash || (k / dash) % 2 == 0)
            plot(xa, ya, c, y0, y1);
        k++;
        if (xa == xb && ya == yb)
            break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; xa += sx; }
            if (e2 <= dx) { err += dx; ya += sy; }
        }
    }
}

void gfx_circle(int cx, int cy, int r, unsigned char c, int dotted, int y0, int y1)
{
    int x = r, y = 0, err = 1 - r, k = 0;
    if (cy + r < y0 || cy - r >= y1)
        return;
    while (x >= y) {
        if (!dotted || (k & 2)) {
            plot(cx + x, cy + y, c, y0, y1); plot(cx - x, cy + y, c, y0, y1);
            plot(cx + x, cy - y, c, y0, y1); plot(cx - x, cy - y, c, y0, y1);
            plot(cx + y, cy + x, c, y0, y1); plot(cx - y, cy + x, c, y0, y1);
            plot(cx + y, cy - x, c, y0, y1); plot(cx - y, cy - x, c, y0, y1);
        }
        k++;
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_frame(int x, int y, int w, int h, unsigned char c, int y0, int y1)
{
    gfx_fill(x, y, w, 1, c, y0, y1);
    gfx_fill(x, y + h - 1, w, 1, c, y0, y1);
    gfx_fill(x, y, 1, h, c, y0, y1);
    gfx_fill(x + w - 1, y, 1, h, c, y0, y1);
}
