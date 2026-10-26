/* lightfield.c: the view, place and turn of a ship's sprite, and the pixel
 * kernels that scale and rotate one (lightfield.h).  No DOS here: the host
 * test (tests/host) compiles this file as it is. */
#include "lightfield.h"
#define RV_TABLES
#include "rvdata.h"

/* --- trigonometry --- */

int32_t lf_sin(int16_t a)
{
    a &= 1023;
    if (a < 256) return rv_qsin[a];
    if (a < 512) return rv_qsin[512 - a];
    if (a < 768) return -rv_qsin[a - 512];
    return -rv_qsin[1024 - a];
}

int32_t lf_cos(int16_t a) { return lf_sin(a + 256); }

uint32_t lf_sqrt(uint32_t v)
{
    uint32_t r = 0, bit = UINT32_C(1) << 30;
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
int16_t lf_atan2(int32_t y, int32_t x)
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, t;
    int16_t a, lo = 0, hi = 256;
    if (!ax && !ay)
        return 0;
    t = (ay < ax ? ay : ax) * INT32_C(16384) / (ay < ax ? ax : ay);   /* tan of the angle from the nearer axis */
    while (hi - lo > 1) {
        int16_t mid = (lo + hi) / 2;
        if ((int32_t)rv_qsin[mid] <= (t * rv_qsin[256 - mid]) >> 14)
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

int16_t lf_view_of(int32_t vx, int32_t vy, int32_t vz, int16_t nb, int16_t nh)
{
    int32_t r;
    int16_t elev, head, band, h, full = 2 * (nh - 1);
    while (vx > 16000 || vx < -16000 || vy > 16000 || vy < -16000 || vz > 16000 || vz < -16000) {
        vx /= 4;                        /* the squares below must fit: the angles do not change */
        vy /= 4;
        vz /= 4;
    }
    r = (int32_t)lf_sqrt((uint32_t)(vx * vx + vz * vz));
    elev = lf_atan2(vy, r);             /* 256 = straight above */
    head = lf_atan2(vx, vz);            /* 0 nose toward the viewer, 256 = the ship's left (the renderer's +x) */
    if (elev > 512) elev -= 1024;       /* -512..512 */
    /* the band: the nearest of the nb + 2 rows top .. bottom, each 1024 / (2 (nb + 1)) apart */
    band = (int16_t)((INT32_C(2) * (256 - elev) * (nb + 1) + 512) / 1024);
    if (band <= 0) return 0;            /* the top */
    if (band >= nb + 1) return 1 + nb * nh;
    band--;
    h = (int16_t)(((int32_t)head * (nh - 1) * 2 + 512) / 1024) % full;   /* the nearest heading step */
    if (h <= nh - 1)
        return 1 + band * nh + h;
    return 2 + nb * nh + band * (nh - 2) + (h - nh);               /* 180 + step .. 360 - step */
}

int16_t lf_pair_of(int16_t v, int16_t nb, int16_t nh)
{
    int16_t band, j, k, base = 2 + nb * nh;
    if (v == 0 || v == 1 + nb * nh)
        return v;
    if (v < base) {
        band = (v - 1) / nh;
        j = (v - 1) % nh;
        if (j == 0 || j == nh - 1)
            return v;                   /* nose on, tail on: their own mirror */
        return base + band * (nh - 2) + (nh - 2 - j);
    }
    band = (v - base) / (nh - 2);
    k = (v - base) % (nh - 2);
    return 1 + band * nh + (nh - 2 - k);
}

/* --- where --- */

static int32_t dot(const LfVec *a, const LfVec *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

void lf_view_rot(int16_t v, int16_t nb, int16_t nh, LfViewRot *r)
{
    int16_t band, e, h, base = 2 + nb * nh;
    int32_t se, ce, sh, ch;
    if (v == 0 || v == 1 + nb * nh) {  /* the top and the bottom: heading 90 (shipviews.lattice) */
        e = v ? -256 : 256;
        h = 256;
    } else {
        if (v < base) {
            band = (v - 1) / nh;
            h = (v - 1) % nh * 512 / (nh - 1);
        } else {
            band = (v - base) / (nh - 2);
            h = 512 + ((v - base) % (nh - 2) + 1) * 512 / (nh - 1);
        }
        e = 256 - (band + 1) * 512 / (nb + 1);
    }
    se = lf_sin(e); ce = lf_cos(e); sh = lf_sin(h); ch = lf_cos(h);
    /* shipviews.view_matrix: the viewer's way d = (sin h cos e, sin e, cos h cos e), the view's x = (cos h, 0,
     * -sin h), its y = (-sin h sin e, cos e, -cos h sin e), its z = d (the poles' up as the tool picks it gives
     * the same) */
    r->m[0] = ch; r->m[1] = 0; r->m[2] = -sh;
    r->m[3] = -(sh * se >> 14); r->m[4] = ce; r->m[5] = -(ch * se >> 14);
    r->m[6] = sh * ce >> 14; r->m[7] = se; r->m[8] = ch * ce >> 14;
}

void lf_sprite_point(const LfViewRot *r, int16_t roll, int32_t x, int32_t y, int32_t z, int32_t *sx, int32_t *sy,
                     int32_t *sz)
{
    int32_t qx = (r->m[0] * x + r->m[1] * y + r->m[2] * z) >> 14;
    int32_t qy = (r->m[3] * x + r->m[4] * y + r->m[5] * z) >> 14;
    int32_t c = lf_cos(roll), s = lf_sin(roll);
    *sz = (r->m[6] * x + r->m[7] * y + r->m[8] * z) >> 14;
    /* the picture's x right, y down (-qy), then turned clockwise on the screen */
    *sx = (qx * c + qy * s) >> 14;
    *sy = (qx * s - qy * c) >> 14;
}

void lf_place(const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int16_t cx, int16_t cy, int16_t focal,
              int16_t nb, int16_t nh, LfPlace *out)
{
    LfVec d, back;
    int32_t x, y, z, ux, uy;
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
    out->sx = cx + (int16_t)(x * focal / z);
    out->sy = cy - (int16_t)(y * focal / z);     /* the camera's up is up the screen */
    if (out->sx < -320 || out->sx > 960 || out->sy < -320 || out->sy > 720)
        return;
    out->on_screen = 1;
    /* the viewer's direction in the ship's own frame.  The renderer's +x is
     * the ship's left (a right-handed mesh with the nose on +z), the game's
     * right vector its right: hence -right. */
    back.x = -d.x; back.y = -d.y; back.z = -d.z;
    out->view = lf_view_of(-dot(&back, &ship->right) / 256, dot(&back, &ship->up) / 256, dot(&back, &ship->front) / 256,
                           nb, nh);
    /* the turn on screen, clockwise: a band view is drawn with the ship's up
     * toward the top of the screen; the top and bottom views lie nose left.
     * On the screen (y down) the ship's up shows as (ux, -uy). */
    if (out->view == 0 || out->view == 1 + nb * nh) {
        ux = dot(&ship->front, &cam->right);
        uy = dot(&ship->front, &cam->up);
        out->roll = lf_atan2(uy, -ux);
    } else {
        ux = dot(&ship->up, &cam->right);
        uy = dot(&ship->up, &cam->up);
        out->roll = lf_atan2(ux, uy);
    }
}

void lf_sky(const LfVec *dir, const LfVec *up, const LfBasis *cam, int16_t cx, int16_t cy, int16_t focal, LfPlace *out)
{
    int32_t x = dot(dir, &cam->right) / 256, y = dot(dir, &cam->up) / 256, z = dot(dir, &cam->front) / 256;
    out->depth = INT32_C(0x7FFFFFFF);
    out->on_screen = 0;
    out->sx = out->sy = out->view = out->roll = 0;
    if (z < 2)
        return;
    out->sx = cx + (int16_t)(x * focal / z);
    out->sy = cy - (int16_t)(y * focal / z);
    if (out->sx < -320 || out->sx > 960 || out->sy < -320 || out->sy > 720)
        return;
    out->on_screen = 1;
    out->roll = lf_atan2(dot(up, &cam->right), dot(up, &cam->up));   /* as a band view's up is turned (lf_place) */
}

/* --- the kernels --- */

void lf_colmap(int16_t src_w, int16_t dst_w, uint16_t LF_FAR *map)
{
    int16_t x;
    for (x = 0; x < dst_w; x++)
        map[x] = (uint16_t)((int32_t)x * src_w / dst_w);
}

void lf_scale_row(const uint8_t LF_FAR *line, const uint16_t LF_FAR *map, int16_t xa, int16_t xb,
                  uint8_t LF_FAR *out)
{
    int16_t x;
    for (x = xa; x < xb; x++) {
        uint8_t p = line[map[x]];
        if (p)
            out[x] = p;
    }
}

void lf_rotate_setup(int16_t w, int16_t h, int16_t cx, int16_t cy, int16_t roll, int16_t scale256, LfRot *r)
{
    /* output = R (source - origin) * scale, R clockwise on a y-down screen:
     * [c -s; s c]; the inverse takes an output offset back: [c s; -s c] / scale */
    int32_t c = lf_cos(roll), s = lf_sin(roll);        /* 16384 = 1.0 */
    int32_t xs[4], ys[4], minx = 0, maxx = 0, miny = 0, maxy = 0;
    int16_t i;
    xs[0] = -cx; ys[0] = -cy;
    xs[1] = w - cx; ys[1] = -cy;
    xs[2] = -cx; ys[2] = h - cy;
    xs[3] = w - cx; ys[3] = h - cy;
    for (i = 0; i < 4; i++) {
        int32_t ox = ((xs[i] * c - ys[i] * s) >> 14) * scale256 / 256;
        int32_t oy = ((xs[i] * s + ys[i] * c) >> 14) * scale256 / 256;
        if (ox < minx) minx = ox;
        if (ox > maxx) maxx = ox;
        if (oy < miny) miny = oy;
        if (oy > maxy) maxy = oy;
    }
    r->ocx = (int16_t)-minx + 1;
    r->ocy = (int16_t)-miny + 1;
    r->ow = (int16_t)(maxx - minx) + 2;
    r->oh = (int16_t)(maxy - miny) + 2;
    /* 16.16 steps: a column step moves (c, -s) / scale in the source */
    r->du = (c * 4) * 256 / scale256;
    r->dv = (-s * 4) * 256 / scale256;
    r->dru = (s * 4) * 256 / scale256;
    r->drv = (c * 4) * 256 / scale256;
    /* both origins are pixel centres: output pixel (x, y) is (x - ocx, y - ocy)
     * from the origin, and lands on the source pixel under (cx + 0.5, cy + 0.5)
     * plus the turned offset */
    r->u00 = ((int32_t)cx << 16) + INT32_C(32768) + (-(int32_t)r->ocx) * r->du + (-(int32_t)r->ocy) * r->dru;
    r->v00 = ((int32_t)cy << 16) + INT32_C(32768) + (-(int32_t)r->ocx) * r->dv + (-(int32_t)r->ocy) * r->drv;
}

void lf_rotate_row(const uint8_t LF_FAR *dense, int16_t w, int16_t h, const LfRot *r, int16_t y, int16_t xa, int16_t xb,
                   uint8_t LF_FAR *out)
{
    int32_t u = r->u00 + (int32_t)y * r->dru + (int32_t)xa * r->du;
    int32_t v = r->v00 + (int32_t)y * r->drv + (int32_t)xa * r->dv;
    int16_t x;
    for (x = xa; x < xb; x++, u += r->du, v += r->dv) {
        int32_t su = u >> 16, sv = v >> 16;
        uint8_t p;
        if (su < 0 || sv < 0 || su >= w || sv >= h)
            continue;
        p = dense[(uint16_t)sv * w + (uint16_t)su];
        if (p)
            out[x] = p;
    }
}

/* x such that lo <= a + x * d < hi, as [xs, xe) (16.16 a, d; lo, hi whole) */
static int16_t span_of(int32_t a, int32_t d, int32_t lo, int32_t hi, int16_t *xs, int16_t *xe)
{
    int32_t s, e;
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
    if (s > *xs) *xs = (int16_t)s;
    if (e < *xe) *xe = (int16_t)e;
    return *xs < *xe;
}

int16_t lf_rotate_span(const LfRot *r, int16_t w, int16_t h, int16_t y, int16_t xa, int16_t xb, int16_t *xs,
                       int16_t *xe)
{
    int32_t u = r->u00 + (int32_t)y * r->dru, v = r->v00 + (int32_t)y * r->drv;
    *xs = xa;
    *xe = xb;
    if (!span_of(u, r->du, 0, (int32_t)w << 16, xs, xe))
        return 0;
    if (!span_of(v, r->dv, 0, (int32_t)h << 16, xs, xe))
        return 0;
    return *xs < *xe;
}
