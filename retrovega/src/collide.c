/* collide.c: ship-to-ship collisions (collide.h).
 *
 * A ship's frame here is its orientation's right, up and front (256 = 1.0);
 * RvShipType's points are in its sprites' frame (127 = its half box, +x the
 * ship's left, +y up, +z the nose), so a point (x, y, z) is (-x, y, z) x
 * half / 127 along right, up, front.  A vital of ship P is taken into ship
 * B's frame by B's axes dotted with P's (a 3 x 3 product, once a pair) and
 * the two middles' difference in B's frame: a point is in B's box when it is
 * within the box's half extent on all three axes; the core, a sphere, when
 * it is within its radius of the box (the box grown by the radius with its
 * edges and corners rounded: the user's "squircle"), its push then straight
 * out from the box's nearest point.  All in long integers: after the
 * coarse test the ships are within their half boxes' sum, a few million
 * positions at most, so the products keep inside 2^31. */
#include <stdlib.h>
#include <string.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "collide.h"

#define HALF_PER_PX 800L                /* positions of half box per sprite pixel across (lod0_px): 2500/400 km / 2 x 256 */

typedef struct {
    const RvShipType __far *t;
    LfVec c;                            /* its middle (positions) */
    long ax[3][3];                      /* its right, up, front (256 = 1.0) */
    long h[3];                          /* its box's half extents along them (positions) */
    long H;                             /* its half box */
} Body;

static int is_ship(int s)
{
    return s >= 0 && s <= RV_MAX_SHIP_SLOT && (maybeEntityType[s] == 0xC || maybeEntityType[s] == 0xD);
}

static int body(int s, Body *o)
{
    int i;
    if (!is_ship(s) || (o->t = ship_type(gInstanceShipTypes[s])) == 0)
        return 0;
    o->c = *(const LfVec *)&gPositionVector[s];
    o->ax[0][0] = gOrientationRightVector[s].x; o->ax[0][1] = gOrientationRightVector[s].y;
    o->ax[0][2] = gOrientationRightVector[s].z;
    o->ax[1][0] = gOrientationUpVector[s].x; o->ax[1][1] = gOrientationUpVector[s].y; o->ax[1][2] = gOrientationUpVector[s].z;
    o->ax[2][0] = gOrientationFrontVector[s].x; o->ax[2][1] = gOrientationFrontVector[s].y;
    o->ax[2][2] = gOrientationFrontVector[s].z;
    o->H = (long)o->t->lod0_px * HALF_PER_PX;
    for (i = 0; i < 3; i++)
        o->h[i] = o->H * o->t->hit[i] / 127;
    return 1;
}

long collide_half(int slot)
{
    const RvShipType __far *t;
    if (!is_ship(slot) || (t = ship_type(gInstanceShipTypes[slot])) == 0)
        return 0;
    return (long)t->lod0_px * HALF_PER_PX;
}

int collide_coarse(int a, int b)
{
    long ha = collide_half(a), hb = collide_half(b), R = ha + hb, dx, dy, dz;
    if (a == b || !ha || !hb)
        return 0;
    dx = gPositionVector[b].x - gPositionVector[a].x;
    dy = gPositionVector[b].y - gPositionVector[a].y;
    dz = gPositionVector[b].z - gPositionVector[a].z;
    if (labs(dx) >= R || labs(dy) >= R || labs(dz) >= R)
        return 0;
    dx >>= 6;
    dy >>= 6;
    dz >>= 6;
    R >>= 6;
    return dx * dx + dy * dy + dz * dz < R * R;
}

/* vital k of ship P (0 its core, then its engines, then its other vitals) along its right, up, front (positions),
 * and its radius; 0 past the last */
static int vital(const Body *P, int k, long q[3], long *r)
{
    const signed char __far *v;
    int ne = P->t->nengines;
    if (k == 0) {                       /* the core: the biggest sphere about its middle inside its sprite at every
                                           view (the user's definition; 0 for a ship whose middle shows space) */
        q[0] = q[1] = q[2] = 0;
        *r = P->H * P->t->core / 127;
        return 1;
    }
    if (k <= ne)
        v = P->t->engine[k - 1];
    else if (k <= ne + P->t->nvitals)
        v = P->t->vital[k - 1 - ne];
    else
        return 0;
    q[0] = -(long)v[0] * P->H / 127;    /* (its sprites' +x is the ship's left) */
    q[1] = (long)v[1] * P->H / 127;
    q[2] = (long)v[2] * P->H / 127;
    *r = 0;
    return 1;
}

/* P's vitals in B's box: the deepest one's depth (0: none in it), and the least move of P that frees it (world,
 * positions): along B's axis it is shallowest on, out the nearer face */
static long vitals_in(const Body *B, const Body *P, LfVec *move)
{
    long t[3], M[3][3], best = 0, q[3], r;
    int i, j, k;
    for (i = 0; i < 3; i++) {
        t[i] = ((P->c.x - B->c.x) * B->ax[i][0] + (P->c.y - B->c.y) * B->ax[i][1] + (P->c.z - B->c.z) * B->ax[i][2])
               / 256;
        for (j = 0; j < 3; j++)
            M[i][j] = (B->ax[i][0] * P->ax[j][0] + B->ax[i][1] * P->ax[j][1] + B->ax[i][2] * P->ax[j][2]) / 256;
    }
    for (k = 0; vital(P, k, q, &r); k++) {
        long l[3], o[3], out2 = 0, mv[3], d;
        int a;
        for (i = 0; i < 3; i++) {
            l[i] = t[i] + (M[i][0] * q[0] + M[i][1] * q[1] + M[i][2] * q[2]) / 256;
            o[i] = labs(l[i]) - B->h[i];   /* how far outside the box on this axis (negative: inside) */
            if (o[i] >= r)
                break;
            if (o[i] > 0)
                out2 += (o[i] >> 8) * (o[i] >> 8);   /* (in km: squares of positions overflow) */
        }
        if (i < 3)
            continue;
        if (out2) {                     /* the core, outside the box but within its radius of it: off the corner */
            long dist = (long)lf_sqrt((unsigned long)out2), rk = r >> 8;
            if (dist >= rk)
                continue;
            d = (rk - dist) << 8;
            for (i = 0; i < 3; i++)
                mv[i] = o[i] > 0 ? (l[i] < 0 ? -1 : 1) * (o[i] >> 8) * (rk - dist) * 256 / (dist ? dist : 1) : 0;
        } else {                        /* in the box: out the nearer face of the axis it is shallowest on */
            long dd[3];
            for (i = 0; i < 3; i++)
                dd[i] = r - o[i];
            a = dd[0] < dd[1] ? (dd[0] < dd[2] ? 0 : 2) : (dd[1] < dd[2] ? 1 : 2);
            d = dd[a];
            mv[0] = mv[1] = mv[2] = 0;
            mv[a] = l[a] < 0 ? -d : d;
        }
        if (d > best) {
            best = d;
            move->x = (mv[0] * B->ax[0][0] + mv[1] * B->ax[1][0] + mv[2] * B->ax[2][0]) / 256;
            move->y = (mv[0] * B->ax[0][1] + mv[1] * B->ax[1][1] + mv[2] * B->ax[2][1]) / 256;
            move->z = (mv[0] * B->ax[0][2] + mv[1] * B->ax[1][2] + mv[2] * B->ax[2][2]) / 256;
        }
    }
    return best;
}

int collide_ships(int a, int b, LfVec *push, long *depth)
{
    Body A, B;
    LfVec m;
    long d;
    unsigned long va, vb;
    push->x = push->y = push->z = 0;
    if (depth)
        *depth = 0;
    if (!collide_coarse(a, b) || !body(a, &A) || !body(b, &B))
        return 0;
    va = (unsigned long)(A.h[0] >> 10) * (A.h[1] >> 10) * (A.h[2] >> 10);
    vb = (unsigned long)(B.h[0] >> 10) * (B.h[1] >> 10) * (B.h[2] >> 10);
    /* the smaller's box against the bigger's vitals first; then the bigger's box against the smaller's */
    if (va <= vb) {
        if ((d = vitals_in(&A, &B, &m)) != 0) {
            *push = m;                  /* (b's vital in a's box: b moves out) */
        } else if ((d = vitals_in(&B, &A, &m)) != 0) {
            push->x = -m.x;             /* (a's in b's: a moves out, so b by the opposite) */
            push->y = -m.y;
            push->z = -m.z;
        }
    } else {
        if ((d = vitals_in(&B, &A, &m)) != 0) {
            push->x = -m.x;
            push->y = -m.y;
            push->z = -m.z;
        } else if ((d = vitals_in(&A, &B, &m)) != 0) {
            *push = m;
        }
    }
    if (depth)
        *depth = d;
    return d != 0;
}

int collide_shape(int slot, LfVec corner[8], LfVec *v, int maxv)
{
    Body P;
    long q[3], r;
    int k, n = 0, i;
    if (!body(slot, &P))
        return 0;
    for (k = 0; k < 8; k++) {
        long sa = k & 1 ? P.h[0] : -P.h[0], su = k & 2 ? P.h[1] : -P.h[1], sf = k & 4 ? P.h[2] : -P.h[2];
        corner[k].x = P.c.x + (sa * P.ax[0][0] + su * P.ax[1][0] + sf * P.ax[2][0]) / 256;
        corner[k].y = P.c.y + (sa * P.ax[0][1] + su * P.ax[1][1] + sf * P.ax[2][1]) / 256;
        corner[k].z = P.c.z + (sa * P.ax[0][2] + su * P.ax[1][2] + sf * P.ax[2][2]) / 256;
    }
    for (k = 0; n < maxv && vital(&P, k, q, &r); k++, n++) {
        long w[3];
        for (i = 0; i < 3; i++)
            w[i] = (q[0] * P.ax[0][i] + q[1] * P.ax[1][i] + q[2] * P.ax[2][i]) / 256;
        v[n].x = P.c.x + w[0];
        v[n].y = P.c.y + w[1];
        v[n].z = P.c.z + w[2];
    }
    return n;
}
