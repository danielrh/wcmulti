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
 * Sizes: 13 steps, each 0.84 of the last, 256/256 .. 32/256.  Rolls: 32
 * steps of 11.25 degrees (16 of 22.5 until 2026-10-22: the user spent the
 * new XMS on finer turning in the mid field), of which a symmetric ship
 * stores the arc 0..90 (nine) for every view: an upside-down copy turns a
 * sprite by 180 - theta and mirrors the heading, a mirrored copy turns it
 * by -theta and mirrors the heading, both together turn it by 180, so the
 * flight draws the other 23 from those nine (see stored()).  An asymmetric
 * ship stores the eight 45-degree steps.  Rolls are stored for the
 * ROLL_SIZES largest sizes only.
 *
 * The views come in a lattice (lightfield.h): WC1's 5 bands x 7 headings
 * (37 views, 62 with an asymmetric ship's mirrored headings) or the denser
 * 11 x 13 (145 / 266).  Block 0, the full-size views the flight turns and
 * scales exactly, keeps the lattice as the .RV has it (a symmetric ship's
 * mirrored headings are not stored: the flight mirrors them as it expands
 * a view).  The other blocks, the copies for the smaller sizes, use the
 * 30-degree sub-lattice (every other band and heading of a 15-degree one)
 * with the mirrored headings made here, 62 views, so the sets stay small.
 *
 * File (little-endian):
 *   "RVSP"  u8 type, u8 nviews0 (low byte), u8 nsizes, u8 nrolls, u8 roll_sizes, u8 asym, u8 nb0, u8 nh0
 *   u16 scale256[16]                       the size steps (0 past nsizes)
 *   u8 nb1, u8 nh1, u16 nviews0            the sets' lattice (5, 7), block 0's view count whole
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

#define NSIZES 13                      /* the ladder of a set: its sizes k = 0 .. nsizes - 1 are the steps k + step0 of SCALES */
#define NSCALES 21
#define NROLLS 32
#define ROLL_SIZES 4
#define MAXV 1750                      /* 29 x 31 at 6 degrees with the mirrored headings (1,742) */
static const int SCALES[NSCALES] = { 256, 215, 181, 152, 128, 108, 91, 76, 64, 54, 45, 38, 32, 27, 22, 19, 16, 13, 11, 9, 8 };

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
/* the picture turned by roll and scaled, by the flight's own kernel (every output pixel samples one source pixel) */
static unsigned char *turned(const Dense *src, int scale, int roll, LfRot *r)
{
    unsigned char *img;
    int y;
    lf_rotate_setup(src->w, src->h, src->ox, src->oy, roll, scale, r);
    img = calloc((size_t)r->ow * r->oh, 1);
    for (y = 0; y < r->oh; y++) {
        int16_t xs, xe;
        if (lf_rotate_span(r, src->w, src->h, y, 0, r->ow, &xs, &xe))
            lf_rotate_row(src->px, src->w, src->h, r, y, xs, xe, img + (long)y * r->ow);
    }
    return img;
}

static long floor16(long a) { return a >= 0 ? a >> 16 : -((-a + 65535) >> 16); }
static long floor8(long a) { return a >= 0 ? a >> 8 : -((-a + 255) >> 8); }

/* The smaller sizes: a picture shrunk by coverage, not by sampling, so a
 * wing a pixel wide survives as a line instead of dropping out (the user's
 * complaint: ships collapsing at a distance look like a bug, hand-drawn
 * sprites keep their lines).  Output pixel (x, y) owns the box of source
 * pixels whose centres fall in its footprint (the boxes tile the source);
 * it is drawn when enough of them are ship - the user's ladder: half from
 * half size up, a quarter below half, an eighth below a quarter, any at
 * all below an eighth - with the colour of the ship pixel nearest the
 * box's centre.  The footprint is the one lf_rotate_setup gives at roll
 * 0, so origins agree with the flight's. */
static unsigned char *shrunk(const unsigned char *img, int tw, int th, int tcx, int tcy, int scale, LfRot *r)
{
    unsigned char *out;
    int x, y;
    lf_rotate_setup(tw, th, tcx, tcy, 0, scale, r);
    out = calloc((size_t)r->ow * r->oh, 1);
    for (y = 0; y < r->oh; y++) {
        long vc = r->v00 + (long)y * r->drv, v0 = vc - r->drv / 2, v1 = v0 + r->drv;
        long r0 = -floor16(32768L - v0), r1 = -floor16(32768L - v1);   /* rows with centres in [v0, v1) */
        for (x = 0; x < r->ow; x++) {
            long uc = r->u00 + (long)x * r->du, u0 = uc - r->du / 2, u1 = u0 + r->du;
            long c0 = -floor16(32768L - u0), c1 = -floor16(32768L - u1);
            long total = (c1 - c0) * (r1 - r0), count = 0, need, best = -1, c, rr;
            unsigned char pick = 0;
            if (total <= 0)
                continue;
            need = scale >= 128 ? (total + 1) / 2 : scale >= 64 ? (total + 3) / 4 : scale >= 32 ? (total + 7) / 8 : 1;
            for (rr = r0 < 0 ? 0 : r0; rr < r1 && rr < th; rr++)
                for (c = c0 < 0 ? 0 : c0; c < c1 && c < tw; c++) {
                    unsigned char p = img[rr * tw + c];
                    long dx, dy, d;
                    if (!p)
                        continue;
                    count++;
                    dx = floor8((c << 16) + 32768L - uc);
                    dy = floor8((rr << 16) + 32768L - vc);
                    d = dx * dx + dy * dy;
                    if (best < 0 || d < best) {
                        best = d;
                        pick = p;
                    }
                }
            if (count >= need)
                out[(long)y * r->ow + x] = pick;
        }
    }
    return out;
}

static unsigned char *render(const Dense *src, int scale, int roll, int *w, int *h, int *ox, int *oy, long *len)
{
    LfRot r;
    unsigned char *line, *out;
    uint32_t *tab;                      /* (the host's long is 64 bits: the file's entries are 32) */
    long n = 0, cap;
    int y, x;
    unsigned char *img;
    int bx0, bx1 = 0, by0, by1 = 0;
    /* the turned picture (shrunk by coverage below full size), then its painted box: the
     * margins would cost a row each when drawn */
    if (scale < 256) {
        LfRot full;
        unsigned char *big = turned(src, 256, roll, &full);
        img = shrunk(big, full.ow, full.oh, full.ocx, full.ocy, scale, &r);
        free(big);
    } else
        img = turned(src, scale, roll, &r);
    bx0 = r.ow;
    by0 = r.oh;
    for (y = 0; y < r.oh; y++)
        for (x = 0; x < r.ow; x++)
            if (img[(long)y * r.ow + x]) {
                if (x < bx0) bx0 = x;
                if (x + 1 > bx1) bx1 = x + 1;
                if (y < by0) by0 = y;
                if (y + 1 > by1) by1 = y + 1;
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
        return r % (NROLLS / 8) == 0;
    return r <= NROLLS / 4;
}

/* the 30-degree sub-lattice's view j (of 62: 37 and the mirrored headings)
 * as a view of the .RV's lattice (nb, nh): the lattice's own index, or -1
 * when it must be made as the mirror image of the view returned in *mirror_of */
static int sub_view(int j, int nb, int nh, int asym, int *mirror_of)
{
    int step = (nb + 1) / 6;           /* 1 for (5, 7), 2 for (11, 13) */
    int b, h, k;
    *mirror_of = -1;
    if (j == 0) return 0;
    if (j == 36) return 1 + nb * nh;
    if (j <= 35) {
        b = (j - 1) / 7;
        h = (j - 1) % 7;
        return 1 + (b * step + step - 1) * nh + h * step;
    }
    b = (j - 37) / 5;                  /* the mirrored headings 210 .. 330: k = 1 .. 5 */
    k = (j - 37) % 5 + 1;
    if (asym)
        return 2 + nb * nh + (b * step + step - 1) * (nh - 2) + (k * step - 1);
    *mirror_of = 1 + (b * step + step - 1) * nh + (6 - k) * step;   /* heading 360 - 30k = the mirror of 30 (6 - k) */
    return -1;
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned long b0, b1, total, nv, i;
    const unsigned char *rec;
    Dense views[MAXV];
    int type, nviews, asym, v, k, r, nblocks = 0, first_size = 0, roll_from = 0, roll_sizes = ROLL_SIZES, nb, nh;
    Dense sub[MAXV];
    int nview0, nsub, sub_step = 3, factor = 1, step0 = 0;
    struct { int size, roll, nv; long at, len; unsigned char *data; int w[MAXV], h[MAXV], ox[MAXV], oy[MAXV]; long vat[MAXV]; } *blocks;
    long at;
    if (argc < 3) {
        fprintf(stderr, "usage: rvsprites IN.RV OUT.RVS [first_size roll_from roll_sizes [lattice factor]]\n"
                        "  lattice: 3 = the sets on the 30-degree sub-lattice (62 views; the default), 1 = on every view\n"
                        "  factor: 1 = the sets at full size; 2 or 4 = half or quarter size, drawn that many times\n"
                        "          bigger as 2 x 2 or 4 x 4 blocks (their ladder starts 4 or 8 steps down)\n");
        return 2;
    }
    if (argc >= 6) {
        first_size = atoi(argv[3]);
        roll_from = atoi(argv[4]);
        roll_sizes = atoi(argv[5]);
    }
    if (argc >= 8) {
        sub_step = atoi(argv[6]);
        factor = atoi(argv[7]);
        step0 = factor == 4 ? 8 : factor == 2 ? 4 : 0;
        if ((sub_step != 1 && sub_step != 3) || (factor != 1 && factor != 2 && factor != 4)) {
            fprintf(stderr, "lattice 1 or 3, factor 1, 2 or 4\n");
            return 2;
        }
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
    nviews = rec[5] | (rec[6] << 8);   /* a word: an asymmetric 15-degree ship has 266 */
    asym = rec[7] & 1;
    nb = rec[11] ? rec[11] : 5;        /* the lattice (0, 0 in older files: WC1's) */
    nh = rec[12] ? rec[12] : 7;
    total = rd32(g_file + b0);
    nv = (rd32(g_file + b0 + 4) & 0xFFFFFF);
    nv = (nv - 4) / 4;
    if (nviews > (int)nv) nviews = (int)nv;
    for (v = 0; v < nviews; v++) {
        unsigned long off = b0 + (rd32(g_file + b0 + 4 + 4 * v) & 0xFFFFFF);
        unsigned long end = v + 1 < (int)nv ? b0 + (rd32(g_file + b0 + 4 + 4 * (v + 1)) & 0xFFFFFF) : b0 + total;
        decode(g_file + off, (long)(end - off), &views[v]);
    }
    nview0 = nviews;                   /* block 0: the lattice's views as stored */
    if (sub_step == 3) {
        /* the sets' 62 views: the 30-degree sub-lattice, its mirrored headings made */
        nsub = 62;
        for (v = 0; v < 62; v++) {
            int m, src = sub_view(v, nb, nh, asym, &m);
            if (src >= 0)
                sub[v] = views[src];
            else
                mirror(&views[m], &sub[v]);
        }
    } else if (roll_sizes == 0) {
        /* a ladder only, on every view of the lattice as stored (the engine mirrors a symmetric ship's
         * other headings as it does the full-size views', and turns every sprite itself) */
        nsub = nviews;
        for (v = 0; v < nsub; v++)
            sub[v] = views[v];
    } else {
        /* the sets on every view of the lattice: the stored ones, then a symmetric ship's mirrored
         * headings made (the engine's indices for them: lf_pair_of's) */
        nsub = asym ? nviews : 2 + nb * nh + nb * (nh - 2);
        for (v = 0; v < nsub; v++)
            if (v < nviews && views[v].w)
                sub[v] = views[v];
            else
                mirror(&views[lf_pair_of(v, nb, nh)], &sub[v]);
    }
    blocks = calloc(NSIZES * NROLLS, sizeof *blocks);
    for (k = 0; k < NSIZES; k++)
        for (r = 0; r < NROLLS; r++) {
            long cap = 0, n = 0;
            unsigned char *data;
            const Dense *src = (k == 0 && r == 0) ? views : sub;
            int nv = (k == 0 && r == 0) ? nview0 : nsub;
            if (k < first_size || (r && (k < roll_from || k >= roll_from + roll_sizes)))
                continue;
            for (v = 0; v < nv; v++) {
                if (!stored(v, r, asym))
                    continue;
                cap += (long)src[v].w * src[v].h * 3 + 4000;
            }
            data = malloc(cap);
            blocks[nblocks].nv = nv;
            for (v = 0; v < nv; v++) {
                int w, h, ox, oy;
                long len;
                unsigned char *one;
                if (!stored(v, r, asym)) {
                    blocks[nblocks].w[v] = 0;
                    continue;
                }
                one = render(&src[v], SCALES[k + step0], r * (1024 / NROLLS), &w, &h, &ox, &oy, &len);
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
    fputc(type, f); fputc(nview0 & 255, f); fputc(NSIZES, f); fputc(NROLLS, f);
    fputc(roll_sizes, f); fputc(asym | (factor == 2 ? 2 : factor == 4 ? 4 : 0), f); fputc(nb, f); fputc(nh, f);
    for (k = 0; k < 16; k++)            /* the sizes relative to the ship at full size: a half set's start at 128 */
        put16(f, k < NSIZES ? SCALES[k + step0] : 0);
    if (sub_step == 3) { fputc(5, f); fputc(7, f); } else { fputc(nb, f); fputc(nh, f); }   /* the sets' lattice */
    put16(f, nview0);                   /* the view count whole (266 overflows the byte) */
    put32(f, nblocks);
    at = 4 + 8 + 32 + 4 + 4;
    for (i = 0; i < (unsigned long)nblocks; i++)
        at += 12 + 12L * blocks[i].nv;
    for (i = 0; i < (unsigned long)nblocks; i++) {
        fputc(blocks[i].size, f); fputc(blocks[i].roll, f); put16(f, blocks[i].nv);
        put32(f, at); put32(f, blocks[i].len);
        for (v = 0; v < blocks[i].nv; v++) {
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
    printf("%s: type %02X, %d views (%d x %d), %d blocks, %ld KB\n", argv[2], type, nview0, nb, nh, nblocks, at / 1024);
    return 0;
}
