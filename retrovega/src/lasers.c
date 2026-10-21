/* lasers.c: laser bolts through the camera and onto the screen (lasers.h).
 *
 * The reference: rv_bolts_place, rv_lines_draw and rv_pass_draw
 * (lowlevel.asm) do what lz_place, lz_lines_draw and lz_pass_draw do here,
 * to the bit (the flight compares them at its start, tests/host checks these).
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
    if (r >= st->rim16) {
        pass_of(f, len, sh, r, st->rim, &pass[n++]);
        pass_of(f, len, sh, r * 5 / 8, st->body, &pass[n++]);
        rc = r / 4;
    } else {
        pass_of(f, len, sh, r, st->body, &pass[n++]);
        rc = r * 3 / 8;
    }
    if (rc >= st->body16) {
        pass_of(f, len, sh, rc, st->core, &pass[n++]);
        *core_line = 0;
    } else {
        lz_line_of(f->ax, f->ay, f->bx, f->by, core);
        *core_line = 1;
    }
    return n;
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

void lz_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext,
                  int ext_y0, int ext_rows)
{
    int ys = p->top > y0 ? p->top : y0, ye = p->bot < y1 ? p->bot : y1, y;
    long c, hw = p->hw;
    if (!clip_rows(&ys, &ye, ext_y0, ext_rows, ext))
        return;
    c = p->c0 + p->m * (ys - p->top);
    for (y = ys; y < ye; y++, c += p->m) {
        int yc = y * 16 + 8, d, xs, xe;
        long L, R;
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
        xs = (int)((L + 0x7FFF) >> 16);
        xe = (int)((R + 0x8000) >> 16);
        if (xs < xe)
            span(band + (unsigned)(y - band_y0) * 640, xs, xe, y, (unsigned char)p->colour, ext, ext_y0);
    }
}

/* --- the simpler bolts: polygons --- */

static void poly_rows(LzPoly LZ_FAR *p)
{
    int i, lo = p->y[0], hi = p->y[0];
    for (i = 1; i < p->n; i++) {
        if (p->y[i] < lo) lo = p->y[i];
        if (p->y[i] > hi) hi = p->y[i];
    }
    p->top = (short)((lo + 7) >> 4);    /* the first row whose centre is at or below the top */
    p->bot = (short)(((hi - 8) >> 4) + 1);
}

static void poly4(LzPoly LZ_FAR *p, int colour, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3)
{
    p->n = 4;
    p->colour = (short)colour;
    p->x[0] = (short)x0; p->y[0] = (short)y0;
    p->x[1] = (short)x1; p->y[1] = (short)y1;
    p->x[2] = (short)x2; p->y[2] = (short)y2;
    p->x[3] = (short)x3; p->y[3] = (short)y3;
    poly_rows(p);
}

/* LZ_SHAPE_ROUND: an octagon (flats at w, corners at 1.08 w) swept from a to
 * b, its sum with the segment.  Octagon edge k, from vertex k to k + 1, faces
 * (1,0), (1,1), (0,1), (-1,1), (-1,0), (-1,-1), (0,-1), (1,-1): it is the
 * outline's edge at b where that . (b - a) > 0, else at a; where two edges
 * in a row take different ends, the long side along the bolt joins them at
 * the vertex between.  At most ten vertices; end on (b = a), the octagon,
 * which at a few pixels covers a circle's pixels.  No root, no division. */
static void round_of(const LzFat LZ_FAR *f, int w, int colour, LzPoly LZ_FAR *p)
{
    int s = w * 53 >> 7, k, n = 0, last, side[8];
    int vx[8], vy[8];
    long dx = (long)f->bx - f->ax, dy = (long)f->by - f->ay, dot[4];
    vx[0] = w; vy[0] = -s; vx[1] = w; vy[1] = s; vx[2] = s; vy[2] = w; vx[3] = -s; vy[3] = w;
    vx[4] = -w; vy[4] = s; vx[5] = -w; vy[5] = -s; vx[6] = -s; vy[6] = -w; vx[7] = s; vy[7] = -w;
    dot[0] = dx; dot[1] = dx + dy; dot[2] = dy; dot[3] = dy - dx;
    for (k = 0; k < 4; k++) {
        side[k] = dot[k] > 0;
        side[k + 4] = dot[k] < 0;
    }
    last = side[7];
    for (k = 0; k < 8; k++) {
        if (side[k] != last) {          /* the long side: from the end the last edge took */
            p->x[n] = (short)((last ? f->bx : f->ax) + vx[k]);
            p->y[n] = (short)((last ? f->by : f->ay) + vy[k]);
            n++;
            last = side[k];
        }
        p->x[n] = (short)((last ? f->bx : f->ax) + vx[k]);
        p->y[n] = (short)((last ? f->by : f->ay) + vy[k]);
        n++;
    }
    p->n = (short)n;
    p->colour = (short)colour;
    poly_rows(p);
}

/* the bolt's outline at half width w (x16) */
static void slab_of(const LzFat LZ_FAR *f, int shape, int w, int colour, LzPoly LZ_FAR *p)
{
    int ax = f->ax, ay = f->ay, bx = f->bx, by = f->by;
    long dx = (long)bx - ax, dy = (long)by - ay, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    long len = adx > ady ? adx + ady * 3 / 8 : ady + adx * 3 / 8;   /* the length within 7 %: no root */
    if (shape == LZ_SHAPE_ROUND) {
        round_of(f, w, colour, p);
        return;
    }
    if (shape == LZ_SHAPE_AXIS || len < 16) {
        /* ends level for a steep bolt, upright for a shallow one, each pushed out by w along the bolt so a
         * bolt end on is a square 2w across */
        if (ady >= adx)
            poly4(p, colour, ax - w, ay - w, ax + w, ay - w, bx + w, by + w, bx - w, by + w);
        else if (dx >= 0)
            poly4(p, colour, ax - w, ay - w, bx + w, by - w, bx + w, by + w, ax - w, ay + w);
        else
            poly4(p, colour, ax + w, ay - w, ax + w, ay + w, bx - w, by + w, bx - w, by - w);
        return;
    }
    {
        int nx = (int)(-dy * w / len), ny = (int)(dx * w / len);   /* w across the bolt */
        if (shape == LZ_SHAPE_RECT) {
            /* its ends pushed out by w along the bolt, (ny, -nx), as the other shapes' are: end on it is a
             * square, not a sliver as thin as the bolt's projected length */
            poly4(p, colour, ax - ny + nx, ay + nx + ny, bx + ny + nx, by - nx + ny, bx + ny - nx, by - nx - ny,
                  ax - ny - nx, ay + nx - ny);
        } else {
            int tx = (int)(dx * w / len), ty = (int)(dy * w / len);   /* w along it: the points */
            p->n = 6;
            p->colour = (short)colour;
            p->x[0] = (short)(ax - tx); p->y[0] = (short)(ay - ty);
            p->x[1] = (short)(ax + nx); p->y[1] = (short)(ay + ny);
            p->x[2] = (short)(bx + nx); p->y[2] = (short)(by + ny);
            p->x[3] = (short)(bx + tx); p->y[3] = (short)(by + ty);
            p->x[4] = (short)(bx - nx); p->y[4] = (short)(by - ny);
            p->x[5] = (short)(ax - nx); p->y[5] = (short)(ay - ny);
            poly_rows(p);
        }
    }
}

int lz_slab_setup(const LzFat LZ_FAR *f, int shape, const LzStyle *st, LzPoly LZ_FAR *poly, LzLine LZ_FAR *lines,
                  int *nlines)
{
    if (f->r > 24) {                    /* wider than 3 px: the outline, and the middle half as wide */
        slab_of(f, shape, f->r, st->body, &poly[0]);
        slab_of(f, shape, f->r / 2, st->core, &poly[1]);
        *nlines = 0;
        return 2;
    }
    if (shape == LZ_SHAPE_ROUND) {
        /* narrower: the outline and a white core line, as the capsules have under 2 px (end on, a red dot round a
         * white pixel; three lines side by side would be a dash) */
        slab_of(f, shape, f->r, st->body, &poly[0]);
        lz_line_of(f->ax, f->ay, f->bx, f->by, &lines[0]);
        *nlines = 1;
        return 1;
    }
    {
        /* three lines a pixel apart across the bolt: red, white (the middle, lines[1]), red */
        long dx = (long)f->bx - f->ax, dy = (long)f->by - f->ay;
        int ox = 0, oy = 0;
        if ((dy < 0 ? -dy : dy) >= (dx < 0 ? -dx : dx))
            ox = 16;
        else
            oy = 16;
        lz_line_of(f->ax, f->ay, f->bx, f->by, &lines[1]);
        lz_line_of(f->ax - ox, f->ay - oy, f->bx - ox, f->by - oy, &lines[0]);
        lz_line_of(f->ax + ox, f->ay + oy, f->bx + ox, f->by + oy, &lines[2]);
        *nlines = 3;
        return 0;
    }
}

/* a chain of edges from vertex *idx going way dir: on to the edge that holds the row centre yc (its lower
 * end below yc), its x there (16.16 px) and per row; 0 when the chain has turned back up (past the bottom) */
static int chain_at(const LzPoly LZ_FAR *p, int *idx, int dir, int yc, long *x, long *m, int *yend)
{
    for (;;) {
        int i = *idx, j = i + dir;
        if (j == p->n) j = 0;
        if (j < 0) j = p->n - 1;
        if (p->y[j] < p->y[i])
            return 0;
        if (p->y[j] > yc) {
            long dx = (long)p->x[j] - p->x[i], dy = (long)p->y[j] - p->y[i];
            *m = (dx << 16) / dy;
            *x = ((long)p->x[i] << 12) + muldiv(((long)yc - p->y[i]) * dx, 4096, dy);
            *yend = p->y[j];
            return 1;
        }
        *idx = j;
    }
}

void lz_poly_draw(const LzPoly LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0, int ext_rows)
{
    int ys = p->top > y0 ? p->top : y0, ye = p->bot < y1 ? p->bot : y1, y, yc, t = 0, i, ia, ib, ea, eb;
    long xa, xb, ma, mb;
    if (!clip_rows(&ys, &ye, ext_y0, ext_rows, ext))
        return;
    for (i = 1; i < p->n; i++)
        if (p->y[i] < p->y[t])
            t = i;
    ia = ib = t;
    yc = ys * 16 + 8;
    if (!chain_at(p, &ia, 1, yc, &xa, &ma, &ea) || !chain_at(p, &ib, -1, yc, &xb, &mb, &eb))
        return;
    for (y = ys; y < ye; y++, yc += 16) {
        long L = xa < xb ? xa : xb, R = xa < xb ? xb : xa;
        int xs = (int)((L + 0x7FFF) >> 16), xe = (int)((R + 0x8000) >> 16);
        if (xs < xe)
            span(band + (unsigned)(y - band_y0) * 640, xs, xe, y, (unsigned char)p->colour, ext, ext_y0);
        xa += ma;
        xb += mb;
        if (yc + 16 >= ea && !chain_at(p, &ia, 1, yc + 16, &xa, &ma, &ea))
            break;
        if (yc + 16 >= eb && !chain_at(p, &ib, -1, yc + 16, &xb, &mb, &eb))
            break;
    }
}
