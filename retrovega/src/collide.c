/* collide.c: collisions with ships (collide.h).
 *
 * A ship's frame here is its orientation's right, up and front (256 = 1.0);
 * RvShipType's points are in its sprites' frame (127 = its half box, +x the
 * ship's left, +y up, +z the nose), so a point (x, y, z) is (-x, y, z) x
 * half / 127 along right, up, front.  A point is taken into ship B's frame by
 * B's axes dotted with its offset from B's middle (a vital of ship P: by B's
 * axes dotted with P's, a 3 x 3 product once a pair, and the middles'
 * difference).  In B's frame it is in B's hull when it is inside B's box and
 * its cell is solid in B's three silhouettes (SHAPES.DAT, tools/hitboxes.py:
 * 64 x 64 bit masks over the box from above, the side and the front: the
 * cell of v on an axis of half extent h is (v + h) / 2h x 64); a ship type
 * with no silhouettes is its box.  The least way out of the hull is the
 * shortest of twelve: along each of a silhouette's two axes, either way, to
 * its first clear cell (out of one silhouette is out of the hull).  A
 * segment is clipped to the box and walked in cell units (8 fraction bits),
 * at most half a cell an axis a step.  All in long integers (Watcom's long
 * long divisions cost thousands of cycles: the clipping scales its terms
 * down instead). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "collide.h"
#include "world.h"

#define SN 64                           /* a silhouette's cells across */
#define SROW 8                          /* bytes a row */
#define SMASK 512
#define SBYTES (3 * SMASK)
#define CACHE 8                         /* the types' silhouettes kept in memory at a time */
#define BIT(m, k, row, col) ((m)[(k) * SMASK + (row) * SROW + ((col) >> 3)] & (1 << ((col) & 7)))

typedef struct {
    const RvShipType __far *t;
    const uint8_t __far *m;             /* its silhouettes, or 0: its box */
    LfVec c;                            /* its middle (positions) */
    int32_t ax[3][3];                   /* its right, up, front (256 = 1.0) */
    int32_t h[3];                       /* its box's half extents along them (positions) */
    int32_t cs[3];                      /* a silhouette cell's size along them */
    int32_t H;                          /* its half box */
} Body;

static XmsBlock __far g_shape_blk;
static uint8_t __far g_shape_of[256];   /* a type's silhouettes in the block (0xFF none) */
static struct { uint8_t __far *m; int16_t type; uint16_t used; } __far g_cache[CACHE];
static uint16_t __far g_tick;
static Body __far g_body[RV_MAX_SHIP_SLOT + 1];   /* each slot's, as last taken (reused while it stands) */
static uint8_t __far g_body_ok[RV_MAX_SHIP_SLOT + 1];

int16_t collide_reserve(void)
{
    static const char __far s_magic[] = "RVSHAPE", __far s_none[] = "collide: no SHAPES.DAT",
        __far s_in[] = "collide: %d ships' silhouettes, %lu KB in XMS";
    FILE *f = fopen("SHAPES.DAT", "rb");
    uint8_t *buf = rv_filebuf;
    int16_t n, i;
    _fmemset(g_shape_of, 0xFF, sizeof g_shape_of);
    g_shape_blk = 0;
    if (!f) {
        rv_logf(s_none);
        return 0;
    }
    fread(buf, 1, 16, f);
    n = buf[10] | buf[11] << 8;
    if (_fmemcmp(buf, s_magic, 8) || (buf[8] | buf[9] << 8) != 1 || (buf[12] | buf[13] << 8) != SN || n < 1 || n > 255
        || (g_shape_blk = xms_alloc((uint32_t)n * SBYTES, XMS_RESIDENT)) == 0) {
        fclose(f);
        return 0;
    }
    for (i = 0; i < n; i++) {
        fread(buf, 1, 2, f);
        g_shape_of[buf[0]] = (uint8_t)i;
        fread(buf, 1, 1024, f);
        xms_put(g_shape_blk, (uint32_t)i * SBYTES, buf, 1024);
        fread(buf, 1, SBYTES - 1024, f);
        xms_put(g_shape_blk, (uint32_t)i * SBYTES + 1024, buf, SBYTES - 1024);
    }
    fclose(f);
    rv_logf(s_in, n, (uint32_t)n * SBYTES / 1024);
    return 1;
}

/* a type's silhouettes, from XMS into the cache the first time (the least recently used slot given up) */
static const uint8_t __far *shape(uint16_t type)
{
    int16_t i, lru = 0;
    if (!g_shape_blk || type > 255 || g_shape_of[type] == 0xFF)
        return 0;
    g_tick++;
    for (i = 0; i < CACHE; i++)
        if (g_cache[i].m && g_cache[i].type == (int16_t)type) {
            g_cache[i].used = g_tick;
            return g_cache[i].m;
        }
    for (i = 0; i < CACHE; i++) {
        if (!g_cache[i].m) {
            lru = i;
            break;
        }
        if (g_cache[i].used < g_cache[lru].used)
            lru = i;
    }
    if (!g_cache[lru].m && (g_cache[lru].m = (uint8_t __far *)_fmalloc(SBYTES)) == 0) {
        static const char __far s_nomem[] = "collide: no memory for type %u's silhouettes: its box stands in";
        rv_logf(s_nomem, type);
        return 0;
    }
    xms_get(g_shape_blk, (uint32_t)g_shape_of[type] * SBYTES, g_cache[lru].m, SBYTES);
    g_cache[lru].type = (int16_t)type;
    g_cache[lru].used = g_tick;
    return g_cache[lru].m;
}

void collide_flight_end(void)
{
    int16_t i;
    for (i = 0; i < CACHE; i++) {
        if (g_cache[i].m)
            _ffree(g_cache[i].m);
        g_cache[i].m = 0;
    }
    _fmemset(g_body_ok, 0, sizeof g_body_ok);
}

static int16_t is_ship(int16_t s)
{
    return s >= 0 && s <= RV_MAX_SHIP_SLOT && (maybeEntityType[s] == 0xC || maybeEntityType[s] == 0xD);
}

static int16_t body(int16_t s, Body *o)
{
    int16_t i;
    if (!is_ship(s))
        return 0;
    if (g_body_ok[s] && g_body[s].c.x == gPositionVector[s].x && g_body[s].c.y == gPositionVector[s].y
        && g_body[s].c.z == gPositionVector[s].z && g_body[s].ax[2][0] == gOrientationFrontVector[s].x
        && g_body[s].ax[2][1] == gOrientationFrontVector[s].y && g_body[s].ax[2][2] == gOrientationFrontVector[s].z
        && g_body[s].ax[1][0] == gOrientationUpVector[s].x && g_body[s].t == ship_type(gInstanceShipTypes[s])) {
        *o = g_body[s];                 /* (unmoved since: as it was) */
        return 1;
    }
    if ((o->t = ship_type(gInstanceShipTypes[s])) == 0)
        return 0;
    o->m = shape(gInstanceShipTypes[s]);
    o->c = *(const LfVec *)&gPositionVector[s];
    o->ax[0][0] = gOrientationRightVector[s].x; o->ax[0][1] = gOrientationRightVector[s].y;
    o->ax[0][2] = gOrientationRightVector[s].z;
    o->ax[1][0] = gOrientationUpVector[s].x; o->ax[1][1] = gOrientationUpVector[s].y; o->ax[1][2] = gOrientationUpVector[s].z;
    o->ax[2][0] = gOrientationFrontVector[s].x; o->ax[2][1] = gOrientationFrontVector[s].y;
    o->ax[2][2] = gOrientationFrontVector[s].z;
    o->H = (int32_t)o->t->radius_km * KM;   /* (its size: ships.csv's radius_km) */
    for (i = 0; i < 3; i++) {
        o->h[i] = o->H * o->t->hit[i] / 127;
        o->cs[i] = 2 * o->h[i] / SN;
        if (o->cs[i] < 1)
            o->cs[i] = 1;
    }
    g_body[s] = *o;
    g_body_ok[s] = 1;
    return 1;
}

int32_t collide_half(int16_t slot)
{
    const RvShipType __far *t;
    if (!is_ship(slot) || (t = ship_type(gInstanceShipTypes[slot])) == 0)
        return 0;
    return (int32_t)t->radius_km * KM;
}

int16_t collide_coarse(int16_t a, int16_t b)
{
    int32_t ha = collide_half(a), hb = collide_half(b), R = ha + hb, dx, dy, dz;
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

/* the cell of a coordinate on axis i (-1 outside the box) */
static int16_t cell(const Body *B, int16_t i, int32_t v)
{
    int32_t c;
    if (v <= -B->h[i] || v >= B->h[i])
        return -1;
    c = (v + B->h[i]) / B->cs[i];
    return c >= SN ? SN - 1 : (int16_t)c;
}

/* a point in B's frame (positions) in B's hull */
static int16_t in_hull(const Body *B, const int32_t l[3])
{
    int16_t x = cell(B, 0, l[0]), y = cell(B, 1, l[1]), z = cell(B, 2, l[2]);
    if (x < 0 || y < 0 || z < 0)
        return 0;
    return !B->m || (BIT(B->m, 0, z, x) && BIT(B->m, 1, y, z) && BIT(B->m, 2, y, x));
}

/* the least move (in B's frame, positions) that takes a point in B's hull out of it; its length back */
static int32_t way_out(const Body *B, const int32_t l[3], int32_t mv[3])
{
    static const uint8_t __far plane[3][2] = { { 0, 2 }, { 2, 1 }, { 0, 1 } };   /* a mask's column, row axes */
    int16_t c[3], k, j, dir, best_ax = 0;
    int32_t best = INT32_C(0x7FFFFFFF), d;
    for (k = 0; k < 3; k++)
        c[k] = cell(B, k, l[k]);
    mv[0] = mv[1] = mv[2] = 0;
    if (!B->m) {                        /* the box: out the nearer face of the axis it is shallowest on */
        for (k = 0; k < 3; k++)
            if ((d = B->h[k] - labs(l[k])) < best) {
                best = d;
                best_ax = k;
            }
        mv[best_ax] = l[best_ax] < 0 ? -(best + 1) : best + 1;
        return best + 1;
    }
    for (k = 0; k < 3; k++)
        for (j = 0; j < 2; j++)         /* along the mask's columns (j 0) and its rows (j 1) */
            for (dir = -1; dir <= 1; dir += 2) {
                int16_t a = plane[k][j], o = plane[k][1 - j], e = c[a];
                do
                    e += dir;
                while (e >= 0 && e < SN && (j == 0 ? BIT(B->m, k, c[o], e) : BIT(B->m, k, e, c[o])));
                /* to the first clear cell's near edge (or the box's) */
                d = dir > 0 ? -B->h[a] + e * B->cs[a] - l[a] : l[a] - (-B->h[a] + (e + 1) * B->cs[a]);
                if (d < 0)
                    d = 0;
                if (d < best) {
                    best = d;
                    best_ax = a;
                    mv[0] = mv[1] = mv[2] = 0;
                    mv[a] = dir * (d + 1);
                }
            }
    return best + 1;
}

/* the world vector of a move in B's frame */
static void to_world(const Body *B, const int32_t mv[3], LfVec *w)
{
    w->x = (mv[0] * B->ax[0][0] + mv[1] * B->ax[1][0] + mv[2] * B->ax[2][0]) / 256;
    w->y = (mv[0] * B->ax[0][1] + mv[1] * B->ax[1][1] + mv[2] * B->ax[2][1]) / 256;
    w->z = (mv[0] * B->ax[0][2] + mv[1] * B->ax[1][2] + mv[2] * B->ax[2][2]) / 256;
}

/* d x t / 1024 for t in 0 .. 1024 without overflow */
static int32_t part_of(int32_t d, int32_t t)
{
    return d / 1024 * t + d % 1024 * t / 1024;
}

/* a world point in B's frame (positions; the offset shifted down first: good to 32,768 km) */
static void to_local(const Body *B, const LfVec *p, int32_t l[3])
{
    int32_t dx = (p->x - B->c.x) >> 2, dy = (p->y - B->c.y) >> 2, dz = (p->z - B->c.z) >> 2;
    int16_t i;
    for (i = 0; i < 3; i++)
        l[i] = (dx * B->ax[i][0] + dy * B->ax[i][1] + dz * B->ax[i][2]) >> 6;
}

/* vital k of ship P (0 its core, then its engines, then its other vitals) along its right, up, front (positions),
 * and its radius; 0 past the last */
static int16_t vital(const Body *P, int16_t k, int32_t q[3], int32_t *r)
{
    const int8_t __far *v;
    int16_t ne = P->t->nengines;
    if (k == 0) {                       /* the core: the biggest sphere about its middle inside its sprite at every
                                           view, to 95% (the user's definition; 0 for a ship whose middle shows space) */
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
    q[0] = -(int32_t)v[0] * P->H / 127;   /* (its sprites' +x is the ship's left) */
    q[1] = (int32_t)v[1] * P->H / 127;
    q[2] = (int32_t)v[2] * P->H / 127;
    *r = 0;
    return 1;
}

/* P's vitals in B's hull: the deepest one's depth (0: none in it), and the least move of P that frees it (world).
 * A sphere (the core) is its middle and six points a radius off it along B's axes */
static int32_t vitals_in(const Body *B, const Body *P, LfVec *move)
{
    int32_t t[3], M[3][3], best = 0, q[3], r;
    int16_t i, j, k, s;
    for (i = 0; i < 3; i++) {
        t[i] = ((P->c.x - B->c.x) * B->ax[i][0] + (P->c.y - B->c.y) * B->ax[i][1] + (P->c.z - B->c.z) * B->ax[i][2])
               / 256;
        for (j = 0; j < 3; j++)
            M[i][j] = (B->ax[i][0] * P->ax[j][0] + B->ax[i][1] * P->ax[j][1] + B->ax[i][2] * P->ax[j][2]) / 256;
    }
    for (k = 0; vital(P, k, q, &r); k++) {
        int32_t l[3];
        for (i = 0; i < 3; i++)
            l[i] = t[i] + (M[i][0] * q[0] + M[i][1] * q[1] + M[i][2] * q[2]) / 256;
        for (s = 0; s < (r ? 7 : 1); s++) {
            int32_t p[3], mv[3], d;
            for (i = 0; i < 3; i++)
                p[i] = l[i] + (s && (s - 1) / 2 == i ? ((s & 1) ? r : -r) : 0);
            if (!in_hull(B, p))
                continue;
            if ((d = way_out(B, p, mv)) > best) {
                best = d;
                to_world(B, mv, move);
            }
        }
    }
    return best;
}

int16_t collide_ships(int16_t a, int16_t b, LfVec *push, int32_t *depth)
{
    Body A, B;
    LfVec m;
    int32_t d;
    uint32_t va, vb;
    push->x = push->y = push->z = 0;
    if (depth)
        *depth = 0;
    if (!collide_coarse(a, b) || !body(a, &A) || !body(b, &B))
        return 0;
    va = (uint32_t)(A.h[0] >> 10) * (A.h[1] >> 10) * (A.h[2] >> 10);
    vb = (uint32_t)(B.h[0] >> 10) * (B.h[1] >> 10) * (B.h[2] >> 10);
    /* the smaller's hull against the bigger's vitals first; then the bigger's hull against the smaller's */
    if (va <= vb) {
        if ((d = vitals_in(&A, &B, &m)) != 0) {
            *push = m;                  /* (b's vital in a: b moves out) */
        } else if ((d = vitals_in(&B, &A, &m)) != 0) {
            push->x = -m.x;             /* (a's in b: a moves out, so b by the opposite) */
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

int16_t collide_point(int16_t slot, const LfVec *p, LfVec *push)
{
    Body B;
    int32_t l[3], mv[3];
    if (!body(slot, &B))
        return 0;
    if (labs(p->x - B.c.x) >= B.H || labs(p->y - B.c.y) >= B.H || labs(p->z - B.c.z) >= B.H)
        return 0;
    to_local(&B, p, l);
    if (!in_hull(&B, l))
        return 0;
    if (push) {
        way_out(&B, l, mv);
        to_world(&B, mv, push);
    }
    return 1;
}

int16_t collide_segment(int16_t slot, const LfVec *a, const LfVec *b, LfVec *hit, int16_t *t1024)
{
    Body B;
    int32_t la[3], lb[3], ea[3], eb[3], p[3], dp[3], n, k, big, t0 = 0, t1 = 1024;
    int16_t i;
    if (!body(slot, &B))
        return 0;
    /* the segment's box against the ship's: nothing near, nothing more */
    if ((a->x < b->x ? a->x : b->x) >= B.c.x + B.H || (a->x > b->x ? a->x : b->x) <= B.c.x - B.H
        || (a->y < b->y ? a->y : b->y) >= B.c.y + B.H || (a->y > b->y ? a->y : b->y) <= B.c.y - B.H
        || (a->z < b->z ? a->z : b->z) >= B.c.z + B.H || (a->z > b->z ? a->z : b->z) <= B.c.z - B.H)
        return 0;
    to_local(&B, a, la);
    to_local(&B, b, lb);
    /* clipped to the box (t in 1024ths of the way from a to b) */
    for (i = 0; i < 3; i++) {
        int32_t d = lb[i] - la[i];
        if (!d) {
            if (la[i] <= -B.h[i] || la[i] >= B.h[i])
                return 0;
            continue;
        }
        {
            int32_t na = -B.h[i] - la[i], nb = B.h[i] - la[i], ta, tb;
            while (labs(na) > INT32_C(0x1FFFFF) || labs(nb) > INT32_C(0x1FFFFF)) {   /* (x 1024 inside 2^31) */
                na /= 2;
                nb /= 2;
                d /= 2;
            }
            if (!d)                     /* (a sliver of a segment, far off this axis's slab) */
                return 0;
            ta = na * 1024 / d;
            tb = nb * 1024 / d;
            if (ta > tb) {
                int32_t x = ta;
                ta = tb;
                tb = x;
            }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
        }
    }
    if (t0 > t1)
        return 0;
    /* in cell units (8 fraction bits) from where it enters to where it leaves, at most half a cell an axis a step */
    for (i = 0, big = 0; i < 3; i++) {
        int32_t x0 = la[i] + part_of(lb[i] - la[i], t0), x1 = la[i] + part_of(lb[i] - la[i], t1);
        ea[i] = (x0 + B.h[i]) * 256 / B.cs[i];
        eb[i] = (x1 + B.h[i]) * 256 / B.cs[i];
        if (labs(eb[i] - ea[i]) > big)
            big = labs(eb[i] - ea[i]);
    }
    n = big / 128 + 1;
    for (i = 0; i < 3; i++) {
        p[i] = ea[i] * 256;             /* (16 fraction bits as it goes; * 256, not << 8: a negative one's shift is
                                           undefined in C, the host harness's UBSan found) */
        dp[i] = (eb[i] - ea[i]) * 256 / n;
    }
    for (k = 0; k <= n; k++) {
        int16_t x = (int16_t)(p[0] >> 16), y = (int16_t)(p[1] >> 16), z = (int16_t)(p[2] >> 16);
        if (x >= 0 && x < SN && y >= 0 && y < SN && z >= 0 && z < SN
            && (!B.m || (BIT(B.m, 0, z, x) && BIT(B.m, 1, y, z) && BIT(B.m, 2, y, x)))) {
            if (hit) {
                int32_t l[3];
                LfVec w;
                for (i = 0; i < 3; i++)
                    l[i] = (p[i] >> 8) * B.cs[i] / 256 - B.h[i];
                to_world(&B, l, &w);
                hit->x = B.c.x + w.x;
                hit->y = B.c.y + w.y;
                hit->z = B.c.z + w.z;
            }
            if (t1024)                  /* how far along, a to b */
                *t1024 = (int16_t)(t0 + (t1 - t0) * k / n);
            return 1;
        }
        for (i = 0; i < 3; i++)
            p[i] += dp[i];
    }
    return 0;
}

int16_t collide_shape(int16_t slot, LfVec corner[8], LfVec *v, int16_t maxv)
{
    Body P;
    int32_t q[3], r;
    int16_t k, n = 0, i;
    if (!body(slot, &P))
        return 0;
    for (k = 0; k < 8; k++) {
        int32_t sa = k & 1 ? P.h[0] : -P.h[0], su = k & 2 ? P.h[1] : -P.h[1], sf = k & 4 ? P.h[2] : -P.h[2];
        corner[k].x = P.c.x + (sa * P.ax[0][0] + su * P.ax[1][0] + sf * P.ax[2][0]) / 256;
        corner[k].y = P.c.y + (sa * P.ax[0][1] + su * P.ax[1][1] + sf * P.ax[2][1]) / 256;
        corner[k].z = P.c.z + (sa * P.ax[0][2] + su * P.ax[1][2] + sf * P.ax[2][2]) / 256;
    }
    for (k = 0; n < maxv && vital(&P, k, q, &r); k++, n++) {
        int32_t w[3];
        for (i = 0; i < 3; i++)
            w[i] = (q[0] * P.ax[0][i] + q[1] * P.ax[1][i] + q[2] * P.ax[2][i]) / 256;
        v[n].x = P.c.x + w[0];
        v[n].y = P.c.y + w[1];
        v[n].z = P.c.z + w[2];
    }
    return n;
}
