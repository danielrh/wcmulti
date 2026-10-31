/* shots.c: the flight's gunfire (shots.h).
 *
 * The list is the truth; the bolts' and balls' pools (bolts.c, balls.c) are refilled from it once a frame, so the
 * drawing never decides anything.  A hit is found as a bolt's is in /hulldemo: a coarse test against each ship's
 * half box (one compare an axis), then the segment of the shot's last step through the ship's silhouette hull
 * (collide_segment).  The module's data is far (DGROUP is nearly full). */
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "rv.h"
#include "campaign.h"
#include "bolts.h"
#include "balls.h"
#include "collide.h"
#include "world.h"
#include "shots.h"

static Shot __far *__far g_s;           /* the list (SHOTS_MAX), or 0 */
static int16_t __far g_n;
static int16_t __far g_bolts, __far g_balls;   /* the pools' room (0: not ours, or none) */
static uint16_t __far g_added, __far g_hits;   /* the flight's shots and the hits shot_hit found (logged at its end) */
void (*__far shots_on_add)(int16_t owner, uint16_t weapon_type);
void (*__far shots_on_hit)(int16_t slot, const RvVec3 *where, int16_t owner, uint16_t damage);

int16_t shots_flight_start(int16_t own_pools)
{
    static const char __far s_room[] = "shots: room for %d (bolts drawn %d, balls %d)";
    shots_flight_end();
    g_added = g_hits = 0;
    g_s = (Shot __far *)_fmalloc(SHOTS_MAX * sizeof(Shot));
    if (own_pools && g_s) {
        g_bolts = bolts_init(SHOTS_MAX, 48);   /* (48 KB kept for the rocks, should a field come) */
        g_balls = balls_init(SHOTS_MAX, 48);
    }
    rv_logf(s_room, g_s ? SHOTS_MAX : 0, g_bolts, g_balls);
    return g_s ? SHOTS_MAX : 0;
}

void shots_flight_end(void)
{
    static const char __far s_tally[] = "shots: %u fired, %u hits";
    if (g_s) {
        rv_logf(s_tally, g_added, g_hits);
        _ffree(g_s);
    }
    if (g_bolts)
        bolts_free();
    if (g_balls)
        balls_free();
    g_s = 0;
    g_n = g_bolts = g_balls = 0;
}

int16_t shots_count(void) { return g_n; }

uint16_t shots_added(void) { return g_added; }

Shot __far *shot(int16_t i)
{
    RV_CHECK(i, g_n);
    return &g_s[i];
}

int16_t shot_add(int16_t owner, uint16_t weapon_type, const RvVec3 *pos, const RvVec3 *vel)
{
    const RvWeapon __far *w = weapon(weapon_type);
    Shot __far *s;
    RvVec3 d;
    int32_t step;
    if (!g_s || g_n >= SHOTS_MAX)
        return -1;
    s = &g_s[g_n];
    s->x = pos->x; s->y = pos->y; s->z = pos->z;
    s->vx = vel->x; s->vy = vel->y; s->vz = vel->z;
    s->owner = (uint8_t)owner;
    s->weapon = (uint16_t)weapon_type;
    s->look = w ? w->look : RV_LOOK_BOLT;
    s->damage = w ? w->damage : 1;
    /* its life: its weapon's range at its weapon's speed (a beam's speed is 0: two steps) */
    s->life = w && w->speed_kps ? (uint16_t)((int32_t)w->range_km * KPS_HZ / w->speed_kps) : 2;
    if (!s->life)
        s->life = 1;
    /* its drawn half length along its way (a bolt's; a beam's the step it reaches) */
    d.x = vel->x / 256; d.y = vel->y / 256; d.z = vel->z / 256;   /* (km a step) */
    step = (int32_t)lf_sqrt((uint32_t)(d.x * d.x + d.y * d.y + d.z * d.z));
    vec_norm(&d);
    if (s->look == RV_LOOK_BEAM) {
        step = step * 8 > INT32_C(32000) ? INT32_C(32000) : step * 8;   /* (half the step, x16, as a short can keep it) */
        s->hx = (int16_t)(d.x * step / 256); s->hy = (int16_t)(d.y * step / 256); s->hz = (int16_t)(d.z * step / 256);
    } else {
        s->hx = (int16_t)(d.x * BOLT_HALF16 / 256); s->hy = (int16_t)(d.y * BOLT_HALF16 / 256);
        s->hz = (int16_t)(d.z * BOLT_HALF16 / 256);
    }
    g_added++;
    if (shots_on_add)
        shots_on_add(owner, weapon_type);
    return g_n++;
}

void shot_remove(int16_t i)
{
    RV_CHECK(i, g_n);
    g_s[i] = g_s[--g_n];
}

void shots_move(void)
{
    int16_t i;
    maybeEntityType[SHOT_PROXY_SLOT] = ET_NONE;   /* (the stand-in is a step's) */
    for (i = g_n - 1; i >= 0; i--) {
        Shot __far *s = &g_s[i];
        if (!s->life || !--s->life) {
            shot_remove(i);
            continue;
        }
        s->x += s->vx;
        s->y += s->vy;
        s->z += s->vz;
    }
}

int16_t shot_hit(int16_t i, int16_t *slot, RvVec3 *where)
{
    Shot __far *sh = shot(i);
    LfVec a, b, hit;
    int32_t ax = labs(sh->vx), ay = labs(sh->vy), az = labs(sh->vz);
    int16_t s, t;
    b.x = sh->x; b.y = sh->y; b.z = sh->z;
    a.x = b.x - sh->vx; a.y = b.y - sh->vy; a.z = b.z - sh->vz;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++) {
        int32_t r;
        if (s == sh->owner || !slot_is_ship(s))
            continue;
        r = collide_half(s);            /* (the coarse test: within its half box and the step on every axis) */
        if (labs(b.x - gPositionVector[s].x) > r + ax || labs(b.y - gPositionVector[s].y) > r + ay
            || labs(b.z - gPositionVector[s].z) > r + az)
            continue;
        if (collide_segment(s, &a, &b, &hit, &t)) {
            *slot = s;
            where->x = hit.x; where->y = hit.y; where->z = hit.z;
            g_hits++;
            if (shots_on_hit)
                shots_on_hit(s, where, sh->owner, sh->damage);
            return 1;
        }
    }
    return 0;
}

int16_t shot_source(int16_t i)
{
    Shot __far *sh = shot(i);
    int16_t p = SHOT_PROXY_SLOT;
    maybeEntityType[p] = ET_BOLT;
    parentShipForShip[p] = sh->owner;
    gInstanceShipTypes[p] = sh->weapon;
    damageProperty[p] = sh->damage;
    commAnimInfo[p] = commAnimInfo[sh->owner];
    rvController[p] = CTL_FROZEN;
    rvFlags[p] = 0;
    gPositionVector[p].x = sh->x; gPositionVector[p].y = sh->y; gPositionVector[p].z = sh->z;
    gVelocityVector[p].x = sh->vx; gVelocityVector[p].y = sh->vy; gVelocityVector[p].z = sh->vz;
    return p;
}

/* --- the drawing --- */

void shots_place(const LfVec *eye, const LfBasis *cam, int16_t cx, int16_t cy, int16_t focal, int16_t y0, int16_t y1)
{
    LzBolt __far *b = g_bolts ? bolts_pool() : 0;
    LzBall __far *ball = g_balls ? balls_pool() : 0;
    int16_t i, nb = 0, nl = 0;
    if (!g_bolts && !g_balls)
        return;
    for (i = 0; i < g_n; i++) {
        const Shot __far *s = &g_s[i];
        if (s->look == RV_LOOK_BOLT || s->look == RV_LOOK_BEAM) {
            if (!b || nb >= g_bolts)
                continue;
            /* (a bolt's middle: its head less its half length; positions are sixteenths x 16) */
            b[nb].x = s->x - s->hx * INT32_C(16); b[nb].y = s->y - s->hy * INT32_C(16);
            b[nb].z = s->z - s->hz * INT32_C(16);
            b[nb].hx = s->hx; b[nb].hy = s->hy; b[nb].hz = s->hz;
            nb++;
        } else if (s->look >= RV_LOOK_SLUG && s->look <= RV_LOOK_STAR) {
            if (!ball || nl >= g_balls)
                continue;
            ball[nl].x = s->x; ball[nl].y = s->y; ball[nl].z = s->z;
            ball[nl].vx = ball[nl].vy = ball[nl].vz = 0;   /* (drawn where the list has it, not flown) */
            ball[nl].kind = (uint8_t)(s->look - RV_LOOK_SLUG);
            ball[nl].phase = (uint8_t)i;
            nl++;
        }
    }
    if (g_bolts) {
        bolts_set_count(nb);
        bolts_place(eye->x, eye->y, eye->z, cam, cx, cy, focal, y0, y1, BOLT_LINE16);
    }
    if (g_balls) {
        balls_set_count(nl);
        balls_place(eye->x, eye->y, eye->z, cam, cx, cy, focal, y0, y1, frameCounter / 2);   /* (15 animation frames
                                           a second, as the benchmark's) */
    }
}

void shots_draw_far(int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows)
{
    if (g_bolts)
        bolts_draw_far(y0, y1, ext, ext_y0, ext_rows);
    if (g_balls)
        balls_draw_far(y0, y1, ext, ext_y0, ext_rows);
}

void shots_draw_near(int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows)
{
    if (g_bolts)
        bolts_draw_near(y0, y1, ext, ext_y0, ext_rows);
    if (g_balls)
        balls_draw_near(y0, y1, ext, ext_y0, ext_rows);
}
