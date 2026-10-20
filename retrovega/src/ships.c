/* ships.c: a ship type's views in XMS, and the blitter (ships.h).
 *
 * SHIPnn.RV (tools/pack.py) is a WC container: block 0 holds the views as
 * WC's run-length sprite code, block 1 the 'RVSH' record.  Each view is
 * decoded into a line buffer a row at a time and re-encoded as gfx.c's rows
 * (skip, count, pixels ... 0, 0) with a table of row offsets, into the
 * mission's XMS level.  A symmetric ship's 25 missing headings (210..330)
 * are the mirror images of 150..30, made here, so every set has 62 views
 * and the flight never flips a sprite.  A view's raw bytes sit in gfx.c's
 * scratch while it is decoded, twelve rows at a time, into a band buffer;
 * the rows go out through the scratch's free end.  Drawing fetches a view's
 * rows into the same scratch with one XMS move and hands each row to
 * rv_blit_row (lowlevel.asm), clipped to the cockpit opening's extent on
 * that row. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "ships.h"
#include "lightfield.h"
#include "music.h"
#define RV_TABLES
#include "rvdata.h"

#define SCRATCH 32768U                  /* gfx.c's scratch: a view's raw bytes, then the rows going out */
#define LINE 1024
#define BAND_ROWS 12                    /* rows decoded at a time */
#define OUT_MIN 4096U                   /* the least room kept for the rows going out */

static ShipSet __far *g_set[SHIP_SETS];
static int g_nsets;
static unsigned char __far *g_band;     /* BAND_ROWS x LINE pixels, 0 = clear (no view uses colour 0) */

/* the camera */
static int g_cx = SCR_W / 2, g_cy = SCR_H / 2, g_focal = 400;

/* one view expanded dense (w x h, pitch w) for the turned draw */
#define DENSE_MAX 65024U
static unsigned char __far *g_dense;
static int g_dense_set = -1, g_dense_view = -1;
static unsigned short __far *g_cmap;    /* the scaled draw's column map, LINE entries */
static unsigned char __far *g_line;     /* one source row expanded, LINE pixels */

void __far __cdecl rv_blit_row(unsigned char __far *row, const unsigned char __far *spans, int left, int xa, int xb);
void __far __cdecl rv_scale_row(const unsigned char __far *line, const unsigned short __far *map, int xa, int xb,
                                unsigned char __far *out);
void __far __cdecl rv_rotate_row(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                 unsigned char __far *out);

/* --- loading --- */

static unsigned long rd32(FILE *f)
{
    unsigned char b[4];
    fread(b, 1, 4, f);
    return b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

/* the records of a view (its raw bytes at buf) for rows r0 .. r0+n-1 into
 * the band buffer, w wide; the origin's column is x1 + 1, its row y1 + 1 */
static void decode_band(const unsigned char __far *buf, unsigned long len, int w, int x1, int y1, int r0, int n)
{
    unsigned long off = 8;
    _fmemset(g_band, 0, (unsigned)n * w);
    while (off + 6 <= len) {
        short key = *(const short __far *)(buf + off), vx = *(const short __far *)(buf + off + 2);
        short vy = *(const short __far *)(buf + off + 4);
        int row = vy + y1 + 1 - r0, x = vx + x1 + 1, cnt = key / 2, k;
        unsigned char __far *line = row >= 0 && row < n ? g_band + (unsigned)row * w : 0;
        off += 6;
        if (key == 0)
            break;
        if (key % 2 == 0) {
            if (line)
                _fmemcpy(line + x, buf + off, cnt);
            off += cnt;
        } else {
            int got = 0;
            while (got < cnt && off < len) {
                k = buf[off++];
                if (k % 2 == 0) {
                    if (line)
                        _fmemcpy(line + x + got, buf + off, k / 2);
                    off += k / 2;
                } else {
                    unsigned char c = buf[off++];
                    if (line)
                        _fmemset(line + x + got, c, k / 2);
                }
                got += k / 2;
            }
        }
    }
}

/* one copy of a view (mirrored or not) out as run-length rows into XMS at
 * `at`, through the scratch's free end: fills the row table, returns the
 * bytes written */
static unsigned long encode_view(XmsBlock blk, unsigned long at, const unsigned char __far *raw, unsigned long len,
                                 int w, int h, int x1, int y1, int mirror, unsigned long __far *tab)
{
    unsigned char __far *out = gfx_scratch() + (unsigned)((len + 15) & ~15UL);
    unsigned room = SCRATCH - (unsigned)((len + 15) & ~15UL), n = 0;
    unsigned long start = at;
    int r0, y;
    for (r0 = 0; r0 < h; r0 += BAND_ROWS) {
        int rows = h - r0 < BAND_ROWS ? h - r0 : BAND_ROWS;
        decode_band(raw, len, w, x1, y1, r0, rows);
        for (y = 0; y < rows; y++) {
            const unsigned char __far *line = g_band + (unsigned)y * w;
            int x = 0;
            tab[r0 + y] = at + n - start;
            while (x < w) {
                int skip = 0, run = 0, i;
                while (x < w && !line[mirror ? w - 1 - x : x]) { x++; skip++; }
                if (x >= w)
                    break;
                while (x + run < w && line[mirror ? w - 1 - x - run : x + run] && run < 255)
                    run++;
                if (n + skip / 255 * 2 + 4 + run + 2 > room) {
                    xms_put(blk, at, out, n);
                    at += n;
                    n = 0;
                }
                while (skip > 255) {
                    out[n++] = 255; out[n++] = 0;
                    skip -= 255;
                }
                out[n++] = (unsigned char)skip;
                out[n++] = (unsigned char)run;
                for (i = 0; i < run; i++)
                    out[n + i] = line[mirror ? w - 1 - x - i : x + i];
                n += run;
                x += run;
            }
            out[n++] = 0;
            out[n++] = 0;
        }
    }
    tab[h] = at + n - start;
    if (n)
        xms_put(blk, at, out, n);
    return at + n - start;
}

int ships_find(unsigned type)
{
    int i;
    for (i = 0; i < g_nsets; i++)
        if (g_set[i]->type == type)
            return i;
    return -1;
}

int ships_load(unsigned type)
{
    char name[16];
    FILE *f;
    unsigned long total, b0, b1, n, i, at, size, tab0[64];
    unsigned long __far *rowtab;
    unsigned char __far *raw = gfx_scratch();
    ShipSet __far *s;
    unsigned char rec[12];
    int found = ships_find(type), v;
    if (found >= 0)
        return found;
    if (g_nsets >= SHIP_SETS)
        return -1;
    sprintf(name, "SHIP%02X.RV", type);
    f = fopen(name, "rb");
    if (!f) {
        rv_log("ships: no %s", name);
        return -1;
    }
    if (!g_band) g_band = (unsigned char __far *)_fmalloc((size_t)BAND_ROWS * LINE);
    RV_ASSERT(g_band != 0);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    total = rd32(f);
    b0 = rd32(f) & 0xFFFFFFUL;
    b1 = rd32(f) & 0xFFFFFFUL;
    (void)total;
    fseek(f, b1, SEEK_SET);
    fread(rec, 1, 12, f);
    if (memcmp(rec, "RVSH", 4) != 0) {
        rv_log("ships: %s is not a ship file", name);
        fclose(f);
        return -1;
    }
    s = (ShipSet __far *)_fmalloc(sizeof(ShipSet));
    RV_ASSERT(s != 0);
    _fmemset(s, 0, sizeof(ShipSet));
    s->type = rec[4];
    s->nviews = rec[5];
    s->asym = rec[6] & 1;
    s->livery = rec[7];
    s->lod0 = rec[8] | (rec[9] << 8);
    /* block 0: the views' container */
    fseek(f, b0, SEEK_SET);
    total = rd32(f);
    n = (rd32(f) & 0xFFFFFFUL);
    n = (n - 4) / 4;                    /* the first offset says how long the table is */
    if (n > 64) n = 64;
    fseek(f, b0 + 4, SEEK_SET);
    for (i = 0; i < n; i++)
        tab0[i] = b0 + (rd32(f) & 0xFFFFFFUL);
    if (s->nviews > n) s->nviews = (unsigned char)n;
    /* the mission level: the file's size three times over covers the mirrored copies */
    s->blk = xms_alloc(size * 3 + 65536UL, XMS_MISSION);
    if (!s->blk) {
        rv_log("ships: no XMS for %s", name);
        _ffree(s);
        fclose(f);
        return -1;
    }
    rowtab = (unsigned long __far *)_fmalloc(4 * 1025);
    RV_ASSERT(rowtab != 0);
    at = 0;
    for (v = 0; v < s->nviews; v++) {
        short hdr[4];
        int w, h, copies = 1, c;
        unsigned long len = (i = v + 1) < n ? tab0[i] - tab0[v] : b0 + total - tab0[v], k;
        if (len > SCRATCH - OUT_MIN) {
            rv_log("ships: %s view %d is %lu bytes, too big to decode", name, v, len);
            continue;
        }
        /* the view's bytes into the scratch, 1 KB at a time (fread wants a near buffer) */
        fseek(f, tab0[v], SEEK_SET);
        for (k = 0; k < len; k += 1024) {
            static unsigned char tmp[1024];
            unsigned part = len - k < 1024 ? (unsigned)(len - k) : 1024;
            fread(tmp, 1, part, f);
            _fmemcpy(raw + k, tmp, part);
        }
        _fmemcpy(hdr, raw, 8);          /* x2, x1, y1, y2 */
        w = hdr[1] + hdr[0] + 2;
        h = hdr[2] + hdr[3] + 2;
        if (w > LINE || h > 1024) {
            rv_log("ships: %s view %d is %dx%d, too big to decode", name, v, w, h);
            continue;
        }
        /* a symmetric ship's band views at 30..150 degrees also make the
         * mirrored heading: 150 -> 210, 120 -> 240, 90 -> 270, 60 -> 300, 30 -> 330 */
        if (!s->asym && v >= 1 && v <= 35 && (v - 1) % 7 >= 1 && (v - 1) % 7 <= 5)
            copies = 2;
        for (c = 0; c < copies; c++) {
            int idx = v;
            ShipView __far *view;
            if (c == 1) {
                int band = (v - 1) / 7, hh = (v - 1) % 7;    /* hh 1..5 -> 330..210 */
                idx = 37 + band * 5 + (5 - hh);
            }
            view = &s->view[idx];
            view->w = (unsigned short)w;
            view->h = (unsigned short)h;
            view->ox = (short)(c ? w - 1 - (hdr[1] + 1) : hdr[1] + 1);
            view->oy = (short)(hdr[2] + 1);
            view->at = at;
            at += 4UL * (h + 1);
            at += encode_view(s->blk, at, raw, len, w, h, hdr[1], hdr[2], c, rowtab);
            xms_put(s->blk, view->at, rowtab, 4UL * (h + 1));
            at = (at + 1) & ~1UL;
        }
    }
    if (!s->asym)
        s->nviews = SHIP_VIEWS;
    _ffree(rowtab);
    fclose(f);
    rv_log("ships: %s, %d views, %lu KB in XMS", name, s->nviews, at / 1024);
    g_set[g_nsets] = s;
    return g_nsets++;
}

const ShipSet __far *ships_set(int i)
{
    RV_CHECK(i, g_nsets);
    return g_set[i];
}

void ships_unload(void)
{
    int i;
    for (i = 0; i < g_nsets; i++)
        _ffree(g_set[i]);
    g_nsets = 0;
}

/* --- drawing --- */

void ships_set_camera(int cx, int cy, int focal)
{
    g_cx = cx;
    g_cy = cy;
    g_focal = focal;
}

/* rows r0 .. *r1-1 of a view into the scratch (one XMS move for the table,
 * one for the rows; *r1 comes down when they do not all fit): returns the
 * row table, offsets relative to row r0's data, which starts at *data */
static unsigned long __far *fetch_rows(const ShipSet __far *s, const ShipView __far *view, int r0, int *r1,
                                       unsigned char __far **data)
{
    unsigned char __far *scratch = gfx_scratch();
    unsigned long __far *tab = (unsigned long __far *)scratch;
    unsigned n = *r1 - r0 + 1, room;
    xms_get(s->blk, view->at + 4UL * r0, tab, 4UL * n);
    room = SCRATCH - 4 * n - 16;
    while (tab[*r1 - r0] - tab[0] > room && *r1 > r0 + 1)
        (*r1)--;
    *data = scratch + 4 * n;
    xms_get(s->blk, view->at + 4UL * (view->h + 1) + tab[0], *data, tab[*r1 - r0] - tab[0]);
    return tab;
}

/* a row's clip: [xa, xb) and the opening's extent on row ry; 0 when nothing is left */
static int row_clip(int ry, int *ca, int *cb, const short __far *ext, int ext_y0, int ext_rows)
{
    if (ext) {
        const short __far *e = ext + 2 * (ry - ext_y0);
        if (ry < ext_y0 || ry >= ext_y0 + ext_rows)
            return 0;
        if (e[0] > *ca) *ca = e[0];
        if (e[1] < *cb) *cb = e[1];
    }
    return *ca < *cb;
}

/* path 1: the view's rows copied, span by span (rv_blit_row) */
void ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
                const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipView __far *view;
    int top, left, r0, r1;
    RV_CHECK(v, SHIP_VIEWS);
    view = &s->view[v];
    if (!view->w)
        return;
    top = y - view->oy;
    left = x - view->ox;
    if (y0 < top) y0 = top;
    if (y1 > top + (int)view->h) y1 = top + view->h;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || left + (int)view->w <= xa)
        return;
    r0 = y0 - top;
    while (r0 < y1 - top) {
        unsigned long __far *tab;
        unsigned char __far *data, __far *dst;
        int r;
        r1 = y1 - top;
        tab = fetch_rows(s, view, r0, &r1, &data);
        dst = vid_row(top + r0);
        for (r = r0; r < r1; r++, dst += SCR_W) {
            int ca = xa, cb = xb;
            if (!row_clip(top + r, &ca, &cb, ext, ext_y0, ext_rows))
                continue;
            rv_blit_row(dst, data + (unsigned)(tab[r - r0] - tab[0]), left, ca, cb);
        }
        r0 = r1;
    }
}

/* a run-length row expanded into a line of w pixels (0 where clear) */
static void expand_row(const unsigned char __far *p, unsigned char __far *line, int w)
{
    int x = 0;
    _fmemset(line, 0, w);
    for (;;) {
        unsigned skip = p[0], n = p[1];
        p += 2;
        if (!skip && !n)
            break;
        x += skip;
        if (n) {
            _fmemcpy(line + x, p, n);
            p += n;
            x += n;
        }
    }
}

static void need_buffers(void)
{
    if (!g_cmap) g_cmap = (unsigned short __far *)_fmalloc(2 * LINE);
    if (!g_line) g_line = (unsigned char __far *)_fmalloc(LINE);
    RV_ASSERT(g_cmap && g_line);
}

/* path 2: the view scaled as it is copied: each output row expands its
 * source row once, then maps columns straight into the back buffer */
static void draw_scaled(const ShipSet __far *s, int v, int x, int y, int scale256, int y0, int y1, int xa, int xb,
                        const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipView __far *view = &s->view[v];
    int w = view->w, h = view->h, ow, oh, top, left, oy, r0 = 0, r1 = 0;
    unsigned long __far *tab = 0;
    unsigned char __far *data = 0;
    if (!w)
        return;
    need_buffers();
    ow = (int)(((long)w * scale256 + 255) / 256);
    oh = (int)(((long)h * scale256 + 255) / 256);
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    if (ow > LINE) ow = LINE;
    top = y - (int)((long)view->oy * scale256 / 256);
    left = x - (int)((long)view->ox * scale256 / 256);
    if (y0 < top) y0 = top;
    if (y1 > top + oh) y1 = top + oh;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || left + ow <= xa)
        return;
    if (xa < left) xa = left;
    if (xb > left + ow) xb = left + ow;
    lf_colmap(w, ow, g_cmap);
    for (oy = y0; oy < y1; oy++) {
        int sy = (int)((long)(oy - top) * 256 / scale256), ca = xa, cb = xb;
        if (sy >= h) sy = h - 1;
        if (!row_clip(oy, &ca, &cb, ext, ext_y0, ext_rows))
            continue;
        if (sy >= r1) {
            /* the next chunk of source rows, from the one wanted */
            r0 = sy;
            r1 = h;
            tab = fetch_rows(s, view, r0, &r1, &data);
        }
        expand_row(data + (unsigned)(tab[sy - r0] - tab[0]), g_line, w);
        rv_scale_row(g_line, g_cmap, ca - left, cb - left, vid_row(oy) + left);
    }
}

/* the whole view dense in g_dense: 0 when it does not fit */
static int make_dense(const ShipSet __far *s, int set_i, int v)
{
    const ShipView __far *view = &s->view[v];
    int w = view->w, h = view->h, r0 = 0;
    if (g_dense_set == set_i && g_dense_view == v)
        return 1;
    if (!w || (unsigned long)w * h > DENSE_MAX)
        return 0;
    if (!g_dense) g_dense = (unsigned char __far *)_fmalloc(DENSE_MAX);
    RV_ASSERT(g_dense != 0);
    while (r0 < h) {
        unsigned long __far *tab;
        unsigned char __far *data;
        int r1 = h, r;
        tab = fetch_rows(s, view, r0, &r1, &data);
        for (r = r0; r < r1; r++)
            expand_row(data + (unsigned)(tab[r - r0] - tab[0]), g_dense + (unsigned)r * w, w);
        r0 = r1;
    }
    g_dense_set = set_i;
    g_dense_view = v;
    return 1;
}

/* path 3: the view turned and scaled by inverse mapping from a dense copy */
static int draw_turned(const ShipSet __far *s, int set_i, int v, int x, int y, int scale256, int roll, int y0, int y1,
                       int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipView __far *view = &s->view[v];
    LfRot r;
    int top, left, oy;
    if (!make_dense(s, set_i, v))
        return 0;
    lf_rotate_setup(view->w, view->h, view->ox, view->oy, roll, scale256, &r);
    top = y - r.ocy;
    left = x - r.ocx;
    if (y0 < top) y0 = top;
    if (y1 > top + r.oh) y1 = top + r.oh;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || left + r.ow <= xa)
        return 1;
    if (xa < left) xa = left;
    if (xb > left + r.ow) xb = left + r.ow;
    for (oy = y0; oy < y1; oy++) {
        int ca = xa, cb = xb, xs, xe;
        if (!row_clip(oy, &ca, &cb, ext, ext_y0, ext_rows))
            continue;
        if (lf_rotate_span(&r, view->w, view->h, oy - top, ca - left, cb - left, &xs, &xe))
            rv_rotate_row(g_dense, view->w, view->h, &r, oy - top, xs, xe, vid_row(oy) + left);
    }
    return 1;
}

/* the asm kernels against the C ones (lightfield.c), on a small picture at
 * several turns and scales: returns the pixels compared, or -1 on a mismatch */
long ships_selftest(void)
{
    static unsigned char __far pic[9 * 7];
    static unsigned char __far out_c[64], __far out_a[64];
    static unsigned short __far map[64];
    long compared = 0;
    int i, roll, sc, y, x;
    for (i = 0; i < 9 * 7; i++)
        pic[i] = (unsigned char)((i * 7 + 3) % 11 ? 1 + i % 13 : 0);
    lf_colmap(9, 23, map);
    for (x = 0; x < 64; x++) out_c[x] = out_a[x] = (unsigned char)(200 + x);
    lf_scale_row(pic + 9 * 3, map, 2, 21, out_c);
    rv_scale_row(pic + 9 * 3, map, 2, 21, out_a);
    for (x = 0; x < 64; x++, compared++)
        if (out_c[x] != out_a[x])
            return -1;
    for (roll = 0; roll < 1024; roll += 64)
        for (sc = 96; sc <= 320; sc += 112) {
            LfRot r;
            lf_rotate_setup(9, 7, 4, 3, roll, sc, &r);
            if (r.ow > 60)
                continue;
            for (y = 0; y < r.oh; y++) {
                int xs, xe;
                for (x = 0; x < 64; x++) out_c[x] = out_a[x] = (unsigned char)(200 + x);
                lf_rotate_row(pic, 9, 7, &r, y, 0, r.ow, out_c);
                if (lf_rotate_span(&r, 9, 7, y, 0, r.ow, &xs, &xe))
                    rv_rotate_row(pic, 9, 7, &r, y, xs, xe, out_a);
                for (x = 0; x < 64; x++, compared++)
                    if (out_c[x] != out_a[x])
                        return -1;
            }
        }
    return compared;
}

int ships_draw(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
               int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    int rq;
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, &p);
    if (!p.on_screen || scale256 < 8)
        return 0;
    rq = ((p.roll + 32) & 1023) / 64;   /* sixteen steps of a turn */
    if (rq == 0 && scale256 == 256) {
        ships_blit(s, p.view, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        return 1;
    }
    if (rq == 0 || !draw_turned(s, set_i, p.view, p.sx, p.sy, scale256, rq * 64, y0, y1, xa, xb, ext, ext_y0, ext_rows)) {
        draw_scaled(s, p.view, p.sx, p.sy, scale256, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        return 2;
    }
    return 3;
}

/* --- the benchmark of the paths --- */

void ships_time_paths(int set_i, int v, long out[3])
{
    const ShipSet __far *s = ships_set(set_i);
    const ShipView __far *view = &s->view[v];
    int y0, y1, k, rows, i;
    long px;
    vid_dirty(0, 100);
    vid_dirty_band(&y0, &y1);
    for (k = 0; k < 3; k++) {
        int n = k == 0 ? 600 : k == 1 ? 240 : 120, scale = k == 0 ? 256 : 192;
        unsigned long t0, dt;
        LfRot r;
        if (k == 2) {
            lf_rotate_setup(view->w, view->h, view->ox, view->oy, 128, scale, &r);
            rows = r.oh < 100 ? r.oh : 100;
            px = (long)r.ow * rows;
        } else {
            int ow = (int)((long)view->w * scale / 256), oh = (int)((long)view->h * scale / 256);
            rows = oh < 100 ? oh : 100;
            px = (long)ow * rows;
        }
        t0 = clock240();
        for (i = 0; i < n; i++) {
            /* the origin so that the sprite's top is row 0 */
            int oy = k == 2 ? r.ocy : (int)((long)view->oy * scale / 256), ox = SCR_W / 2;
            if (k == 0)
                ships_blit(s, v, ox, oy, y0, y1, 0, SCR_W, 0, 0, 0);
            else if (k == 1)
                draw_scaled(s, v, ox, oy, scale, y0, y1, 0, SCR_W, 0, 0, 0);
            else
                draw_turned(s, set_i, v, ox, oy, scale, 128, y0, y1, 0, SCR_W, 0, 0, 0);
        }
        dt = clock240() - t0;
        out[k] = dt ? px * n * 24 / (long)(dt * 100) : 0;   /* pixels a millisecond: ticks are 1000/240 ms */
    }
    vid_flip();
}
