/* rvsprites.c: a ship's sprite set, pre-rendered for the flight (docs/engine.md, "Flight").
 *
 *     rvsprites IN.RV OUT.RVS [first_size roll_from roll_sizes]
 *                                        (gcc, from build.sh; the same C builds SETUP.EXE for DOS)
 *     first_size: the first size step kept (0: full size); roll_from, roll_sizes: the
 *     size steps that get rolls (fighters 0 4; a capital ship, big and slow, 3 2)
 *
 * The "compressed" form of a ship is SHIPnn.RV (tools/pack.py): its 37 or 62
 * views at full size, in WC's sprite code.  This is the decompressor: it
 * draws every view at every size step of the ladder, and at the five (or
 * eight) roll steps the flight draws from, with lightfield.c's own kernels,
 * and writes them in the engine's run-length row format so that RV.EXE
 * copies a block from the file into XMS and draws by copying rows.  The
 * page's JavaScript makes the same bytes from the same .RV at load time.
 *
 * Sizes: 13 steps, each 0.84 of the last, 256/256 .. 32/256.  Rolls: sixteen
 * steps of 22.5 degrees, of which a symmetric ship stores the arc 0..90 (five)
 * for every view: an upside-down copy turns a sprite by 180 - theta and
 * mirrors the heading, a mirrored copy turns it by -theta and mirrors the
 * heading, both together turn it by 180, so the flight draws the other
 * eleven from those five (see stored()).  An asymmetric ship stores the
 * eight 45-degree steps.  Rolls are stored for the ROLL_SIZES largest
 * sizes only.
 *
 * File (little-endian):
 *   "RVSP"  u8 type, u8 nviews, u8 nsizes, u8 nrolls, u8 roll_sizes, u8 asym, u16 0
 *   u16 scale256[16]                       the size steps (0 past nsizes)
 *   u32 nblocks; block[nblocks]:           u8 size, u8 roll, u16 nviews, u32 at, u32 length,
 *                                          then nviews x {u16 w, u16 h, i16 ox, i16 oy, u32 at}
 *                                          (at: from the block's start; w == 0: not stored)
 *   the blocks: each view's row table (h + 1 u32, from the rows' start) then its rows
 *   (skip, count, count pixels ... 0, 0), 2-byte aligned
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lightfield.h"

#define NSIZES 13
#define NROLLS 16
#define ROLL_SIZES 4
#define MAXV 62
static const int SCALES[NSIZES] = { 256, 215, 181, 152, 128, 108, 91, 76, 64, 54, 45, 38, 32 };

typedef struct { int w, h, ox, oy; unsigned char *px; } Dense;

static unsigned char *g_file;
static long g_len;

static unsigned long rd32(const unsigned char *p) { return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static int rd16s(const unsigned char *p) { return (short)(p[0] | (p[1] << 8)); }

/* a WC sprite (scripts/wc2data.py's layout) to a dense picture, 0 = clear */
static int decode(const unsigned char *d, long len, Dense *out)
{
    int x2 = rd16s(d), x1 = rd16s(d + 2), y1 = rd16s(d + 4), y2 = rd16s(d + 6);
    long off = 8;
    out->w = x1 + x2 + 2;
    out->h = y1 + y2 + 2;
    out->ox = x1 + 1;
    out->oy = y1 + 1;
    out->px = calloc((size_t)out->w * out->h, 1);
    while (off + 6 <= len) {
        int key = rd16s(d + off), vx = rd16s(d + off + 2), vy = rd16s(d + off + 4), x, n = 0, cnt;
        unsigned char *line;
        off += 6;
        if (key == 0)
            break;
        x = vx + x1 + 1;
        line = out->px + (long)(vy + y1 + 1) * out->w;
        cnt = key / 2;
        if (key % 2 == 0) {
            memcpy(line + x, d + off, cnt);
            off += cnt;
        } else {
            while (n < cnt && off < len) {
                int k = d[off++];
                if (k % 2 == 0) {
                    memcpy(line + x + n, d + off, k / 2);
                    off += k / 2;
                } else {
                    memset(line + x + n, d[off++], k / 2);
                }
                n += k / 2;
            }
        }
    }
    return 1;
}

/* the view at a size and turn, as run-length rows with a row table; returns the bytes */
static unsigned char *render(const Dense *src, int scale, int roll, int *w, int *h, int *ox, int *oy, long *len)
{
    LfRot r;
    unsigned char *line, *out;
    uint32_t *tab;                      /* (the host's long is 64 bits: the file's entries are 32) */
    long n = 0, cap;
    int y, x;
    lf_rotate_setup(src->w, src->h, src->ox, src->oy, roll, scale, &r);
    unsigned char *img;
    int bx0 = r.ow, bx1 = 0, by0 = r.oh, by1 = 0;
    /* the turned picture, then its painted box: the margins would cost a row each when drawn */
    img = calloc((size_t)r.ow * r.oh, 1);
    for (y = 0; y < r.oh; y++) {
        int xs, xe;
        if (lf_rotate_span(&r, src->w, src->h, y, 0, r.ow, &xs, &xe))
            lf_rotate_row(src->px, src->w, src->h, &r, y, xs, xe, img + (long)y * r.ow);
        for (x = 0; x < r.ow; x++)
            if (img[(long)y * r.ow + x]) {
                if (x < bx0) bx0 = x;
                if (x + 1 > bx1) bx1 = x + 1;
                if (y < by0) by0 = y;
                if (y + 1 > by1) by1 = y + 1;
            }
    }
    if (bx1 <= bx0) { bx0 = by0 = 0; bx1 = by1 = 1; }   /* (a blank view keeps one clear pixel) */
    *w = bx1 - bx0;
    *h = by1 - by0;
    *ox = r.ocx - bx0;
    *oy = r.ocy - by0;
    line = malloc(*w + 2);
    cap = (long)*w * *h * 2 + 4L * (*h + 1) + 64;
    out = malloc(cap);
    tab = (uint32_t *)out;
    n = 4L * (*h + 1);
    for (y = 0; y < *h; y++) {
        memcpy(line, img + (long)(by0 + y) * r.ow + bx0, *w);
        tab[y] = (uint32_t)(n - 4L * (*h + 1));
        x = 0;
        while (x < *w) {
            int skip = 0, run = 0;
            while (x < *w && !line[x]) { x++; skip++; }
            if (x >= *w)
                break;
            while (x + run < *w && line[x + run] && run < 255)
                run++;
            while (skip > 255) {
                out[n++] = 255; out[n++] = 0;
                skip -= 255;
            }
            out[n++] = (unsigned char)skip;
            out[n++] = (unsigned char)run;
            memcpy(out + n, line + x, run);
            n += run;
            x += run;
        }
        out[n++] = 0;
        out[n++] = 0;
    }
    tab[*h] = (uint32_t)(n - 4L * (*h + 1));
    if (n & 1)
        out[n++] = 0;
    free(line);
    free(img);
    *len = n;
    return out;
}

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned long v) { put16(f, (unsigned)(v & 0xFFFF)); put16(f, (unsigned)(v >> 16)); }

/* the mirror image of a view (the opposite heading of a symmetric ship) */
static void mirror(const Dense *a, Dense *b)
{
    int x, y;
    b->w = a->w; b->h = a->h; b->oy = a->oy;
    b->ox = a->w - 1 - a->ox;
    b->px = malloc((size_t)a->w * a->h);
    for (y = 0; y < a->h; y++)
        for (x = 0; x < a->w; x++)
            b->px[(long)y * a->w + x] = a->px[(long)y * a->w + (a->w - 1 - x)];
}

/* does view v store roll step r (0..15, 22.5 degrees each)?  A symmetric
 * ship stores the arc 0..90 for every view: the flight draws 90..180 as the
 * opposite heading's sprite upside down (the top and bottom views, their
 * own mirror, mirrored instead), 180..270 as the sprite turned about
 * (both flips), 270..360 as the opposite heading's mirrored (the top and
 * bottom views upside down).  An asymmetric ship cannot mirror: it stores
 * the eight 45-degree steps. */
static int stored(int v, int r, int asym)
{
    (void)v;
    if (asym)
        return r % 2 == 0;
    return r <= 4;
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned long b0, b1, total, nv, i;
    const unsigned char *rec;
    Dense views[MAXV];
    int type, nviews, asym, v, k, r, nblocks = 0, first_size = 0, roll_from = 0, roll_sizes = ROLL_SIZES;
    struct { int size, roll; long at, len; unsigned char *data; int w[MAXV], h[MAXV], ox[MAXV], oy[MAXV]; long vat[MAXV]; } *blocks;
    long at;
    if (argc < 3) {
        fprintf(stderr, "usage: rvsprites IN.RV OUT.RVS\n");
        return 2;
    }
    if (argc >= 6) {
        first_size = atoi(argv[3]);
        roll_from = atoi(argv[4]);
        roll_sizes = atoi(argv[5]);
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    g_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_file = malloc(g_len);
    fread(g_file, 1, g_len, f);
    fclose(f);
    b0 = rd32(g_file + 4) & 0xFFFFFF;
    b1 = rd32(g_file + 8) & 0xFFFFFF;
    rec = g_file + b1;
    if (memcmp(rec, "RVSH", 4) != 0) { fprintf(stderr, "%s: not a ship file\n", argv[1]); return 1; }
    type = rec[4];
    nviews = rec[5];
    asym = rec[6] & 1;
    total = rd32(g_file + b0);
    nv = (rd32(g_file + b0 + 4) & 0xFFFFFF);
    nv = (nv - 4) / 4;
    if (nviews > (int)nv) nviews = (int)nv;
    for (v = 0; v < nviews; v++) {
        unsigned long off = b0 + (rd32(g_file + b0 + 4 + 4 * v) & 0xFFFFFF);
        unsigned long end = v + 1 < (int)nv ? b0 + (rd32(g_file + b0 + 4 + 4 * (v + 1)) & 0xFFFFFF) : b0 + total;
        decode(g_file + off, (long)(end - off), &views[v]);
    }
    if (!asym && nviews == 37) {
        /* the opposite headings: 150 -> 210, 120 -> 240, 90 -> 270, 60 -> 300, 30 -> 330, per band */
        for (v = 1; v <= 35; v++) {
            int band = (v - 1) / 7, hh = (v - 1) % 7;
            if (hh >= 1 && hh <= 5)
                mirror(&views[v], &views[37 + band * 5 + (5 - hh)]);
        }
        nviews = MAXV;
    }
    blocks = calloc(NSIZES * NROLLS, sizeof *blocks);
    for (k = 0; k < NSIZES; k++)
        for (r = 0; r < NROLLS; r++) {
            long cap = 0, n = 0;
            unsigned char *data;
            if (k < first_size || (r && (k < roll_from || k >= roll_from + roll_sizes)))
                continue;
            for (v = 0; v < nviews; v++) {
                if (!stored(v, r, asym))
                    continue;
                cap += (long)views[v].w * views[v].h * 3 + 4000;
            }
            data = malloc(cap);
            for (v = 0; v < nviews; v++) {
                int w, h, ox, oy;
                long len;
                unsigned char *one;
                if (!stored(v, r, asym)) {
                    blocks[nblocks].w[v] = 0;
                    continue;
                }
                one = render(&views[v], SCALES[k], r * (1024 / NROLLS), &w, &h, &ox, &oy, &len);
                memcpy(data + n, one, len);
                blocks[nblocks].w[v] = w; blocks[nblocks].h[v] = h;
                blocks[nblocks].ox[v] = ox; blocks[nblocks].oy[v] = oy;
                blocks[nblocks].vat[v] = n;
                n += len;
                free(one);
            }
            if (n == 0) {               /* no view stores this roll: no block */
                free(data);
                continue;
            }
            blocks[nblocks].size = k;
            blocks[nblocks].roll = r;
            blocks[nblocks].data = data;
            blocks[nblocks].len = n;
            nblocks++;
        }
    f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    fwrite("RVSP", 1, 4, f);
    fputc(type, f); fputc(nviews, f); fputc(NSIZES, f); fputc(NROLLS, f);
    fputc(roll_sizes, f); fputc(asym, f); put16(f, 0);
    for (k = 0; k < 16; k++)
        put16(f, k < NSIZES ? SCALES[k] : 0);
    put32(f, nblocks);
    at = 4 + 8 + 32 + 4 + (long)nblocks * (12 + 12L * nviews);
    for (i = 0; i < (unsigned long)nblocks; i++) {
        fputc(blocks[i].size, f); fputc(blocks[i].roll, f); put16(f, nviews);
        put32(f, at); put32(f, blocks[i].len);
        for (v = 0; v < nviews; v++) {
            put16(f, blocks[i].w[v]); put16(f, blocks[i].h[v]);
            put16(f, (unsigned)(short)blocks[i].ox[v]); put16(f, (unsigned)(short)blocks[i].oy[v]);
            put32(f, blocks[i].vat[v]);
        }
        blocks[i].at = at;
        at += blocks[i].len;
    }
    for (i = 0; i < (unsigned long)nblocks; i++)
        fwrite(blocks[i].data, 1, blocks[i].len, f);
    fclose(f);
    printf("%s: type %02X, %d views, %d blocks, %ld KB\n", argv[2], type, nviews, nblocks, at / 1024);
    return 0;
}
