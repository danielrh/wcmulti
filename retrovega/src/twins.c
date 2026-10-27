/* twins.c: the assembly's C twins (twins.h).
 *
 * c_x is rv_x in C: lowlevel.asm's, lasers.asm's and rocks.asm's, with the same parameters and the same pixels and
 * numbers out, to the bit.  The host build draws with them (host/asm_twins.c: each rv_x calls its c_x); RVT.EXE
 * /twins checks every one against its asm on seeded random inputs and logs a sum of the twin's outputs, and
 * tests/host/twins_test.sh computes the same sums on the host and compares them with the DOS run's: a twin gives
 * the same with int 16 bits (Watcom) as with 32 (gcc, Emscripten).
 *
 * Where lasers.c (or lightfield.c) has the reference the asm was written against, the twin is an adapter to it.
 * The asm's draws take rows the caller has cut to the extents' and an extents table always (the whole width's
 * when the cockpit has none); the references an optional table and how many rows it has.  The rest follow the asm
 * step by step, in sized types where its 16-bit registers wrap: a turned row's source index (v * w + u in a word),
 * a star's column (the quotient's low word plus the centre). */
#include <string.h>
#ifdef __WATCOMC__
#include <malloc.h>
#include <dos.h>
#endif
#include "rv.h"
#include "rvtypes.h"
#include "twins.h"

/* a long of two words, as the asm returns one in dx:ax */
static long pair(int lo, int hi)
{
    return (long)(((uint32_t)(uint16_t)hi << 16) | (uint16_t)lo);
}

/* --- lasers.asm's: adapters to lasers.c --- */

int c_bolts_place(const LzBolt LZ_FAR *b, int n, const LzCam LZ_FAR *cam, LzLine LZ_FAR *lines, int maxlines,
                  LzFat LZ_FAR *fats, int maxfats, int LZ_FAR *nfats)
{
    int nl, nf;
    lz_place(b, n, cam, lines, &nl, maxlines, fats, &nf, maxfats);
    *nfats = nf;
    return nl;
}

/* (the draws: rows y0 .. y1-1 are in the table, so it has rows up to y1) */
void c_lines_draw(const LzLine LZ_FAR *l, int n, int colour, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0)
{
    lz_lines_draw(l, n, (unsigned char)colour, band, band_y0, y0, y1, ext, ext_y0, y1 - ext_y0);
}

void c_bolts_move(LzBolt LZ_FAR *b, int n, long k)
{
    lz_move(b, n, k);
}

/* (roots: lz_sqrt16, which every caller passes and lasers.c reads) */
void c_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                 const short LZ_FAR *ext, int ext_y0, const unsigned char LZ_FAR *roots)
{
    RV_ASSERT(roots == lz_sqrt16);
    lz_pass_draw(p, band, band_y0, y0, y1, ext, ext_y0, y1 - ext_y0);
}

int c_fat_setup(const LzFat LZ_FAR *f, const LzStyle LZ_FAR *st, LzPass LZ_FAR *pass, LzLine LZ_FAR *core,
                int LZ_FAR *core_line)
{
    LzStyle s = *st;
    int cl, n = lz_fat_setup(f, &s, pass, core, &cl);
    *core_line = cl;
    return n;
}

void c_capsule_setup(const LzFat LZ_FAR *f, int colour, LzPass LZ_FAR *pass)
{
    lz_capsule_setup(f, colour, pass);
}

void c_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame LZ_FAR *f, LzFat LZ_FAR *fp)
{
    lz_flame_setup(ax, ay, tx, ty, r16, seed, f, fp);
}

long c_balls_place(const LzBall LZ_FAR *b, int n, const LzCam LZ_FAR *cam, const LzBallKind LZ_FAR *k, int dmax,
                   unsigned anim, LzBallOut LZ_FAR *out, int maxout, const unsigned char LZ_FAR *small)
{
    return lz_balls_place(b, n, cam, k, dmax, anim, out, maxout, small);
}

void c_balls_draw(const LzBallOut LZ_FAR *o, int n, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0)
{
    lz_balls_draw(o, n, band, band_y0, y0, y1, ext, ext_y0, y1 - ext_y0);
}

void c_flame_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0, const unsigned char LZ_FAR *roots, const LzFlame LZ_FAR *f,
                  const LzFlameLook LZ_FAR *look)
{
    RV_ASSERT(roots == lz_sqrt16);
    lz_flame_draw(p, f, look, band, band_y0, y0, y1, ext, ext_y0, y1 - ext_y0);
}

/* --- rocks.asm's --- */

/* The reference of rv_rocks_place, the asm written against it (rocks.c's self-test checks them) */
typedef long long rk_wide;

int lz_rocks_place(LzRock LZ_FAR *r, int n, const LzCam LZ_FAR *c, LzRockCam LZ_FAR *rc, LzRockOut LZ_FAR *out,
                   int maxout, short LZ_FAR *reap, short LZ_FAR *hitl)
{
    int i, no = 0;
    rc->nreap = rc->nclose = 0;
    for (i = 0; i < n && no < maxout; i++, r++) {
        long rx, ry, rz, X, Y, Z, xk, yk, zk, sx, sy, d;
        unsigned long d2, d16;
        r->x += r->vx;                  /* (it flies a frame) */
        r->y += r->vy;
        r->z += r->vz;
        rx = r->x - c->bx;
        ry = r->y - c->by;
        rz = r->z - c->bz;
        if ((unsigned long)rx >= 0x2000000UL || (unsigned long)ry >= 0x2000000UL || (unsigned long)rz >= 0x2000000UL) {
            if (rc->nreap < rc->maxreap)
                reap[rc->nreap++] = (short)i;
            continue;
        }
        rx = (rx >> 4) - 0x100000L;
        ry = (ry >> 4) - 0x100000L;
        rz = (rz >> 4) - 0x100000L;
        Z = rx * c->f[0] + ry * c->f[1] + rz * c->f[2];
        X = rx * c->r[0] + ry * c->r[1] + rz * c->r[2];
        Y = rx * c->u[0] + ry * c->u[1] + rz * c->u[2];
        xk = X >> 12;
        yk = Y >> 12;
        zk = Z >> 12;
        if (xk > 32767 || xk < -32767 || yk > 32767 || yk < -32767 || zk > 32767 || zk < -32767) {
            if (rc->nreap < rc->maxreap)
                reap[rc->nreap++] = (short)i;
            continue;
        }
        d2 = (unsigned long)(xk * xk) + (unsigned long)(yk * yk) + (unsigned long)(zk * zk);
        if (d2 > rc->far2) {
            if (rc->nreap < rc->maxreap)
                reap[rc->nreap++] = (short)i;
            continue;
        }
        {
            long reach = rc->reach + ((long)r->r8 << 15);   /* (+ its radius: 8 km x 4,096 a step) */
            if (X < reach && X > -reach && Y < reach && Y > -reach && Z < reach && Z > -reach
                && rc->nclose < rc->maxclose)
                hitl[rc->nclose++] = (short)i;
        }
        if (Z < c->zmin)
            continue;
        d16 = (unsigned long)((rk_wide)r->r8 * rc->dk / (unsigned long)Z);
        if (d2 > rc->fade2)
            d16 = (unsigned long)((rk_wide)d16 * (rc->far2 - d2) / (rc->far2 - rc->fade2));
        d = (long)((d16 + 8) >> 4);
        if (d == 0)
            continue;
        if (d > rc->dmax)
            d = rc->dmax;
        sx = (c->cx16 + (long)((rk_wide)X * c->f16 / Z)) >> 4;
        sy = (c->cy16 - (long)((rk_wide)Y * c->f16 / Z)) >> 4;
        if (sx - (d - 1) / 2 + d <= c->xmin16 >> 4 || sx - (d - 1) / 2 >= c->xmax16 >> 4
            || sy - (d - 1) / 2 + d <= c->ymin16 >> 4 || sy - (d - 1) / 2 >= c->ymax16 >> 4)
            continue;
        out[no].x = (short)sx;
        out[no].y = (short)sy;
        out[no].d = (unsigned short)d;
        out[no].kind = r->kind;
        out[no].frame = (unsigned char)((r->phase + (((long)rc->anim * r->spin) >> 2)) & rc->mask);
        out[no].z = Z;
        no++;
    }
    return no;
}

int c_rocks_place(LzRock LZ_FAR *r, int n, const LzCam LZ_FAR *c, LzRockCam LZ_FAR *rc, LzRockOut LZ_FAR *out,
                  int maxout, short LZ_FAR *reap, short LZ_FAR *hitl)
{
    return lz_rocks_place(r, n, c, rc, out, maxout, reap, hitl);
}

/* the first row of a rock's to draw, its run table's place and its pixels' (the rows above y0 counted past) */
static int16_t rock_first(const unsigned char LZ_FAR **h, uint32_t *off, int rows, int y, int y0)
{
    uint16_t k;
    if ((int16_t)y0 <= (int16_t)y)
        return (int16_t)y;
    if ((int16_t)(y0 - y) >= (int16_t)rows)
        return 0x7FFF;                  /* (none: past any end) */
    for (k = (uint16_t)(y0 - y); k; k--) {
        unsigned nr = *(*h)++;
        for (; nr; nr--, *h += 2)
            *off += (*h)[1];
    }
    return (int16_t)y0;
}

/* each row's runs clipped to the row's extent and moved from the handle's memory into the band; a run cut to an
 * odd length loses a pixel at its cut side (the driver moves words), at its right when it was not cut */
void c_rocks_xms(const unsigned char LZ_FAR *hdr, int rows, int x, int y, unsigned handle, unsigned long off,
                 unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext, int ext_y0)
{
    const unsigned char LZ_FAR *h = hdr;
    uint32_t at = off;
    int16_t sy = rock_first(&h, &at, rows, y, y0), end = (int16_t)(y + rows);
    if (end > (int16_t)y1)
        end = (int16_t)y1;
    for (; sy < end; sy++) {
        unsigned nr = *h++;
        const short LZ_FAR *e = ext + 2 * (sy - (int16_t)ext_y0);
        for (; nr; nr--, h += 2) {
            int16_t xa = (int16_t)(x + h[0] - 1), ca = xa, cb = (int16_t)(xa + h[1]), n;
            if (ca < e[0])
                ca = e[0];
            if (cb > e[1])
                cb = e[1];
            n = (int16_t)(cb - ca);
            if (n > 0 && (n & 1)) {
                n--;
                if (ca != xa)
                    ca++;
            }
            if (n > 0)
                xms_get_at(handle, at + (uint16_t)(ca - xa), band + (int32_t)(sy - (int16_t)band_y0) * 640 + ca,
                           (unsigned long)n);
            at += h[1];
        }
    }
}

/* the same from pix, each pixel k x k: a run widened once (into the line), then copied to each of its k rows */
void c_rocks_copy(const unsigned char LZ_FAR *hdr, int rows, int x, int y, int k, const unsigned char LZ_FAR *pix,
                  unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext, int ext_y0)
{
    static unsigned char LZ_FAR line[512];
    const unsigned char LZ_FAR *h = hdr;
    uint16_t at = 0;
    int16_t sy = (int16_t)y, left = (int16_t)rows;
    for (; left > 0; left--, sy = (int16_t)(sy + k)) {
        unsigned nr;
        if (sy >= (int16_t)y1)
            return;
        nr = *h++;
        if ((int16_t)(sy + k) <= (int16_t)y0) {      /* its k rows all above y0: its runs counted past */
            for (; nr; nr--, h += 2)
                at = (uint16_t)(at + h[1]);
            continue;
        }
        for (; nr; nr--, h += 2) {
            unsigned len = h[1];
            int16_t xa, w, i, ry;
            const unsigned char LZ_FAR *src;
            if (len) {
                xa = (int16_t)((h[0] - 1) * k + x);
                w = (int16_t)(len * k);
                src = pix + at;
                if (k != 1) {
                    for (i = 0; i < w; i++)
                        line[i] = src[i / k];
                    src = line;
                }
                for (i = 0, ry = sy; i < k; i++, ry++) {
                    const short LZ_FAR *e;
                    int16_t ca = xa, cb = (int16_t)(xa + w), j;
                    unsigned char LZ_FAR *dst;
                    if (ry < (int16_t)y0)
                        continue;
                    if (ry >= (int16_t)y1)
                        break;
                    e = ext + 2 * (ry - (int16_t)ext_y0);
                    if (ca < e[0])
                        ca = e[0];
                    if (cb > e[1])
                        cb = e[1];
                    dst = band + (int32_t)(ry - (int16_t)band_y0) * 640;
                    for (j = ca; j < cb; j++)
                        dst[j] = src[j - xa];
                }
            }
            at = (uint16_t)(at + len);
        }
    }
}

/* --- lowlevel.asm's --- */

/* a run-length row (skip, count, count pixels ... 0, 0) from column `left`, drawn inside [xa, xb) */
void c_blit_row(unsigned char LZ_FAR *row, const unsigned char LZ_FAR *spans, int left, int xa, int xb)
{
    int16_t c = (int16_t)left;
    for (;;) {
        unsigned skip = *spans++, n = *spans++;
        int16_t a, b, i;
        if (!n && !skip)
            return;
        c = (int16_t)(c + skip);
        if (!n)
            continue;
        a = c > (int16_t)xa ? c : (int16_t)xa;
        b = (int16_t)(c + n) < (int16_t)xb ? (int16_t)(c + n) : (int16_t)xb;
        for (i = a; i < b; i++)
            row[i] = spans[i - c];
        spans += n;
        c = (int16_t)(c + n);
    }
}

/* the same mirrored: the span at sprite columns [c, c+n) lands at [right-c-n, right-c), its pixels the other way */
void c_blit_row_rev(unsigned char LZ_FAR *row, const unsigned char LZ_FAR *spans, int right, int xa, int xb)
{
    int16_t c = 0;
    for (;;) {
        unsigned skip = *spans++, n = *spans++;
        int16_t e0, s0, a, b, i;
        if (!n && !skip)
            return;
        c = (int16_t)(c + skip);
        if (!n)
            continue;
        e0 = (int16_t)(right - c);
        s0 = (int16_t)(e0 - n);
        a = s0 > (int16_t)xa ? s0 : (int16_t)xa;
        b = e0 < (int16_t)xb ? e0 : (int16_t)xb;
        for (i = a; i < b; i++)
            row[i] = spans[e0 - 1 - i];
        spans += n;
        c = (int16_t)(c + n);
    }
}

void c_scale_row(const unsigned char LZ_FAR *line, const unsigned short LZ_FAR *map, int xa, int xb,
                 unsigned char LZ_FAR *out)
{
    lf_scale_row(line, map, (int16_t)xa, (int16_t)xb, out);
}

/* (lf_rotate_row's bounds test never fails on the span lf_rotate_span gives, the asm's caller's) */
void c_rotate_row(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                  unsigned char LZ_FAR *out)
{
    lf_rotate_row(dense, (int16_t)w, (int16_t)h, r, (int16_t)y, (int16_t)xs, (int16_t)xe, out);
}

/* the turned kernels: u, v (16.16) at column x of output row y, wrapping in 32 bits as the asm's imul and add do;
 * the source pixel under them at v * w + u in a word (imul dx: the low word is right past 32K too) */
static uint32_t at_u(const LfRot *r, int y, int x)
{
    return (uint32_t)r->u00 + (uint32_t)(int32_t)(int16_t)y * (uint32_t)r->dru
        + (uint32_t)(int32_t)(int16_t)x * (uint32_t)r->du;
}

static uint32_t at_v(const LfRot *r, int y, int x)
{
    return (uint32_t)r->v00 + (uint32_t)(int32_t)(int16_t)y * (uint32_t)r->drv
        + (uint32_t)(int32_t)(int16_t)x * (uint32_t)r->dv;
}

#define SRC(d, w, u, v) ((d)[(uint16_t)((unsigned)(uint16_t)((v) >> 16) * (unsigned)(uint16_t)(w) \
                                        + (uint16_t)((u) >> 16))])

void c_rotate_row2(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                   unsigned char LZ_FAR *out)
{
    uint32_t u = at_u(r, y, xs), v = at_v(r, y, xs), du = (uint32_t)r->du * 2, dv = (uint32_t)r->dv * 2;
    uint16_t n;
    unsigned char LZ_FAR *o = out + (int16_t)xs;
    (void)h;
    if ((int16_t)xe <= (int16_t)xs)
        return;
    for (n = (uint16_t)(xe - xs) >> 1; n; n--, u += du, v += dv, o += 2) {
        unsigned char p = SRC(dense, w, u, v);
        if (p)
            o[0] = o[1] = p;
    }
    if ((uint16_t)(xe - xs) & 1) {      /* (an odd span: xe - 1, once: never past the span) */
        unsigned char p = SRC(dense, w, u, v);
        if (p)
            o[0] = p;
    }
}

/* the first column of [xs, xe) whose source pixel is not clear (xe when none), and from xe - 1 back the last */
static int16_t first_of(const unsigned char LZ_FAR *dense, int w, const LfRot *r, int y, int xs, int xe)
{
    uint32_t u = at_u(r, y, xs), v = at_v(r, y, xs);
    int16_t x;
    for (x = (int16_t)xs; x < (int16_t)xe; x++, u += (uint32_t)r->du, v += (uint32_t)r->dv)
        if (SRC(dense, w, u, v))
            return x;
    return (int16_t)xe;
}

static int16_t last_of(const unsigned char LZ_FAR *dense, int w, const LfRot *r, int y, int xe)
{
    uint32_t u = at_u(r, y, xe - 1), v = at_v(r, y, xe - 1);
    int16_t x = (int16_t)(xe - 1);
    while (!SRC(dense, w, u, v)) {      /* (it stops at the first column at the latest: that one is not clear) */
        u -= (uint32_t)r->du;
        v -= (uint32_t)r->dv;
        x--;
    }
    return x;
}

/* columns x .. x+n-1 sampled into out */
static void sampled(const unsigned char LZ_FAR *dense, int w, const LfRot *r, int y, int x, int n,
                    unsigned char LZ_FAR *out)
{
    uint32_t u = at_u(r, y, x), v = at_v(r, y, x);
    for (; (int16_t)n > 0; n--, x++, u += (uint32_t)r->du, v += (uint32_t)r->dv) {
        unsigned char p = SRC(dense, w, u, v);
        if (p)
            out[(int16_t)x] = p;
    }
}

long c_rotate_edges(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe)
{
    int16_t first;
    (void)h;
    if ((int16_t)xe <= (int16_t)xs || (first = first_of(dense, w, r, y, xs, xe)) == (int16_t)xe)
        return pair(xe, xe);
    return pair(first, last_of(dense, w, r, y, xe) + 1);
}

long c_turn_drawn(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                  unsigned char LZ_FAR *out)
{
    int16_t first, end;
    (void)h;
    if ((int16_t)xe <= (int16_t)xs || (first = first_of(dense, w, r, y, xs, xe)) == (int16_t)xe)
        return pair(xe, xe);
    end = (int16_t)(last_of(dense, w, r, y, xe) + 1);
    sampled(dense, w, r, y, first, end - first, out);
    return pair(first, end);
}

/* a dropped row: its edges probed from the row above's (pl, pr: outward while the next pixel is the ship's, else
 * inward to the first that is), what the row above covers too copied down from `above`, the rest sampled */
long c_turn_dropped(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe, int pl,
                    int pr, unsigned char LZ_FAR *out, const unsigned char LZ_FAR *above)
{
    int16_t x, first, end, a, b, last = (int16_t)(xe - 1);
    (void)h;
#define PROBE(c) SRC(dense, w, at_u(r, y, c), at_v(r, y, c))
    if ((int16_t)xe <= (int16_t)xs)
        return pair(xe, xe);
    x = (int16_t)pl;
    if (x < (int16_t)xs)
        x = (int16_t)xs;
    if (x > last)
        x = last;
    if (PROBE(x)) {
        while (x > (int16_t)xs && PROBE(x - 1))
            x--;
    } else {
        do {
            if (++x >= (int16_t)xe)
                return pair(xe, xe);
        } while (!PROBE(x));
    }
    first = x;
    x = (int16_t)(pr - 1);
    if (x < first)
        x = first;
    if (x > last)
        x = last;
    if (PROBE(x)) {
        while (x < last && PROBE(x + 1))
            x++;
    } else {
        do
            x--;
        while (!PROBE(x));
    }
#undef PROBE
    end = (int16_t)(x + 1);
    a = first > (int16_t)pl ? first : (int16_t)pl;
    b = end < (int16_t)pr ? end : (int16_t)pr;
    if (a >= b) {
        sampled(dense, w, r, y, first, end - first, out);
    } else {
        for (x = a; x < b; x++)
            out[x] = above[x];
        sampled(dense, w, r, y, first, a - first, out);
        sampled(dense, w, r, y, b, end - b, out);
    }
    return pair(first, end);
}

/* the stars (x, y, z, c: a direction of length 4096 and a colour) through the basis (front, right, up; 256 = 1.0):
 * (x, y, c) out for each on screen, its depth over 0.7 of its length first; the count back */
int c_stars_place(const short LZ_FAR *stars, int n, const long LZ_FAR *basis, int cx, int cy, int rows,
                  short LZ_FAR *out)
{
    uint32_t b[9];
    int16_t left = (int16_t)n, on = 0, k;
    for (k = 0; k < 9; k++)
        b[k] = (uint32_t)basis[k];
    if (left <= 0)
        return 0;
    for (; left; left--, stars += 4) {
        uint32_t x = (uint32_t)(int32_t)stars[0], y = (uint32_t)(int32_t)stars[1], z = (uint32_t)(int32_t)stars[2];
        int32_t depth = (int32_t)(x * b[0] + y * b[1] + z * b[2]), q;
        int16_t sx, sy;
        if (depth < INT32_C(734003))
            continue;
        q = (int32_t)((x * b[3] + y * b[4] + z * b[5]) * 400u) / depth;
        sx = (int16_t)((uint16_t)q + (uint16_t)cx);
        if (sx < 0 || sx >= 640)
            continue;
        q = (int32_t)((x * b[6] + y * b[7] + z * b[8]) * 400u) / depth;
        sy = (int16_t)((uint16_t)cy - (uint16_t)q);
        if (sy < 0 || sy >= (int16_t)rows)
            continue;
        out[0] = sx;
        out[1] = sy;
        out[2] = stars[3];
        out += 3;
        on++;
    }
    return on;
}

#ifdef RV_TESTS
/* --- RVT.EXE /twins: every twin against its asm --- */

#define TW_ROWS 40                      /* the bands' */
#define TW_BOLTS 320
#define TW_FATS 32
#define TW_BALLS 400
#define TW_ROCKS 300
#define TW_ROCK_OUT 400
#define TW_STARS 300

static uint32_t LZ_FAR g_tw_seed;
static uint32_t LZ_FAR g_sum;
static int LZ_FAR g_tw_bad;
static const char LZ_FAR g_tw_ok[] = "twins: %s ok, %ld cases, sum %08lX",
    LZ_FAR g_tw_differ[] = "twins: %s DIFFERS in case %ld (%ld %ld %ld %ld)",
    LZ_FAR g_tw_mem[] = "twins: no memory for the self-test",
    LZ_FAR g_tw_xms[] = "twins: no XMS for the rocks' moves",
    LZ_FAR g_tw_end[] = "twins: %s (%ld cases)";

static unsigned tw_rnd(void)
{
    g_tw_seed = g_tw_seed * UINT32_C(1103515245) + UINT32_C(12345);
    return (unsigned)((g_tw_seed >> 16) & 0x7FFF);
}

static int tw_in(int lo, int hi)        /* lo .. hi (at most 32,767 apart) */
{
    return (int)(lo + (int32_t)tw_rnd() % ((int32_t)hi - lo + 1));
}

/* the sum: FNV-1a over the outputs' values, a byte at a time, the low first */
static void sum_b(unsigned v) { g_sum = (g_sum ^ (v & 255)) * UINT32_C(16777619); }
static void sum_16(int32_t v) { sum_b((unsigned)((uint32_t)v & 255)); sum_b((unsigned)(((uint32_t)v >> 8) & 255)); }
static void sum_32(int32_t v) { sum_16(v); sum_16((int32_t)((uint32_t)v >> 16)); }
static void sum_mem(const unsigned char LZ_FAR *p, unsigned n) { while (n--) sum_b(*p++); }

/* a test's name for the log (near: printf's %s) */
static void tw_say(const char LZ_FAR *fmt, const char LZ_FAR *name, long cases)
{
    char nm[40];
    _fstrncpy(nm, name, sizeof nm - 1);
    nm[sizeof nm - 1] = 0;
    rv_logf(fmt, nm, cases, (unsigned long)g_sum);
}

/* a mismatch: logged once a test, with four numbers that say where */
static int tw_differ(const char LZ_FAR *name, long cs, long a, long b, long c, long d)
{
    char nm[40];
    _fstrncpy(nm, name, sizeof nm - 1);
    nm[sizeof nm - 1] = 0;
    rv_logf(g_tw_differ, nm, cs, a, b, c, d);
    g_tw_bad = 1;
    return 1;
}

static void tw_fill(unsigned char LZ_FAR *a, unsigned char LZ_FAR *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        a[i] = b[i] = (unsigned char)(i * 7 + 3);
}

/* nothing written in a's n bytes from base outside the columns [x0, x1) (they keep tw_fill's pattern) */
static int tw_kept(const unsigned char LZ_FAR *a, unsigned base, unsigned n, int x0, int x1)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (((int)i < x0 || (int)i >= x1) && a[base + i] != (unsigned char)((base + i) * 7 + 3))
            return 0;
    return 1;
}

/* the buffers: two bands (the twin's, the asm's) and room for each test's things */
static unsigned char LZ_FAR *g_bc, LZ_FAR *g_ba, LZ_FAR *g_mem;
static short LZ_FAR *g_ext;             /* an opening of uneven extents, every row */
static short LZ_FAR *g_open;            /* the whole width */
#define MEM_BYTES 40000U

/* --- the rows: run-length, scaled --- */

static void make_spans(unsigned char LZ_FAR *sp)
{
    int k = tw_in(0, 7), i, j, at = 0;
    for (i = 0; i < k; i++) {
        int skip = tw_in(0, 40), n = tw_in(0, 3) ? tw_in(1, 24) : 0;
        if (!skip && !n)
            skip = 1;
        sp[at++] = (unsigned char)skip;
        sp[at++] = (unsigned char)n;
        for (j = 0; j < n; j++)
            sp[at++] = (unsigned char)(tw_in(0, 3) ? tw_in(1, 255) : 0);
    }
    sp[at++] = 0;
    sp[at] = 0;
}

static void test_rows(void)
{
    static const char LZ_FAR n_blit[] = "blit_row", LZ_FAR n_rev[] = "blit_row_rev", LZ_FAR n_scale[] = "scale_row";
    unsigned char LZ_FAR *sp = g_mem, LZ_FAR *line = g_mem + 256;
    unsigned short LZ_FAR *map = (unsigned short LZ_FAR *)(g_mem + 640);
    long i;
    int k;
    for (k = 0; k < 2; k++) {
        g_sum = 0;
        for (i = 0; i < 400; i++) {
            int at = tw_in(-150, 650), xa = tw_in(0, 640), xb = tw_in(0, 640);
            if (k)
                at = tw_in(-10, 800);
            make_spans(sp);
            tw_fill(g_bc, g_ba, SCR_W);
            if (k) {
                c_blit_row_rev(g_bc, sp, at, xa, xb);
                rv_blit_row_rev(g_ba, sp, at, xa, xb);
            } else {
                c_blit_row(g_bc, sp, at, xa, xb);
                rv_blit_row(g_ba, sp, at, xa, xb);
            }
            if (_fmemcmp(g_bc, g_ba, SCR_W) && tw_differ(k ? n_rev : n_blit, i, at, xa, xb, 0))
                break;
            sum_mem(g_bc, SCR_W);
        }
        if (i == 400)
            tw_say(g_tw_ok, k ? n_rev : n_blit, i);
    }
    g_sum = 0;
    for (i = 0; i < 300; i++) {
        int xa = tw_in(0, 640), xb = tw_in(0, 640), x;
        for (x = 0; x < 320; x++)
            line[x] = (unsigned char)(tw_in(0, 3) ? tw_in(1, 255) : 0);
        for (x = 0; x < SCR_W; x++)
            map[x] = (unsigned short)tw_in(0, 319);
        tw_fill(g_bc, g_ba, SCR_W);
        c_scale_row(line, map, xa, xb, g_bc);
        rv_scale_row(line, map, xa, xb, g_ba);
        if (_fmemcmp(g_bc, g_ba, SCR_W) && tw_differ(n_scale, i, xa, xb, 0, 0))
            return;
        sum_mem(g_bc, SCR_W);
    }
    tw_say(g_tw_ok, n_scale, i);
}

/* --- the turned rows: pictures of every shape, every row of them turned and scaled --- */

static void make_picture(unsigned char LZ_FAR *d, int w, int h, int noise)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int32_t dx = 2 * x + 1 - w, dy = 2 * y + 1 - h;
            int in = noise ? tw_in(0, 1) : dx * dx * h * h + dy * dy * w * w <= (int32_t)w * w * h * h;
            unsigned char p = 0;
            if (in ? tw_in(0, 9) != 0 : tw_in(0, 39) == 0)
                p = (unsigned char)tw_in(1, 255);
            d[y * w + x] = p;
        }
}

static void test_turned(void)
{
    static const char LZ_FAR n_rot[] = "rotate_row", LZ_FAR n_row2[] = "rotate_row2", LZ_FAR n_edges[] = "rotate_edges",
        LZ_FAR n_drawn[] = "turn_drawn", LZ_FAR n_drop[] = "turn_dropped", LZ_FAR n_row2_past[] = "rotate_row2 past its span";
    static uint32_t LZ_FAR sums[5];
    static long LZ_FAR cases[5];
    unsigned char LZ_FAR *dense = g_mem, LZ_FAR *above = g_mem + 8192, LZ_FAR *ac = g_bc + SCR_W + 16,
        LZ_FAR *aa = g_ba + SCR_W + 16;
    int img, k;
    for (k = 0; k < 5; k++)
        sums[k] = 0, cases[k] = 0;
    for (img = 0; img < 36; img++) {
        int w = tw_in(3, 48), h = tw_in(3, 40), y, pl = 0, pr = 0, ox, oy, roll, scale;
        LfRot r;
        make_picture(dense, w, h, img % 4 == 3);
        for (k = 0; k < SCR_W; k++)
            above[k] = (unsigned char)(k * 13 + img);
        ox = tw_in(0, w - 1);           /* (one at a time: the order of a call's arguments is the compiler's) */
        oy = tw_in(0, h - 1);
        roll = tw_in(0, 1023);
        scale = tw_in(40, 300);
        lf_rotate_setup((int16_t)w, (int16_t)h, (int16_t)ox, (int16_t)oy, (int16_t)roll, (int16_t)scale, &r);
        for (y = 0; y < r.oh; y++) {
            int16_t xs, xe;
            long ec, ea;
            int lo, hi;
            if (!lf_rotate_span(&r, (int16_t)w, (int16_t)h, (int16_t)y, 0, r.ow, &xs, &xe)) {
                xs = xe = (int16_t)tw_in(0, r.ow);   /* (an empty span: the edges are xe) */
                if (tw_in(0, 1))
                    xs++;
            }
            /* rv_rotate_row */
            if (xs < xe) {
                tw_fill(g_bc, g_ba, SCR_W);
                c_rotate_row(dense, w, h, &r, y, xs, xe, g_bc);
                rv_rotate_row(dense, w, h, &r, y, xs, xe, g_ba);
                if (_fmemcmp(g_bc, g_ba, SCR_W) && tw_differ(n_rot, img, y, xs, xe, 0))
                    return;
                g_sum = sums[0], sum_mem(g_bc, SCR_W), sums[0] = g_sum, cases[0]++;
            }
            /* the edges */
            ec = c_rotate_edges(dense, w, h, &r, y, xs, xe);
            ea = rv_rotate_edges(dense, w, h, &r, y, xs, xe);
            if (ec != ea && tw_differ(n_edges, img, y, ec, ea, 0))
                return;
            g_sum = sums[1], sum_32(ec), sums[1] = g_sum, cases[1]++;
            /* half the detail, from the edges and from the span */
            lo = (int)(int16_t)ec;
            hi = (int)(int16_t)(ec >> 16);
            if (lo < hi || xs < xe) {
                tw_fill(g_bc, g_ba, SCR_W);
                if (lo < hi) {
                    c_rotate_row2(dense, w, h, &r, y, lo, hi, g_bc);
                    rv_rotate_row2(dense, w, h, &r, y, lo, hi, g_ba);
                }
                if (xs < xe) {
                    c_rotate_row2(dense, w, h, &r, y, xs, xe, g_bc + 320);
                    rv_rotate_row2(dense, w, h, &r, y, xs, xe, g_ba + 320);
                }
                if (_fmemcmp(g_bc, g_ba, SCR_W) && tw_differ(n_row2, img, y, lo, hi, xs))
                    return;
                /* (only its span written, by both: an odd one's last column too) */
                if ((!tw_kept(g_bc, 0, 320, lo, lo < hi ? hi : lo) || !tw_kept(g_ba, 0, 320, lo, lo < hi ? hi : lo)
                     || !tw_kept(g_bc, 320, 320, xs, xs < xe ? xe : xs) || !tw_kept(g_ba, 320, 320, xs, xs < xe ? xe : xs))
                    && tw_differ(n_row2_past, img, y, lo, hi, xe))
                    return;
                g_sum = sums[2], sum_mem(g_bc, SCR_W), sums[2] = g_sum, cases[2]++;
            }
            /* a drawn row */
            tw_fill(g_bc, g_ba, SCR_W);
            ec = c_turn_drawn(dense, w, h, &r, y, xs, xe, g_bc);
            ea = rv_turn_drawn(dense, w, h, &r, y, xs, xe, g_ba);
            if ((ec != ea || _fmemcmp(g_bc, g_ba, SCR_W)) && tw_differ(n_drawn, img, y, ec, ea, xs))
                return;
            g_sum = sums[3], sum_32(ec), sum_mem(g_bc, SCR_W), sums[3] = g_sum, cases[3]++;
            /* a dropped row: from the row above's edges as the ships draw them, or any */
            if (tw_in(0, 3)) {
                pl += tw_in(-3, 3);
                pr += tw_in(-3, 3);
            } else {
                pl = tw_in(xs - 5, xe + 5);
                pr = pl + tw_in(-4, 30);
            }
            _fmemcpy(ac, above, SCR_W);
            _fmemcpy(aa, above, SCR_W);
            tw_fill(g_bc, g_ba, SCR_W);
            ec = c_turn_dropped(dense, w, h, &r, y, xs, xe, pl, pr, g_bc, ac);
            ea = rv_turn_dropped(dense, w, h, &r, y, xs, xe, pl, pr, g_ba, aa);
            if ((ec != ea || _fmemcmp(g_bc, g_ba, SCR_W)) && tw_differ(n_drop, img, y, ec, ea, (long)pl << 16 | pr))
                return;
            g_sum = sums[4], sum_32(ec), sum_mem(g_bc, SCR_W), sums[4] = g_sum, cases[4]++;
            _fmemcpy(above, g_bc, SCR_W);
            pl = (int)(int16_t)ec;
            pr = (int)(int16_t)(ec >> 16);
        }
    }
    g_sum = sums[0], tw_say(g_tw_ok, n_rot, cases[0]);
    g_sum = sums[2], tw_say(g_tw_ok, n_row2, cases[2]);
    g_sum = sums[1], tw_say(g_tw_ok, n_edges, cases[1]);
    g_sum = sums[3], tw_say(g_tw_ok, n_drawn, cases[3]);
    g_sum = sums[4], tw_say(g_tw_ok, n_drop, cases[4]);
}

/* --- the stars --- */

static void test_stars(void)
{
    static const char LZ_FAR n_stars[] = "stars_place";
    short LZ_FAR *st = (short LZ_FAR *)g_mem, LZ_FAR *oc = (short LZ_FAR *)g_bc, LZ_FAR *oa = (short LZ_FAR *)g_ba;
    long basis[9], i;
    int k, nc, na;
    g_sum = 0;
    for (i = 0; i < 24; i++) {
        int cx = tw_in(200, 440), cy = tw_in(50, 350), rows = tw_in(1, 400), n = i ? TW_STARS : 0;
        for (k = 0; k < TW_STARS * 4; k++)
            st[k] = (short)(k % 4 == 3 ? tw_in(0, 255) : tw_in(-4096, 4096));
        for (k = 0; k < 9; k++)
            basis[k] = i < 12 ? tw_in(-300, 300) : tw_in(-1500, 1500);   /* (the second dozen's products wrap) */
        _fmemset(oc, 0, TW_STARS * 6);
        _fmemset(oa, 0, TW_STARS * 6);
        nc = c_stars_place(st, n, basis, cx, cy, rows, oc);
        na = rv_stars_place(st, n, basis, cx, cy, rows, oa);
        if ((nc != na || _fmemcmp(oc, oa, TW_STARS * 6)) && tw_differ(n_stars, i, nc, na, 0, 0))
            return;
        sum_16(nc);
        for (k = 0; k < nc * 3; k++)
            sum_16(oc[k]);
    }
    tw_say(g_tw_ok, n_stars, i);
}

/* --- the bolts: placed, drawn as lines and fat ones, the engines' flames, flown --- */

static void tw_camera(LzCam LZ_FAR *cam, int turned, long radius16, long zmin, int rmax16, int line16)
{
    long f[3], r[3], u[3];
    f[0] = 0; f[1] = 0; f[2] = 256;
    r[0] = 256; r[1] = 0; r[2] = 0;
    u[0] = 0; u[1] = 256; u[2] = 0;
    if (turned) {                       /* (front a little up and right, rolled) */
        f[0] = 60; f[1] = 40; f[2] = 246;
        r[0] = 247; r[1] = -30; r[2] = -55;
        u[0] = -38; u[1] = 250; u[2] = -33;
    }
    lz_camera(cam, 0, 0, 0, f, r, u, SCR_W / 2, SCR_H / 2, 400, radius16, zmin, 40000L, rmax16, line16, 0, SCR_H);
}

/* band w's rows (TW_ROWS a band), cut as the flight cuts them: to the opening's rows 12 .. 387 with the extents
 * (the second round), else the whole width */
static int tw_band(int w, int *y0, int *cy0, int *cy1, const short LZ_FAR **ext, int *e0)
{
    *y0 = (w % (SCR_H / TW_ROWS)) * TW_ROWS;
    *cy0 = *y0;
    *cy1 = *y0 + TW_ROWS;
    if (w >= SCR_H / TW_ROWS) {
        *ext = g_ext + 2 * 12;
        *e0 = 12;
        if (*cy0 < 12) *cy0 = 12;
        if (*cy1 > 388) *cy1 = 388;
    } else {
        *ext = g_open;
        *e0 = 0;
    }
    return *cy0 < *cy1;
}

static void sum_line(const LzLine LZ_FAR *l)
{
    sum_16(l->y0); sum_16(l->y1); sum_32(l->ax); sum_32(l->bx); sum_32(l->x1); sum_32(l->m);
}

static void sum_pass(const LzPass LZ_FAR *p)
{
    sum_16(p->top); sum_16(p->bot); sum_16(p->ax); sum_16(p->ay); sum_16(p->bx); sum_16(p->by); sum_16(p->r2);
    sum_16(p->la); sum_16(p->lb); sum_16(p->ra); sum_16(p->rb); sum_16(p->colour); sum_32(p->c0); sum_32(p->m);
    sum_32(p->hw); sum_16(p->r);
}

static void test_bolts(void)
{
    static const char LZ_FAR n_place[] = "bolts_place", LZ_FAR n_lines[] = "lines_draw", LZ_FAR n_fat[] = "fat_setup",
        LZ_FAR n_caps[] = "capsule_setup", LZ_FAR n_pass[] = "pass_draw", LZ_FAR n_fsetup[] = "flame_setup",
        LZ_FAR n_flame[] = "flame_draw", LZ_FAR n_move[] = "bolts_move";
    static LzStyle LZ_FAR style = { 15, 15, 7, 0, 12 };
    LzBolt LZ_FAR *b = (LzBolt LZ_FAR *)g_mem;
    LzLine LZ_FAR *lc = (LzLine LZ_FAR *)(g_mem + TW_BOLTS * sizeof(LzBolt)), LZ_FAR *la = lc + TW_BOLTS;
    LzFat LZ_FAR *fc = (LzFat LZ_FAR *)(la + TW_BOLTS), LZ_FAR *fa = fc + TW_FATS;
    LzFlameLook LZ_FAR *lk;
    long lines = 0, fats = 0, caps = 0, draws = 0, flames = 0, setups = 0;
    uint32_t s_place = 0, s_lines = 0, s_fat = 0, s_caps = 0, s_flame = 0, s_fsetup = 0;
    int round, i, k, w;
    for (round = 0; round < 3; round++) {
        LzCam cam;
        int nlc, nla, nfc, nfa;
        for (i = 0; i < TW_BOLTS; i++) {   /* bolts round the camera, near and far, every way (bolts.c's test's) */
            long d = i < 60 ? 220 + tw_in(0, 679) : 1500 + tw_in(0, 7499), hx = tw_in(-96, 96), hy = tw_in(-96, 96),
                hz = tw_in(-96, 96), len = (long)lf_sqrt((uint32_t)(hx * hx + hy * hy + hz * hz));
            if (len < 8) { hx = 96; len = 96; }
            b[i].x = (long)tw_in(-1000, 1000) * d / 1000 * 700 / 1000 * 256;
            b[i].y = (long)tw_in(-1000, 1000) * d / 1000 * 450 / 1000 * 256;
            b[i].z = d * 256;
            b[i].hx = (short)(hx * 192 / len);
            b[i].hy = (short)(hy * 192 / len);
            b[i].hz = (short)(hz * 192 / len);
        }
        for (i = 0; i < 12; i++) {      /* across the near plane, behind */
            b[i].z = (200 + tw_in(0, 12) - 6) * 256L;
            b[i].hz = (short)(tw_in(0, 1) ? 192 : -192);
            b[i].hx = (short)tw_in(-20, 20);
            b[12 + i].z = -256L * (100 + tw_in(0, 899));
        }
        tw_camera(&cam, round == 1, 32, 200, 64, round == 2 ? 6 : 12);
        _fmemset(lc, 0, 2 * TW_BOLTS * sizeof(LzLine) + 2 * TW_FATS * sizeof(LzFat));   /* (no padding compared) */
        nlc = c_bolts_place(b, TW_BOLTS, &cam, lc, TW_BOLTS, fc, TW_FATS, &nfc);
        nla = rv_bolts_place(b, TW_BOLTS, &cam, la, TW_BOLTS, fa, TW_FATS, &nfa);
        if ((nlc != nla || nfc != nfa || _fmemcmp(lc, la, nlc * sizeof(LzLine)) || _fmemcmp(fc, fa, nfc * sizeof(LzFat)))
            && tw_differ(n_place, round, nlc, nla, nfc, nfa))
            return;
        g_sum = s_place;
        sum_16(nlc);
        sum_16(nfc);
        for (i = 0; i < nlc; i++)
            sum_line(&lc[i]);
        for (i = 0; i < nfc; i++) {
            sum_16(fc[i].ax); sum_16(fc[i].ay); sum_16(fc[i].bx); sum_16(fc[i].by); sum_16(fc[i].r);
        }
        s_place = g_sum;
        /* the fat ones' set-ups, at their own radii and every other, some end on */
        for (i = 0; i < nfc; i++)
            for (k = 12; k <= 64; k += 4) {
                LzFat f = fc[i];
                LzPass pc[2], pa[2];
                LzLine cc, ca;
                int clc = 0, cla = 0, n1, n2;
                f.r = (short)k;
                if (i % 4 == 3) {
                    f.bx = f.ax;
                    f.by = f.ay;
                }
                _fmemset(pc, 0, sizeof pc);
                _fmemset(pa, 0, sizeof pa);
                _fmemset(&cc, 0, sizeof cc);
                _fmemset(&ca, 0, sizeof ca);
                c_capsule_setup(&f, 99, &pc[0]);
                rv_capsule_setup(&f, 99, &pa[0]);
                if (_fmemcmp(&pc[0], &pa[0], sizeof(LzPass)) && tw_differ(n_caps, round, i, k, 0, 0))
                    return;
                g_sum = s_caps, sum_pass(&pc[0]), s_caps = g_sum, caps++;
                n1 = c_fat_setup(&f, &style, pc, &cc, &clc);
                n2 = rv_fat_setup(&f, &style, pa, &ca, &cla);
                if ((n1 != n2 || clc != cla || _fmemcmp(pc, pa, n1 * sizeof(LzPass)) || (clc && _fmemcmp(&cc, &ca, sizeof cc)))
                    && tw_differ(n_fat, round, i, k, n1, n2))
                    return;
                g_sum = s_fat;
                sum_16(n1);
                sum_16(clc);
                for (w = 0; w < n1; w++)
                    sum_pass(&pc[w]);
                if (clc)
                    sum_line(&cc);
                s_fat = g_sum;
                fats++;
            }
        /* the draws, band by band, with the extents and without: the lines, then the fat ones as the flight
         * draws them (each one's passes, its core line) */
        for (w = 0; w < SCR_H / TW_ROWS * 2; w++) {
            int y0, cy0, cy1, e0, part;
            const short LZ_FAR *ex;
            if (!tw_band(w, &y0, &cy0, &cy1, &ex, &e0))
                continue;
            for (part = 0; part < 2; part++) {
                _fmemset(g_bc, 0, TW_ROWS * SCR_W);
                _fmemset(g_ba, 0, TW_ROWS * SCR_W);
                if (!part) {
                    c_lines_draw(lc, nlc, 15, g_bc, y0, cy0, cy1, ex, e0);
                    rv_lines_draw(la, nla, 15, g_ba, y0, cy0, cy1, ex, e0);
                } else {
                    for (i = 0; i < nfc; i++) {
                        LzPass p[2];
                        LzLine core;
                        int cl, n;
                        n = c_fat_setup(&fc[i], &style, p, &core, &cl);
                        for (k = 0; k < n; k++) {
                            c_pass_draw(&p[k], g_bc, y0, cy0, cy1, ex, e0, lz_sqrt16);
                            rv_pass_draw(&p[k], g_ba, y0, cy0, cy1, ex, e0, lz_sqrt16);
                        }
                        if (cl) {
                            c_lines_draw(&core, 1, 7, g_bc, y0, cy0, cy1, ex, e0);
                            rv_lines_draw(&core, 1, 7, g_ba, y0, cy0, cy1, ex, e0);
                        }
                    }
                }
                if (_fmemcmp(g_bc, g_ba, TW_ROWS * SCR_W) && tw_differ(part ? n_pass : n_lines, round, w, y0, 0, 0))
                    return;
                g_sum = s_lines, sum_mem(g_bc, TW_ROWS * SCR_W), s_lines = g_sum;
                draws++;
            }
        }
        lines += nlc;
        /* the engines' flames: random ones (some end on) through a scrambled look, band by band with the extents */
        {
#ifdef __WATCOMC__
            void LZ_FAR *mem = g_mem + 30000;   /* (the asm wants the look paragraph aligned: its offset made 0) */
            unsigned long lin = ((unsigned long)FP_SEG(mem) << 4) + FP_OFF(mem) + 15;
            lk = (LzFlameLook LZ_FAR *)MK_FP((unsigned)(lin >> 4), 0);
#else
            lk = (LzFlameLook LZ_FAR *)(g_mem + 30016);
#endif
            for (k = 0; k < 256; k++) {
                lk->lut[k] = (unsigned char)((k & 15) > 12 ? 0 : (k * 37 + 11) & 255);
                lk->ramp_a[k] = (unsigned char)k;
                lk->ramp_b[k] = (unsigned char)(255 - k);
                lk->nr[k] = (signed char)tw_in(-3, 3);
                lk->nv[k] = (signed char)tw_in(-24, 40);
            }
            for (w = SCR_H / TW_ROWS; w < SCR_H / TW_ROWS * 2; w++) {
                int y0, cy0, cy1, e0;
                const short LZ_FAR *ex;
                if (!tw_band(w, &y0, &cy0, &cy1, &ex, &e0))
                    continue;
                _fmemset(g_bc, 0, TW_ROWS * SCR_W);
                _fmemset(g_ba, 0, TW_ROWS * SCR_W);
                for (i = 0; i < nfc; i++) {
                    LzFlame flc, fla;
                    LzFat fpc, fpa;
                    LzPass pp;
                    int tx = i % 4 == 3 ? 1 : fc[i].bx - fc[i].ax, ty = i % 4 == 3 ? 0 : fc[i].by - fc[i].ay;
                    c_flame_setup(fc[i].ax, fc[i].ay, tx * 3, ty * 3, fc[i].r * 3 / 2, i * 7 + w, &flc, &fpc);
                    rv_flame_setup(fc[i].ax, fc[i].ay, tx * 3, ty * 3, fc[i].r * 3 / 2, i * 7 + w, &fla, &fpa);
                    if ((_fmemcmp(&flc, &fla, sizeof flc) || _fmemcmp(&fpc, &fpa, sizeof fpc))
                        && tw_differ(n_fsetup, round, i, flc.mode, fla.mode, 0))
                        return;
                    g_sum = s_fsetup;
                    sum_32(flc.s00); sum_32(flc.sx); sum_32(flc.sy); sum_32(flc.e00); sum_32(flc.ex); sum_32(flc.ey);
                    sum_32(flc.kk); sum_16(flc.mode); sum_16(flc.seed);
                    sum_16(fpc.ax); sum_16(fpc.ay); sum_16(fpc.bx); sum_16(fpc.by); sum_16(fpc.r);
                    s_fsetup = g_sum;
                    setups++;
                    c_capsule_setup(&fpc, 0, &pp);
                    c_flame_draw(&pp, g_bc, y0, cy0, cy1, ex, e0, lz_sqrt16, &flc, lk);
                    rv_flame_draw(&pp, g_ba, y0, cy0, cy1, ex, e0, lz_sqrt16, &flc, lk);
                }
                if (_fmemcmp(g_bc, g_ba, TW_ROWS * SCR_W) && tw_differ(n_flame, round, w, y0, 0, 0))
                    return;
                g_sum = s_flame, sum_mem(g_bc, TW_ROWS * SCR_W), s_flame = g_sum;
                flames++;
            }
        }
    }
    g_sum = s_place, tw_say(g_tw_ok, n_place, lines);
    g_sum = s_caps, tw_say(g_tw_ok, n_caps, caps);
    g_sum = s_fat, tw_say(g_tw_ok, n_fat, fats);
    g_sum = s_lines, tw_say(g_tw_ok, n_lines, draws);   /* (the lines' and the passes' bands together) */
    g_sum = s_fsetup, tw_say(g_tw_ok, n_fsetup, setups);
    g_sum = s_flame, tw_say(g_tw_ok, n_flame, flames);
    /* the flight: every bolt flies k positions a sixteenth of its half axis */
    g_sum = 0;
    for (k = 0; k < 4; k++) {
        static const long LZ_FAR ks[4] = { 200, -77, 100000L, 1 };
        LzBolt LZ_FAR *bc = (LzBolt LZ_FAR *)g_bc, LZ_FAR *ba = (LzBolt LZ_FAR *)g_ba;
        _fmemcpy(bc, b, 40 * sizeof(LzBolt));
        _fmemcpy(ba, b, 40 * sizeof(LzBolt));
        c_bolts_move(bc, 40 - k, ks[k]);
        rv_bolts_move(ba, 40 - k, ks[k]);
        if (_fmemcmp(bc, ba, 40 * sizeof(LzBolt)) && tw_differ(n_move, k, 0, 0, 0, 0))
            return;
        for (i = 0; i < 40; i++) {
            sum_32(bc[i].x); sum_32(bc[i].y); sum_32(bc[i].z);
        }
    }
    tw_say(g_tw_ok, n_move, (long)k);
}

/* --- the balls --- */

static void test_balls(void)
{
    static const char LZ_FAR n_place[] = "balls_place", LZ_FAR n_draw[] = "balls_draw";
    static LzBallKind LZ_FAR kind[4];
    static const unsigned char LZ_FAR frames[4] = { 1, 4, 4, 4 }, LZ_FAR first[4] = { 0, 1, 5, 9 };
    static const short LZ_FAR radius16[4] = { 24, 40, 56, 32 };
    LzBall LZ_FAR *b = (LzBall LZ_FAR *)g_mem;
    LzBallOut LZ_FAR *oc = (LzBallOut LZ_FAR *)(g_mem + TW_BALLS * sizeof(LzBall)), LZ_FAR *oa = oc + TW_BALLS;
    unsigned char LZ_FAR *small = (unsigned char LZ_FAR *)(oa + TW_BALLS), LZ_FAR *big = small + 13 * LZ_BALL_SMALL_SET;
    uint32_t s_place = 0, s_draw = 0;
    long places = 0, draws = 0;
    int round, i, k, w, dmax = 40;
    for (i = 0; i < 13 * LZ_BALL_SMALL_SET + dmax * dmax; i++)
        small[i] = (unsigned char)(tw_in(0, 4) ? tw_in(1, 255) : 0);
    for (round = 0; round < 3; round++) {
        LzCam cam;
        long rc, ra;
        int nsc, nbc;
        tw_camera(&cam, round == 1, 0, 100, 0, 0);
        for (k = 0; k < 4; k++) {
            kind[k].rf = (long)radius16[k] * 256 * cam.f16;
            kind[k].z1 = kind[k].rf / 12;
            kind[k].set = first[k];
            kind[k].frames = frames[k];
            kind[k].mask = (unsigned char)(frames[k] - 1);
        }
        for (i = 0; i < TW_BALLS; i++) {   /* near and far, across 0.7 of their distance (balls.c's test's) */
            long d = i < 80 ? 120 + tw_in(0, 779) : 900 + tw_in(0, 19099);
            b[i].x = (long)tw_in(-1000, 1000) * d / 1000 * 700 / 1000 * 256;
            b[i].y = (long)tw_in(-1000, 1000) * d / 1000 * 700 / 2000 * 256;
            b[i].z = d * 256;
            b[i].vx = b[i].vy = b[i].vz = 0;
            b[i].kind = (unsigned char)tw_in(0, 3);
            b[i].phase = (unsigned char)tw_in(0, 3);
        }
        for (i = 0; i < 6; i++) {       /* at the near plane, behind, at the screen's edges */
            b[i].z = (100 + tw_in(0, 4) - 2) * 256L;
            b[6 + i].z = -256L * (100 + tw_in(0, 899));
            b[12 + i].x = (long)(tw_in(0, 1) ? 1 : -1) * b[12 + i].z * 8 / 10;
            b[18 + i].y = (long)(tw_in(0, 1) ? 1 : -1) * b[18 + i].z / 2;
        }
        _fmemset(oc, 0, TW_BALLS * sizeof(LzBallOut));
        _fmemset(oa, 0, TW_BALLS * sizeof(LzBallOut));
        rc = c_balls_place(b, TW_BALLS, &cam, kind, dmax, round * 5, oc, TW_BALLS, small);
        ra = rv_balls_place(b, TW_BALLS, &cam, kind, dmax, round * 5, oa, TW_BALLS, small);
        nsc = (int)(rc & 0xFFFF);
        nbc = (int)(rc >> 16);
        if ((rc != ra || _fmemcmp(oc, oa, TW_BALLS * sizeof(LzBallOut))) && tw_differ(n_place, round, rc, ra, 0, 0))
            return;
        g_sum = s_place;
        sum_32(rc);
        for (i = 0; i < TW_BALLS; i++)
            if (i < nsc || i >= TW_BALLS - nbc) {
                sum_16(oc[i].x); sum_16(oc[i].y); sum_b(oc[i].d); sum_b(oc[i].set);
                sum_16(oc[i].spr ? (int32_t)(oc[i].spr - small) : -1);
            }
        s_place = g_sum;
        places += nsc + nbc;
        for (i = TW_BALLS - nbc; i < TW_BALLS; i++)   /* the big ones' sprites, as a frame finds them */
            oc[i].spr = oa[i].spr = big;
        for (w = 0; w < SCR_H / TW_ROWS * 2; w++) {
            int y0, cy0, cy1, e0;
            const short LZ_FAR *ex;
            if (!tw_band(w, &y0, &cy0, &cy1, &ex, &e0))
                continue;
            _fmemset(g_bc, 0, TW_ROWS * SCR_W);
            _fmemset(g_ba, 0, TW_ROWS * SCR_W);
            c_balls_draw(oc, nsc, g_bc, y0, cy0, cy1, ex, e0);
            c_balls_draw(oc + TW_BALLS - nbc, nbc, g_bc, y0, cy0, cy1, ex, e0);
            rv_balls_draw(oa, nsc, g_ba, y0, cy0, cy1, ex, e0);
            rv_balls_draw(oa + TW_BALLS - nbc, nbc, g_ba, y0, cy0, cy1, ex, e0);
            if (_fmemcmp(g_bc, g_ba, TW_ROWS * SCR_W) && tw_differ(n_draw, round, w, y0, 0, 0))
                return;
            g_sum = s_draw, sum_mem(g_bc, TW_ROWS * SCR_W), s_draw = g_sum;
            draws++;
        }
    }
    g_sum = s_place, tw_say(g_tw_ok, n_place, places);
    g_sum = s_draw, tw_say(g_tw_ok, n_draw, draws);
}

/* --- the rocks: placed; a big one's rows moved from XMS, copied, as blocks --- */

static void test_rocks(void)
{
    static const char LZ_FAR n_place[] = "rocks_place", LZ_FAR n_xms[] = "rocks_xms", LZ_FAR n_copy[] = "rocks_copy";
    LzRock LZ_FAR *r = (LzRock LZ_FAR *)g_mem, LZ_FAR *r2 = r + TW_ROCKS;
    LzRockOut LZ_FAR *oc = (LzRockOut LZ_FAR *)(r2 + TW_ROCKS), LZ_FAR *oa = oc + TW_ROCK_OUT;
    short LZ_FAR *lists = (short LZ_FAR *)(oa + TW_ROCK_OUT);
    unsigned char LZ_FAR *hdr = g_mem, LZ_FAR *pix = g_mem + 4096;
    LzRockCam rcc, rca;
    LzCam cam;
    long f3[3], r3[3], u3[3], i;
    int nc, na, k, round;
    XmsBlock blk;
    g_sum = 0;
    f3[0] = 60; f3[1] = 40; f3[2] = 246;
    r3[0] = 247; r3[1] = -30; r3[2] = -55;
    u3[0] = -38; u3[1] = 250; u3[2] = -33;
    for (round = 0; round < 2; round++) {
        lz_camera(&cam, 1000000L, -2000000L, 3000000L, f3, r3, u3, SCR_W / 2, 180, 400, 0, 100, 70000L, 0, 0, 40,
                  320);
        for (k = 0; k < TW_ROCKS; k++) {   /* (rocks.c's test's: some within reach, some out of the bubble) */
            long s = k < 40 ? 1500 : k < 80 ? 40000L : 12000;
            r[k].x = 1000000L + (long)tw_in(-1000, 1000) * s / 1000 * 256;
            r[k].y = -2000000L + (long)tw_in(-1000, 1000) * s / 1000 * 256;
            r[k].z = 3000000L + (long)tw_in(-1000, 1000) * s / 1000 * 256;
            r[k].kind = (unsigned char)tw_in(0, 3);
            r[k].r8 = (unsigned char)tw_in(10, 125);
            r[k].phase = (unsigned char)tw_in(0, 31);
            r[k].spin = (signed char)tw_in(-4, 4);
            r[k].vx = (short)tw_in(-2000, 2000);
            r[k].vy = (short)tw_in(-2000, 2000);
            r[k].vz = (short)tw_in(-2000, 2000);
            r[k].pad = 0;
        }
        _fmemset(&rcc, 0, sizeof rcc);
        rcc.dk = 400UL * 65536UL * 16;
        rcc.far2 = 9000UL * 9000UL;
        rcc.fade2 = rcc.far2 / 10000 * 60 * 60;
        rcc.reach = 700L * 4096;
        rcc.anim = (unsigned short)(12345 + round);
        rcc.mask = 31;
        rcc.dmax = 512;
        rcc.maxreap = 64;
        rcc.maxclose = round ? 8 : 64;
        rca = rcc;
        _fmemcpy(r2, r, TW_ROCKS * sizeof(LzRock));
        _fmemset(oc, 0, 2 * TW_ROCK_OUT * sizeof(LzRockOut));
        nc = c_rocks_place(r, TW_ROCKS, &cam, &rcc, oc, round ? 50 : TW_ROCK_OUT, lists, lists + 64);
        na = rv_rocks_place(r2, TW_ROCKS, &cam, &rca, oa, round ? 50 : TW_ROCK_OUT, lists + 128, lists + 192);
        if ((nc != na || _fmemcmp(r, r2, TW_ROCKS * sizeof(LzRock)) || _fmemcmp(&rcc, &rca, sizeof rcc)
             || _fmemcmp(lists, lists + 128, rcc.nreap * sizeof(short))
             || _fmemcmp(lists + 64, lists + 192, rcc.nclose * sizeof(short))
             || _fmemcmp(oc, oa, nc * sizeof(LzRockOut))) && tw_differ(n_place, round, nc, na, rcc.nreap, rca.nreap))
            return;
        sum_16(nc);
        sum_16(rcc.nreap);
        sum_16(rcc.nclose);
        for (k = 0; k < rcc.nreap; k++)
            sum_16(lists[k]);
        for (k = 0; k < rcc.nclose; k++)
            sum_16(lists[64 + k]);
        for (k = 0; k < nc; k++) {
            sum_16(oc[k].x); sum_16(oc[k].y); sum_16(oc[k].d); sum_b(oc[k].kind); sum_b(oc[k].frame); sum_32(oc[k].z);
        }
        for (k = 0; k < TW_ROCKS; k++) {
            sum_32(r[k].x); sum_32(r[k].y); sum_32(r[k].z);
        }
    }
    tw_say(g_tw_ok, n_place, (long)round);
    /* a big rock's rows: a made-up rock (runs of every length, odd ones too), the XMS way and the copy way */
    blk = xms_alloc(16384, XMS_MISSION);
    if (!blk) {
        rv_logf(g_tw_xms);
        g_tw_bad = 1;
        return;
    }
    for (k = 0; k < 2; k++) {
        g_sum = 0;
        for (i = 0; i < 300; i++) {
            int rows = tw_in(1, 40), row, at = 0, run, x, y, band_y0 = tw_in(0, SCR_H - TW_ROWS), y0, y1, kk = 1;
            unsigned total = 0, handle;
            unsigned long off;
            if (k)
                kk = tw_in(1, 4);
            for (row = 0; row < rows; row++) {
                int nr = tw_in(0, 3);
                hdr[at++] = (unsigned char)nr;
                for (run = 0; run < nr; run++) {
                    int len = tw_in(0, 5) ? tw_in(1, 60) : 0;
                    hdr[at++] = (unsigned char)tw_in(0, 100);
                    hdr[at++] = (unsigned char)len;
                    total += len;
                }
            }
            for (row = 0; row < (int)total + 2; row++)
                pix[row] = (unsigned char)(tw_in(0, 5) ? tw_in(1, 255) : 0);
            x = tw_in(-60, 600);
            y = tw_in(band_y0 - 30, band_y0 + 50);
            y0 = tw_in(band_y0, band_y0 + TW_ROWS - 1);
            y1 = tw_in(y0, band_y0 + TW_ROWS);
            tw_fill(g_bc, g_ba, TW_ROWS * SCR_W);
            if (!k) {
                if (total)
                    xms_put(blk, 0, pix, (total + 1) & ~1U);
                xms_where(blk, &handle, &off);
                c_rocks_xms(hdr, rows, x, y, handle, off, g_bc, band_y0, y0, y1, g_ext, 0);
                rv_rocks_xms(hdr, rows, x, y, handle, off, g_ba, band_y0, y0, y1, g_ext, 0);
            } else {
                c_rocks_copy(hdr, rows, x, y, kk, pix, g_bc, band_y0, y0, y1, g_ext, 0);
                rv_rocks_copy(hdr, rows, x, y, kk, pix, g_ba, band_y0, y0, y1, g_ext, 0);
            }
            if (_fmemcmp(g_bc, g_ba, TW_ROWS * SCR_W) && tw_differ(k ? n_copy : n_xms, i, x, y, y0, y1))
                break;
            sum_mem(g_bc, TW_ROWS * SCR_W);
        }
        if (i == 300)
            tw_say(g_tw_ok, k ? n_copy : n_xms, i);
    }
    xms_free_level(XMS_MISSION);
}

long twins_selftest(void)
{
    static const char LZ_FAR s_good[] = "every twin agrees with its asm", LZ_FAR s_bad[] = "A TWIN DISAGREES";
    int k, xms;
    g_tw_seed = 4321;
    g_tw_bad = 0;
    g_bc = (unsigned char LZ_FAR *)_fmalloc(TW_ROWS * SCR_W + 64);
    g_ba = (unsigned char LZ_FAR *)_fmalloc(TW_ROWS * SCR_W + 64);
    g_mem = (unsigned char LZ_FAR *)_fmalloc(MEM_BYTES);
    g_ext = (short LZ_FAR *)_fmalloc(SCR_H * 2 * sizeof(short));
    g_open = (short LZ_FAR *)_fmalloc(SCR_H * 2 * sizeof(short));
    if (!g_bc || !g_ba || !g_mem || !g_ext || !g_open) {
        rv_logf(g_tw_mem);
        return -2;
    }
    for (k = 0; k < SCR_H; k++) {
        g_ext[2 * k] = (short)(30 + (k * 7) % 61);   /* (every 13th row shut) */
        g_ext[2 * k + 1] = (short)(k % 13 == 5 ? 20 : 610 - (k * 5) % 47);
        g_open[2 * k] = 0;
        g_open[2 * k + 1] = SCR_W;
    }
    lz_init();
    xms = xms_init(256, 1024);
    test_rows();                        /* (each to its end or its first mismatch, whatever the others found) */
    test_turned();
    test_stars();
    test_bolts();
    test_balls();
    if (xms)
        test_rocks();
    else
        rv_logf(g_tw_xms), g_tw_bad = 1;
    if (xms)
        xms_shutdown();
    tw_say(g_tw_end, g_tw_bad ? s_bad : s_good, 0);
    _ffree(g_bc);
    _ffree(g_ba);
    _ffree(g_mem);
    _ffree(g_ext);
    _ffree(g_open);
    return g_tw_bad ? -1 : 0;
}
#endif /* RV_TESTS */
