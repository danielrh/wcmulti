/* sky.c: the far things on the sky (sky.h).
 *
 * SKY.DAT (tools/sky.py): a record for each thing (its turns kept, the turns
 * a whole turn, its symmetry, its layer), then each kept turn's header (its
 * size, its middle, where its row table is), then the turns' sprites in the
 * ships' run-length format; all but the records go into resident XMS, where
 * ships_blit_view reads them.  A frame takes the turn nearest the thing's up
 * on the screen (lf_sky's roll): turn k of n is drawn as kept turn k (or, its
 * second half, kept turn k - n/2 upside down and mirrored: a half turn); a
 * thing that is its own mirror image keeps turns 0 .. n/4, and turns past
 * them are a kept one flipped (n/2 - k), both (k - n/2) or mirrored (n - k).
 * The module's data and its log's formats are far (DGROUP is nearly full). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rv.h"
#include "music.h"
#include "ships.h"
#include "sky.h"

#define SKY_KINDS 8
#define FILE_HEAD 12

typedef struct {                        /* SKY.DAT's record of a thing (16 bytes) */
    unsigned short kept, turns;
    unsigned char sym, layer;
    unsigned short box;
    unsigned long heads;
    unsigned char step, rows;           /* its rows kept: 1 all, 2 every other; drawn: 1 all, 2 doubled, 3 interlaced */
    unsigned short pad;
} SkyKind;

typedef struct {                        /* a kept turn's header in XMS (12 bytes) */
    unsigned short w, h;
    short ox, oy;
    unsigned long at;
} SkyHead;

static XmsBlock __far g_blk;
static SkyKind __far g_kind[SKY_KINDS];
static int __far g_kinds;
static struct { unsigned char thing; LfVec dir, up; } __far g_on[SKY_MAX];
static int __far g_non;
static struct { ShipView v; short x, y; unsigned char flags, layer, step, rows; } __far g_drawn[SKY_MAX];
static int __far g_rows_all;            /* 0: each thing's own way; else every thing so (sky_rows) */
static int __far g_ndrawn;
static unsigned long __far g_t_place, __far g_t_draw;

int sky_reserve(void)
{
    static const char __far s_magic[] = "RVSKY", __far s_none[] = "sky: no SKY.DAT",
        __far s_other[] = "sky: SKY.DAT is not this build's",
        __far s_in[] = "sky: %d things, %lu KB in XMS";
    FILE *f = fopen("SKY.DAT", "rb");
    unsigned char *buf = rv_filebuf;
    unsigned long size, at;
    int k;
    g_blk = 0;
    g_kinds = 0;
    if (!f) {
        rv_logf(s_none);
        return 0;
    }
    fread(buf, 1, FILE_HEAD, f);
    k = buf[10] | buf[11] << 8;
    if (_fmemcmp(buf, s_magic, 6) || (buf[8] | buf[9] << 8) != 1 || k < 1 || k > SKY_KINDS) {
        rv_logf(s_other);
        fclose(f);
        return 0;
    }
    fread(buf, sizeof(SkyKind), k, f);
    _fmemcpy(g_kind, buf, k * sizeof(SkyKind));
    fseek(f, 0, SEEK_END);
    size = ftell(f) - FILE_HEAD - k * sizeof(SkyKind);
    fseek(f, FILE_HEAD + k * sizeof(SkyKind), SEEK_SET);
    g_blk = xms_alloc(size + 2, XMS_RESIDENT);
    if (!g_blk) {
        fclose(f);
        return 0;
    }
    for (at = 0; at < size; at += sizeof rv_filebuf) {
        unsigned part = size - at < sizeof rv_filebuf ? (unsigned)(size - at) : sizeof rv_filebuf;
        fread(buf, 1, part, f);
        xms_put(g_blk, at, buf, part);
    }
    fclose(f);
    g_kinds = k;
    rv_logf(s_in, k, size / 1024);
    return 1;
}

int sky_things(void) { return g_kinds; }

void sky_rows(int rows) { g_rows_all = rows; }

void sky_clear(void)
{
    g_non = g_ndrawn = 0;
    g_t_place = g_t_draw = 0;
}

int sky_add(int thing, const LfVec *dir, const LfVec *up)
{
    long m = labs(up->x) > labs(up->y) ? labs(up->x) : labs(up->y);
    if (labs(up->z) > m)
        m = labs(up->z);
    if (g_non >= SKY_MAX || thing < 0 || thing >= g_kinds || !m)
        return 0;
    g_on[g_non].thing = (unsigned char)thing;
    g_on[g_non].dir = *dir;
    g_on[g_non].up.x = up->x * 256 / m; /* (256 at most: lf_atan2's products overflow past 2^17) */
    g_on[g_non].up.y = up->y * 256 / m;
    g_on[g_non].up.z = up->z * 256 / m;
    g_non++;
    return 1;
}

void sky_place(const LfBasis *cam, int cx, int cy, int focal)
{
    unsigned long t0 = clock240();
    int i;
    g_ndrawn = 0;
    for (i = 0; i < g_non; i++) {
        const SkyKind __far *kd = &g_kind[g_on[i].thing];
        LfVec dir = g_on[i].dir, up = g_on[i].up;
        LfPlace p;
        SkyHead hd;
        int n = kd->turns, k, j, flags = 0;
        lf_sky(&dir, &up, cam, cx, cy, focal, &p);
        if (!p.on_screen)
            continue;
        k = (int)(((long)p.roll * n + 512) / 1024) % n;   /* the nearest turn, clockwise */
        if (kd->sym == 0) {             /* halves: the second is the first upside down and mirrored */
            j = k;
            if (k >= n / 2) {
                j = k - n / 2;
                flags = BLIT_H | BLIT_V;
            }
        } else if (k <= n / 4) {        /* quarters: its own mirror image */
            j = k;
        } else if (k <= n / 2) {
            j = n / 2 - k;
            flags = BLIT_V;
        } else if (k <= 3 * n / 4) {
            j = k - n / 2;
            flags = BLIT_H | BLIT_V;
        } else {
            j = n - k;
            flags = BLIT_H;
        }
        if (j >= kd->kept)
            j = kd->kept - 1;

        xms_get(g_blk, kd->heads + (unsigned long)j * sizeof hd, &hd, sizeof hd);
        g_drawn[g_ndrawn].v.w = hd.w;
        g_drawn[g_ndrawn].v.h = hd.h;
        g_drawn[g_ndrawn].v.ox = hd.ox;
        g_drawn[g_ndrawn].v.oy = hd.oy;
        g_drawn[g_ndrawn].v.blk = g_blk;
        g_drawn[g_ndrawn].v.at = hd.at;
        g_drawn[g_ndrawn].v.len = 0;
        g_drawn[g_ndrawn].x = (short)p.sx;
        g_drawn[g_ndrawn].y = (short)p.sy;
        g_drawn[g_ndrawn].flags = (unsigned char)flags;
        g_drawn[g_ndrawn].layer = kd->layer;
        g_drawn[g_ndrawn].step = kd->step == 2 ? 2 : 1;
        g_drawn[g_ndrawn].rows = (unsigned char)(g_rows_all ? g_rows_all : kd->rows ? kd->rows : 1);
        g_ndrawn++;
    }
    g_t_place += clock240() - t0;
}

void sky_draw(int layer, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    unsigned long t0 = clock240();
    int i;
    for (i = 0; i < g_ndrawn; i++)
        if (g_drawn[i].layer == layer)
            ships_blit_rows(&g_drawn[i].v, g_drawn[i].flags, g_drawn[i].step, g_drawn[i].rows, g_drawn[i].x,
                            g_drawn[i].y, y0, y1, 0, SCR_W, ext, ext_y0, ext_rows);
    g_t_draw += clock240() - t0;
}

void sky_ticks(unsigned long out[2])
{
    out[0] = g_t_place;
    out[1] = g_t_draw;
}
