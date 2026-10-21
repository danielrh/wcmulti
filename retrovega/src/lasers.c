/* lasers.c: laser bolts through the camera and onto the screen (lasers.h).
 *
 * The reference: lasers.asm's rv_* do what the lz_* do here, to the bit (the
 * flight compares them at its start, tests/host checks these).
 *
 * The capsule.  Its ends a (upper) and b, radius r: the boundary of each side
 * is an arc of a's circle, a straight stretch (the segment moved out by r
 * along its normal) and an arc of b's circle, met at the tangent points
 * a + r n and b + r n.  A row is sampled at its centre: on the left the
 * boundary is a's arc above the tangent's row, the stretch, then b's arc; the
 * same on the right with the other normal.  The pixels whose centres lie
 * within are the row's span.
 *
 * The line.  A bolt under a pixel across is its segment: each row gets the
 * pixels whose centres the segment passes over within the row (the run of an
 * x-major line), and when that is none (a steep one) the pixel under the
 * row's middle, so every row it crosses shows. */
#include "lasers.h"

unsigned char LZ_FAR lz_sqrt16[LZ_SQRT_N];

static unsigned long isqrt(unsigned long v)
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

void lz_init(void)
{
    int k;
    for (k = 0; k < LZ_SQRT_N; k++) {
        unsigned long s = isqrt(16UL * k + 6);   /* sqrt(4 k + 1.5) x 2 (the bucket's middle), to round */
        lz_sqrt16[k] = (unsigned char)((s + 1) / 2);
    }
}

/* a x b / c, the product taken whole (the asm's imul into edx:eax, then idiv) */
#ifdef __WATCOMC__
typedef long long lz_wide;
#else
typedef long long lz_wide;
#endif
static long muldiv(long a, long b, long c) { return (long)((lz_wide)a * b / c); }

void lz_camera(LzCam LZ_FAR *c, long px, long py, long pz, const long *front, const long *right, const long *up,
               int cx, int cy, int focal, long radius16, long zmin_km, long zmax_km, int rmax16, int line16,
               int y0, int y1)
{
    int i;
    c->bx = px - 0x1000000L;
    c->by = py - 0x1000000L;
    c->bz = pz - 0x1000000L;
    for (i = 0; i < 3; i++) {
        c->f[i] = front[i];
        c->r[i] = right[i];
        c->u[i] = up[i];
    }
    c->f16 = 16L * focal;
    c->cx16 = 16L * cx;
    c->cy16 = 16L * cy;
    c->zmin = zmin_km * 4096;
    c->zmax = zmax_km * 4096;
    c->rf = radius16 * 256 * c->f16;    /* the radius in camera units (sixteenths of a km x 256) x f16 */
    c->rmax16 = (short)rmax16;
    c->line16 = (short)line16;
    c->xmin16 = 0;
    c->xmax16 = (short)(16 * 640);
    c->ymin16 = (short)(16 * y0);
    c->ymax16 = (short)(16 * y1);
}

void lz_line_of(int ax, int ay, int bx, int by, LzLine LZ_FAR *out)
{
    long dx = (long)bx - ax, dy = (long)by - ay;
    out->y0 = (short)(ay >> 4);
    out->y1 = (short)((by >> 4) + 1);
    out->ax = ((long)ax << 12) + 0x7FFF;
    out->bx = ((long)bx << 12) + 0x7FFF;
    if (out->y1 - out->y0 == 1) {
        out->m = dx >= 0 ? 0 : -1;
        out->x1 = out->bx;
    } else {
        long e = ((long)(out->y0 + 1) << 4) - ay;   /* 1 .. 16: from a to the row's bottom edge */
        out->m = (dx << 16) / dy;
        out->x1 = out->ax + muldiv(dx * e, 4096, dy);
    }
}

void lz_place(const LzBolt LZ_FAR *b, int n, const LzCam LZ_FAR *c, LzLine LZ_FAR *lines, int *nlines, int maxlines,
              LzFat LZ_FAR *fats, int *nfats, int maxfats)
{
    int i, nl = 0, nf = 0;
    for (i = 0; i < n; i++, b++) {
        long rx, ry, rz, X, Y, Z, HX, HY, HZ, xh, yh, zh, xt, yt, zt, r16, sxh, syh, sxt, syt, lo, hi;
        int ax, ay, bx, by;
        /* the middle relative to the camera, in sixteenths of a km; beyond 2^20 (65,536 km) on an axis: gone */
        rx = b->x - c->bx;
        ry = b->y - c->by;
        rz = b->z - c->bz;
        if ((unsigned long)rx >= 0x2000000UL || (unsigned long)ry >= 0x2000000UL || (unsigned long)rz >= 0x2000000UL)
            continue;
        rx = (rx >> 4) - 0x100000L;
        ry = (ry >> 4) - 0x100000L;
        rz = (rz >> 4) - 0x100000L;
        Z = rx * c->f[0] + ry * c->f[1] + rz * c->f[2];
        if (Z > c->zmax)
            continue;
        X = rx * c->r[0] + ry * c->r[1] + rz * c->r[2];
        Y = rx * c->u[0] + ry * c->u[1] + rz * c->u[2];
        HX = (long)b->hx * c->r[0] + (long)b->hy * c->r[1] + (long)b->hz * c->r[2];
        HY = (long)b->hx * c->u[0] + (long)b->hy * c->u[1] + (long)b->hz * c->u[2];
        HZ = (long)b->hx * c->f[0] + (long)b->hy * c->f[1] + (long)b->hz * c->f[2];
        xh = X + HX; yh = Y + HY; zh = Z + HZ;
        xt = X - HX; yt = Y - HY; zt = Z - HZ;
        /* the near plane: an end behind it is brought to it along the bolt */
        if (zh < c->zmin) {
            if (zt < c->zmin)
                continue;
            xh = xt + muldiv(xh - xt, c->zmin - zt, zh - zt);
            yh = yt + muldiv(yh - yt, c->zmin - zt, zh - zt);
            zh = c->zmin;
        } else if (zt < c->zmin) {
            xt = xh + muldiv(xt - xh, c->zmin - zh, zt - zh);
            yt = yh + muldiv(yt - yh, c->zmin - zh, zt - zh);
            zt = c->zmin;
        }
        r16 = c->rf / ((zh + zt) >> 1);
        if (r16 > c->rmax16)
            r16 = c->rmax16;
        sxh = c->cx16 + muldiv(xh, c->f16, zh);
        syh = c->cy16 - muldiv(yh, c->f16, zh);
        sxt = c->cx16 + muldiv(xt, c->f16, zt);
        syt = c->cy16 - muldiv(yt, c->f16, zt);
        /* kept if its box (with the radius) touches the screen's */
        lo = sxh < sxt ? sxh : sxt;
        hi = sxh < sxt ? sxt : sxh;
        if (hi + r16 < c->xmin16 || lo - r16 >= c->xmax16)
            continue;
        lo = syh < syt ? syh : syt;
        hi = syh < syt ? syt : syh;
        if (hi + r16 < c->ymin16 || lo - r16 >= c->ymax16)
            continue;
        /* a: the upper end (the left one when level) */
        if (syh < syt || (syh == syt && sxh <= sxt)) {
            ax = (int)sxh; ay = (int)syh; bx = (int)sxt; by = (int)syt;
        } else {
            ax = (int)sxt; ay = (int)syt; bx = (int)sxh; by = (int)syh;
        }
        if (r16 < c->line16) {
            if (nl >= maxlines || (long)bx - ax >= 16384 || (long)ax - bx >= 16384)
                continue;
            {
                LzLine l;
                lz_line_of(ax, ay, bx, by, &l);
                lines[nl++] = l;
            }
        } else {
            if (nf >= maxfats)
                continue;
            fats[nf].ax = (short)ax; fats[nf].ay = (short)ay;
            fats[nf].bx = (short)bx; fats[nf].by = (short)by;
            fats[nf].r = (short)r16;
            fats[nf].pad = 0;
            nf++;
        }
    }
    *nlines = nl;
    *nfats = nf;
}

void lz_move(LzBolt LZ_FAR *b, int n, long k)
{
    for (; n > 0; n--, b++) {
        b->x += (long)b->hx * k;
        b->y += (long)b->hy * k;
        b->z += (long)b->hz * k;
    }
}

/* --- a fat bolt's passes --- */

/* a segment's length x16 with sh more bits (len << sh), sh as large as keeps the square in 31 bits: a
 * short one's length must not be rounded to a whole sixteenth (its body's width would be off by a tenth) */
static long seg_len(long dx, long dy, int *sh)
{
    unsigned long v = (unsigned long)(dx * dx + dy * dy);
    *sh = 0;
    while (*sh < 8 && v < (1UL << 29)) {
        v <<= 2;
        ++*sh;
    }
    return (long)isqrt(v);
}

static void pass_of(const LzFat LZ_FAR *f, long len, int sh, int r, int colour, LzPass LZ_FAR *p)
{
    long dx = (long)f->bx - f->ax, dy = (long)f->by - f->ay;
    int yc;
    p->ax = f->ax; p->ay = f->ay; p->bx = f->bx; p->by = f->by;
    p->pad = 0;
    p->r = (short)r;
    p->r2 = (short)((long)r * r);
    p->colour = (short)colour;
    p->top = (short)((f->ay - r + 7) >> 4);               /* the first row whose centre is at or below a - r */
    p->bot = (short)(((f->by + r - 8) >> 4) + 1);         /* past the last whose centre is at or above b + r */
    if (dy == 0) {                                         /* level (or a dot): a's arc on the left, b's on the right */
        p->la = p->lb = 32767;
        p->ra = p->rb = -32768;
        p->hw = 0;
        p->m = 0;
        p->c0 = 0;
        return;
    }
    {
        /* the tangent points' rows, from the ends' (off = r dx / len): rounded so the straight stretch is
         * only used between them, never past (on a nearly level bolt a sixteenth there is pixels of x) */
        lz_wide num = (lz_wide)r * dx << sh;
        long fo = (long)(num / len), co = fo;
        if ((lz_wide)fo * len > num) fo--;           /* floor */
        if ((lz_wide)co * len < num) co++;           /* ceil */
        p->la = (short)(f->ay + co);
        p->lb = (short)(f->by + fo + 1);
        p->ra = (short)(f->ay - fo);
        p->rb = (short)(f->by - co + 1);
    }
    /* the body: the middle line's x at a row, less / plus the half width a row cuts (r len / dy), 16.16 px */
    p->hw = muldiv((long)r * len, 4096L >> sh, dy);
    p->m = (dx << 16) / dy;
    yc = p->top * 16 + 8;
    p->c0 = ((long)f->ax << 12) + muldiv(((long)yc - f->ay) * dx, 4096, dy);
}

int lz_fat_setup(const LzFat LZ_FAR *f, const LzStyle *st, LzPass LZ_FAR *pass, LzLine LZ_FAR *core, int *core_line)
{
    int n = 0, r = f->r, rc, sh;
    long len = seg_len((long)f->bx - f->ax, (long)f->by - f->ay, &sh);
    pass_of(f, len, sh, r, st->body, &pass[n++]);
    rc = r * 3 / 8;
    if (rc >= st->body16) {
        pass_of(f, len, sh, rc, st->core, &pass[n++]);
        *core_line = 0;
    } else {
        lz_line_of(f->ax, f->ay, f->bx, f->by, core);
        *core_line = 1;
    }
    return n;
}

void lz_capsule_setup(const LzFat LZ_FAR *f, int colour, LzPass LZ_FAR *p)
{
    int sh;
    long len = seg_len((long)f->bx - f->ax, (long)f->by - f->ay, &sh);
    pass_of(f, len, sh, f->r, colour, p);
}

/* --- the draws --- */

static int clip_rows(int *y0, int *y1, int ext_y0, int ext_rows, const short LZ_FAR *ext)
{
    if (ext) {
        if (*y0 < ext_y0) *y0 = ext_y0;
        if (*y1 > ext_y0 + ext_rows) *y1 = ext_y0 + ext_rows;
    }
    if (*y0 < 0) *y0 = 0;
    if (*y1 > 400) *y1 = 400;
    return *y0 < *y1;
}

static void span(unsigned char LZ_FAR *row, int xs, int xe, int y, unsigned char colour, const short LZ_FAR *ext,
                 int ext_y0)
{
    int lo = 0, hi = 640;
    if (ext) {
        lo = ext[2 * (y - ext_y0)];
        hi = ext[2 * (y - ext_y0) + 1];
    }
    if (xs < lo) xs = lo;
    if (xe > hi) xe = hi;
    while (xs < xe)
        row[xs++] = colour;
}

void lz_lines_draw(const LzLine LZ_FAR *l, int n, unsigned char colour, unsigned char LZ_FAR *band, int band_y0,
                   int y0, int y1, const short LZ_FAR *ext, int ext_y0, int ext_rows)
{
    int i;
    if (!clip_rows(&y0, &y1, ext_y0, ext_rows, ext))
        return;
    for (i = 0; i < n; i++, l++) {
        int ys = l->y0 > y0 ? l->y0 : y0, ye = l->y1 < y1 ? l->y1 : y1, y;
        long top;
        if (ys >= ye)
            continue;
        top = ys == l->y0 ? l->ax : l->x1 + l->m * (ys - l->y0 - 1);   /* x' at the row's top (or a) */
        for (y = ys; y < ye; y++) {
            long bot = y == l->y1 - 1 ? l->bx : (y == l->y0 ? l->x1 : top + l->m);
            int xs, xe;
            if (l->m >= 0) {
                xs = (int)(top >> 16);
                xe = (int)(bot >> 16);
            } else {
                xs = (int)(bot >> 16);
                xe = (int)(top >> 16);
            }
            if (xe <= xs) {                 /* no centre passed over: the pixel under the row's middle */
                xs = (int)((top + bot - 65536L) >> 17);
                xe = xs + 1;
            }
            span(band + (unsigned)(y - band_y0) * 640, xs, xe, y, colour, ext, ext_y0);
            top = bot;
        }
    }
}

/* the root table's index for half a chord at d from the middle (x16): (r^2 - d^2) / 4, never below 0 */
static int root_index(int r2, int d)
{
    int k = r2 - d * d;
    return k > 0 ? k >> 2 : 0;
}

/* row y's span of a pass, [*xs, *xe) before the extents; c the middle line's x there (16.16 px) */
static void pass_span(const LzPass LZ_FAR *p, int y, long c, int *xs, int *xe)
{
    int yc = y * 16 + 8, d;
    long L, R, hw = p->hw;
    if (yc < p->la) {
        d = yc - p->ay;
        L = (long)(p->ax - lz_sqrt16[root_index(p->r2, d)]) << 12;
    } else if (yc < p->lb) {
        L = c - hw;
    } else {
        d = yc - p->by;
        L = (long)(p->bx - lz_sqrt16[root_index(p->r2, d)]) << 12;
    }
    if (yc < p->ra) {
        d = yc - p->ay;
        R = (long)(p->ax + lz_sqrt16[root_index(p->r2, d)]) << 12;
    } else if (yc < p->rb) {
        R = c + hw;
    } else {
        d = yc - p->by;
        R = (long)(p->bx + lz_sqrt16[root_index(p->r2, d)]) << 12;
    }
    *xs = (int)((L + 0x7FFF) >> 16);
    *xe = (int)((R + 0x8000) >> 16);
}

void lz_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext,
                  int ext_y0, int ext_rows)
{
    int ys = p->top > y0 ? p->top : y0, ye = p->bot < y1 ? p->bot : y1, y;
    long c;
    if (!clip_rows(&ys, &ye, ext_y0, ext_rows, ext))
        return;
    c = p->c0 + p->m * (ys - p->top);
    for (y = ys; y < ye; y++, c += p->m) {
        int xs, xe;
        pass_span(p, y, c, &xs, &xe);
        if (xs < xe)
            span(band + (unsigned)(y - band_y0) * 640, xs, xe, y, (unsigned char)p->colour, ext, ext_y0);
    }
}

/* --- the engines' flames --- */

void lz_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame LZ_FAR *f, LzFat LZ_FAR *fp)
{
    long t2 = (long)tx * tx + (long)ty * ty, len = (long)isqrt((unsigned long)t2), lr;
    int rf = r16 * 9 / 8 + 2;           /* the footprint: the profile is at most 1 radius wide, its edge's noise 3/16 */
    f->seed = (short)seed;
    if (len * 4 < r16) {
        /* end on, or nearly: a disc; s down and e across it, in radii */
        f->mode = 1;
        f->sx = 0;
        f->sy = muldiv(16, 65536L, r16);
        f->s00 = muldiv(8 - ay, 65536L, r16);
        f->ex = f->sy;
        f->ey = 0;
        f->e00 = muldiv(8 - ax, 65536L, r16);
        f->kk = 65536L;
        tx = ty = 0;
    } else {
        f->mode = 0;
        f->sx = muldiv(16L * tx, 65536L, t2);
        f->sy = muldiv(16L * ty, 65536L, t2);
        f->s00 = muldiv((long)(8 - ax) * tx + (long)(8 - ay) * ty, 65536L, t2);
        lr = len * r16;
        f->ex = muldiv(16L * ty, 65536L, lr);
        f->ey = muldiv(-16L * tx, 65536L, lr);
        f->e00 = muldiv((long)(8 - ax) * ty - (long)(8 - ay) * tx, 65536L, lr);
        f->kk = muldiv(len, 65536L, r16);
    }
    if (rf > LZ_RMAX16)
        rf = LZ_RMAX16;
    /* the footprint, a capsule round the flame: a the upper end */
    if (ty < 0 || (ty == 0 && tx < 0)) {
        fp->ax = (short)(ax + tx); fp->ay = (short)(ay + ty); fp->bx = (short)ax; fp->by = (short)ay;
    } else {
        fp->ax = (short)ax; fp->ay = (short)ay; fp->bx = (short)(ax + tx); fp->by = (short)(ay + ty);
    }
    fp->r = (short)rf;
    fp->pad = 0;
}

/* one row's pixels xs .. xe-1 of a flame, shaded two at a time: each pair of pixels shows its level's two colours
 * (the stipple: the first ramp_a where x + y is odd), the noise is the pair's */
static void flame_span(const LzFlame LZ_FAR *f, const LzFlameLook LZ_FAR *lk, unsigned char LZ_FAR *row, int y, int xs,
                       int xe)
{
    long s = f->s00 + f->sy * y + f->sx * xs, e = f->e00 + f->ey * y + f->ex * xs, s2 = f->sx * 2, e2 = f->ex * 2;
    unsigned n = (unsigned)(y * 7 + xs * 13 + f->seed) & 255;
    int odd = (y + xs) & 1, x;
    for (x = xs; x < xe; x += 2, s += s2, e += e2, n = (n + 26) & 255) {
        long ae = e < 0 ? -e : e, r;
        int sr = 0, ri, t;
        if (f->mode || s < 0) {
            /* the round root (or an end-on disc): the distance from the nozzle, max + 3/8 min */
            long c = f->mode ? (s < 0 ? -s : s) : (long)(((lz_wide)-s * f->kk) >> 16);
            r = ae > c ? ae + (c * 3 >> 3) : c + (ae * 3 >> 3);
        } else {
            if (s >= 65536L)
                continue;
            r = ae;
            sr = (int)(s >> 12) * 16;
        }
        if (r >= 19L << 12)
            continue;
        ri = (int)(r >> 12) + lk->nr[n];
        if (ri < 0)
            ri = 0;
        if (ri >= 16)
            continue;
        t = lk->lut[sr + ri];
        if (!t)
            continue;
        t += lk->nv[n];
        if (t < 0) t = 0;
        if (t > 255) t = 255;
        row[x] = odd ? lk->ramp_a[t] : lk->ramp_b[t];
        if (x + 1 < xe)
            row[x + 1] = odd ? lk->ramp_b[t] : lk->ramp_a[t];
    }
}

void lz_flame_draw(const LzPass LZ_FAR *p, const LzFlame LZ_FAR *f, const LzFlameLook LZ_FAR *lk, unsigned char LZ_FAR *band,
                   int band_y0, int y0, int y1, const short LZ_FAR *ext, int ext_y0, int ext_rows)
{
    int ys = p->top > y0 ? p->top : y0, ye = p->bot < y1 ? p->bot : y1, y;
    long c;
    if (!clip_rows(&ys, &ye, ext_y0, ext_rows, ext))
        return;
    c = p->c0 + p->m * (ys - p->top);
    for (y = ys; y < ye; y++, c += p->m) {
        int xs, xe, lo = 0, hi = 640;
        pass_span(p, y, c, &xs, &xe);
        if (ext) {
            lo = ext[2 * (y - ext_y0)];
            hi = ext[2 * (y - ext_y0) + 1];
        }
        if (xs < lo) xs = lo;
        if (xe > hi) xe = hi;
        if (xs < xe)
            flame_span(f, lk, band + (unsigned)(y - band_y0) * 640, y, xs, xe);
    }
}

