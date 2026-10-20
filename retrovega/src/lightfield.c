/* lightfield.c: the view, place and turn of a ship's sprite, and the pixel
 * kernels that scale and rotate one (lightfield.h).  No DOS here: the host
 * test (tests/host) compiles this file as it is. */
#include "lightfield.h"
#define RV_TABLES
#include "rvdata.h"

/* --- trigonometry --- */

long lf_sin(int a)
{
    a &= 1023;
    if (a < 256) return rv_qsin[a];
    if (a < 512) return rv_qsin[512 - a];
    if (a < 768) return -rv_qsin[a - 512];
    return -rv_qsin[1024 - a];
}

long lf_cos(int a) { return lf_sin(a + 256); }

unsigned long lf_sqrt(unsigned long v)
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

/* atan2 in 1024ths of a turn, by octant and a search of the quarter table */
int lf_atan2(long y, long x)
{
    long ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, t;
    int a, lo = 0, hi = 256;
    if (!ax && !ay)
        return 0;
    t = (ay < ax ? ay : ax) * 16384L / (ay < ax ? ax : ay);   /* tan of the angle from the nearer axis */
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
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

/* --- which view --- */

int lf_view_of(long vx, long vy, long vz)
{
    long r;
    int elev, head, band, h;
    while (vx > 16000 || vx < -16000 || vy > 16000 || vy < -16000 || vz > 16000 || vz < -16000) {
        vx /= 4;                        /* the squares below must fit: the angles do not change */
        vy /= 4;
        vz /= 4;
    }
    r = (long)lf_sqrt((unsigned long)(vx * vx + vz * vz));
    elev = lf_atan2(vy, r);             /* 256 = straight above */
    head = lf_atan2(vx, vz);            /* 0 nose toward the viewer, 256 = the ship's left (the renderer's +x) */
    if (elev > 512) elev -= 1024;       /* -512..512 */
    if (elev > 213) return 0;           /* above 75 degrees: the top */
    if (elev < -213) return 36;
    band = elev > 128 ? 0 : elev > 43 ? 1 : elev >= -43 ? 2 : elev >= -128 ? 3 : 4;   /* 60, 30, 0, -30, -60 */
    h = ((head + 43) / 85) % 12;        /* 30 degree steps: 0 .. 11 */
    if (h <= 6)
        return 1 + band * 7 + h;
    return 37 + band * 5 + (h - 7);     /* 210, 240, 270, 300, 330 */
}

/* --- where --- */

static long dot(const LfVec *a, const LfVec *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

void lf_place(const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int cx, int cy, int focal, LfPlace *out)
{
    LfVec d, back;
    long x, y, z, ux, uy;
    d.x = rel->x / 16;                  /* 16 units = 1/16 km: keeps the products in a long */
    d.y = rel->y / 16;
    d.z = rel->z / 16;
    x = dot(&d, &cam->right) / 256;
    y = dot(&d, &cam->up) / 256;
    z = dot(&d, &cam->front) / 256;
    out->depth = z * 16;
    out->on_screen = 0;
    out->sx = out->sy = 0;
    out->view = 0;
    out->roll = 0;
    if (z < 2)
        return;
    out->sx = cx + (int)(x * focal / z);
    out->sy = cy - (int)(y * focal / z);     /* the camera's up is up the screen */
    if (out->sx < -320 || out->sx > 960 || out->sy < -320 || out->sy > 720)
        return;
    out->on_screen = 1;
    /* the viewer's direction in the ship's own frame.  The renderer's +x is
     * the ship's left (a right-handed mesh with the nose on +z), the game's
     * right vector its right: hence -right. */
    back.x = -d.x; back.y = -d.y; back.z = -d.z;
    out->view = lf_view_of(-dot(&back, &ship->right) / 256, dot(&back, &ship->up) / 256, dot(&back, &ship->front) / 256);
    /* the turn on screen, clockwise: a band view is drawn with the ship's up
     * toward the top of the screen; the top and bottom views lie nose left.
     * On the screen (y down) the ship's up shows as (ux, -uy). */
    if (out->view == 0 || out->view == 36) {
        ux = dot(&ship->front, &cam->right);
        uy = dot(&ship->front, &cam->up);
        out->roll = lf_atan2(uy, -ux);
    } else {
        ux = dot(&ship->up, &cam->right);
        uy = dot(&ship->up, &cam->up);
        out->roll = lf_atan2(ux, uy);
    }
}

/* --- the kernels --- */

void lf_colmap(int src_w, int dst_w, unsigned short LF_FAR *map)
{
    int x;
    for (x = 0; x < dst_w; x++)
        map[x] = (unsigned short)((long)x * src_w / dst_w);
}

void lf_scale_row(const unsigned char LF_FAR *line, const unsigned short LF_FAR *map, int xa, int xb,
                  unsigned char LF_FAR *out)
{
    int x;
    for (x = xa; x < xb; x++) {
        unsigned char p = line[map[x]];
        if (p)
            out[x] = p;
    }
}

void lf_rotate_setup(int w, int h, int cx, int cy, int roll, int scale256, LfRot *r)
{
    /* output = R (source - origin) * scale, R clockwise on a y-down screen:
     * [c -s; s c]; the inverse takes an output offset back: [c s; -s c] / scale */
    long c = lf_cos(roll), s = lf_sin(roll);        /* 16384 = 1.0 */
    long xs[4], ys[4], minx = 0, maxx = 0, miny = 0, maxy = 0;
    int i;
    xs[0] = -cx; ys[0] = -cy;
    xs[1] = w - cx; ys[1] = -cy;
    xs[2] = -cx; ys[2] = h - cy;
    xs[3] = w - cx; ys[3] = h - cy;
    for (i = 0; i < 4; i++) {
        long ox = ((xs[i] * c - ys[i] * s) >> 14) * scale256 / 256;
        long oy = ((xs[i] * s + ys[i] * c) >> 14) * scale256 / 256;
        if (ox < minx) minx = ox;
        if (ox > maxx) maxx = ox;
        if (oy < miny) miny = oy;
        if (oy > maxy) maxy = oy;
    }
    r->ocx = (int)-minx + 1;
    r->ocy = (int)-miny + 1;
    r->ow = (int)(maxx - minx) + 2;
    r->oh = (int)(maxy - miny) + 2;
    /* 16.16 steps: a column step moves (c, -s) / scale in the source */
    r->du = (c * 4) * 256 / scale256;
    r->dv = (-s * 4) * 256 / scale256;
    r->dru = (s * 4) * 256 / scale256;
    r->drv = (c * 4) * 256 / scale256;
    /* both origins are pixel centres: output pixel (x, y) is (x - ocx, y - ocy)
     * from the origin, and lands on the source pixel under (cx + 0.5, cy + 0.5)
     * plus the turned offset */
    r->u00 = ((long)cx << 16) + 32768L + (-(long)r->ocx) * r->du + (-(long)r->ocy) * r->dru;
    r->v00 = ((long)cy << 16) + 32768L + (-(long)r->ocx) * r->dv + (-(long)r->ocy) * r->drv;
}

void lf_rotate_row(const unsigned char LF_FAR *dense, int w, int h, const LfRot *r, int y, int xa, int xb,
                   unsigned char LF_FAR *out)
{
    long u = r->u00 + (long)y * r->dru + (long)xa * r->du;
    long v = r->v00 + (long)y * r->drv + (long)xa * r->dv;
    int x;
    for (x = xa; x < xb; x++, u += r->du, v += r->dv) {
        long su = u >> 16, sv = v >> 16;
        unsigned char p;
        if (su < 0 || sv < 0 || su >= w || sv >= h)
            continue;
        p = dense[(unsigned)sv * w + (unsigned)su];
        if (p)
            out[x] = p;
    }
}

/* x such that lo <= a + x * d < hi, as [xs, xe) (16.16 a, d; lo, hi whole) */
static int span_of(long a, long d, long lo, long hi, int *xs, int *xe)
{
    long s, e;
    if (d == 0) {
        if (a < lo || a >= hi)
            return 0;
        return 1;                       /* every x: the caller's range stands */
    }
    if (d > 0) {
        s = -((a - lo) / d);            /* a + x d >= lo  ->  x >= (lo - a) / d */
        while (a + s * d < lo) s++;
        while (s > -32000 && a + (s - 1) * d >= lo) s--;
        e = (hi - a) / d;               /* a + x d < hi   ->  x < (hi - a) / d */
        while (a + e * d >= hi) e--;
        while (e < 32000 && a + (e + 1) * d < hi) e++;
        e++;
    } else {
        s = (a - hi) / -d;              /* a + x d < hi   ->  x > (a - hi) / -d */
        while (a + s * d >= hi) s++;
        while (s > -32000 && a + (s - 1) * d < hi) s--;
        e = (a - lo) / -d;              /* a + x d >= lo  ->  x <= (a - lo) / -d */
        while (a + e * d < lo) e--;
        while (e < 32000 && a + (e + 1) * d >= lo) e++;
        e++;
    }
    if (s > *xs) *xs = (int)s;
    if (e < *xe) *xe = (int)e;
    return *xs < *xe;
}

int lf_rotate_span(const LfRot *r, int w, int h, int y, int xa, int xb, int *xs, int *xe)
{
    long u = r->u00 + (long)y * r->dru, v = r->v00 + (long)y * r->drv;
    *xs = xa;
    *xe = xb;
    if (!span_of(u, r->du, 0, (long)w << 16, xs, xe))
        return 0;
    if (!span_of(v, r->dv, 0, (long)h << 16, xs, xe))
        return 0;
    return *xs < *xe;
}
