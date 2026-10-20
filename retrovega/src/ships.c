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
#define RV_TABLES
#include "rvdata.h"

#define SCRATCH 32768U                  /* gfx.c's scratch: a view's raw bytes, then the rows going out */
#define LINE 1024
#define BAND_ROWS 12                    /* rows decoded at a time */
#define OUT_MIN 4096U                   /* the least room kept for the rows going out */

static ShipSet __far *g_set[SHIP_SETS];
static int g_nsets;
static unsigned char __far *g_band;     /* BAND_ROWS x LINE pixels, 0 = clear (no view uses colour 0) */

void __far __cdecl rv_blit_row(unsigned char __far *row, const unsigned char __far *spans, int left, int xa, int xb);

/* --- trigonometry --- */

long rv_sin(int a)
{
    a &= 1023;
    if (a < 256) return rv_qsin[a];
    if (a < 512) return rv_qsin[512 - a];
    if (a < 768) return -rv_qsin[a - 512];
    return -rv_qsin[1024 - a];
}

long rv_cos(int a) { return rv_sin(a + 256); }

unsigned long rv_sqrt(unsigned long v)
{
    unsigned long r = 0, bit = 1UL << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* atan2 in 1024ths of a turn, by octant and the quarter table's inverse */
int rv_atan2(long y, long x)
{
    long ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, t;
    int a, lo = 0, hi = 256;
    if (!ax && !ay)
        return 0;
    /* tan(a) = min/max in [0, 1]: find a with sin(a)/cos(a) = t, a in 0..128 */
    t = (ay < ax ? ay : ax) * 16384L / (ay < ax ? ax : ay);
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        /* sin(mid) / cos(mid) <= t ?  (cos = qsin[256 - mid]) */
        if ((long)rv_qsin[mid] <= (t * rv_qsin[256 - mid]) >> 14)
            lo = mid;
        else
            hi = mid;
    }
    a = ay < ax ? lo : 256 - lo;        /* from the x axis */
    if (x < 0) a = 512 - a;
    if (y < 0) a = 1024 - a;
    return a & 1023;
}

/* --- the view for a direction --- */

int ships_view_of(long vx, long vy, long vz)
{
    long r = (long)rv_sqrt((unsigned long)(vx * vx + vz * vz));
    int elev = rv_atan2(vy, r);         /* 0..1023, 256 = straight above */
    int head = rv_atan2(vx, vz);        /* 0 nose toward the viewer, 256 = the right side */
    int band, h;
    if (elev > 512) elev -= 1024;       /* -512..512 */
    if (elev > 213) return 0;           /* above 75 degrees: the top */
    if (elev < -213) return 36;
    band = elev > 128 ? 0 : elev > 43 ? 1 : elev >= -43 ? 2 : elev >= -128 ? 3 : 4;   /* 60, 30, 0, -30, -60 */
    h = ((head + 43) / 85) % 12;        /* 30 degree steps: 0 .. 11 */
    if (h <= 6)
        return 1 + band * 7 + h;
    return 37 + band * 5 + (h - 7);     /* 210, 240, 270, 300, 330 */
}

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
    if (left >= xb || left + (int)view->w <= xa)
        return;
    r0 = y0 - top;
    while (r0 < y1 - top) {
        unsigned char __far *scratch = gfx_scratch();
        unsigned long __far *tab = (unsigned long __far *)scratch;
        unsigned char __far *data, __far *dst;
        unsigned n, room;
        int r;
        r1 = y1 - top;
        n = r1 - r0 + 1;
        xms_get(s->blk, view->at + 4UL * r0, tab, 4UL * n);
        room = SCRATCH - 4 * n - 16;
        while (tab[r1 - r0] - tab[0] > room && r1 > r0 + 1)
            r1--;
        dst = vid_row(top + r0);
        data = scratch + 4 * n;
        xms_get(s->blk, view->at + 4UL * (view->h + 1) + tab[0], data, tab[r1 - r0] - tab[0]);
        for (r = r0; r < r1; r++, dst += SCR_W) {
            int ry = top + r, ca = xa, cb = xb;
            if (ry % 100 == 0 && r > r0)
                dst = vid_row(ry);      /* the next band of the back buffer */
            if (ext) {
                const short __far *e = ext + 2 * (ry - ext_y0);
                if (ry < ext_y0 || ry >= ext_y0 + ext_rows)
                    continue;
                if (e[0] > ca) ca = e[0];
                if (e[1] < cb) cb = e[1];
            }
            if (ca >= cb)
                continue;
            rv_blit_row(dst, data + (unsigned)(tab[r - r0] - tab[0]), left, ca, cb);
        }
        r0 = r1;
    }
}
