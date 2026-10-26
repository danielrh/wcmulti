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
static int __far g_n;
static int __far g_bolts, __far g_balls;   /* the pools' room (0: not ours, or none) */
static unsigned __far g_added, __far g_hits;   /* the flight's shots and the hits shot_hit found (logged at its end) */

int shots_flight_start(int own_pools)
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

int shots_count(void) { return g_n; }

Shot __far *shot(int i)
{
    RV_CHECK(i, g_n);
    return &g_s[i];
}

int shot_add(int owner, unsigned weapon_type, const RvVec3 *pos, const RvVec3 *vel)
{
    const RvWeapon __far *w = weapon(weapon_type);
    Shot __far *s;
    RvVec3 d;
    long step;
    if (!g_s || g_n >= SHOTS_MAX)
        return -1;
    s = &g_s[g_n];
    s->x = pos->x; s->y = pos->y; s->z = pos->z;
    s->vx = vel->x; s->vy = vel->y; s->vz = vel->z;
    s->owner = (unsigned char)owner;
    s->weapon = (unsigned short)weapon_type;
    s->look = w ? w->look : RV_LOOK_BOLT;
    s->damage = w ? w->damage : 1;
    /* its life: its weapon's range at its weapon's speed (a beam's speed is 0: two steps) */
    s->life = w && w->speed_kps ? (unsigned short)((long)w->range_km * KPS_HZ / w->speed_kps) : 2;
    if (!s->life)
        s->life = 1;
    /* its drawn half length along its way (a bolt's; a beam's the step it reaches) */
    d.x = vel->x / 256; d.y = vel->y / 256; d.z = vel->z / 256;   /* (km a step) */
    step = (long)lf_sqrt((unsigned long)(d.x * d.x + d.y * d.y + d.z * d.z));
    vec_norm(&d);
    if (s->look == RV_LOOK_BEAM) {
        step = step * 8 > 32000L ? 32000L : step * 8;   /* (half the step, x16, as a short can keep it) */
        s->hx = (short)(d.x * step / 256); s->hy = (short)(d.y * step / 256); s->hz = (short)(d.z * step / 256);
    } else {
        s->hx = (short)(d.x * BOLT_HALF16 / 256); s->hy = (short)(d.y * BOLT_HALF16 / 256);
        s->hz = (short)(d.z * BOLT_HALF16 / 256);
    }
    g_added++;
    return g_n++;
}

void shot_remove(int i)
{
    RV_CHECK(i, g_n);
    g_s[i] = g_s[--g_n];
}

void shots_move(void)
{
    int i;
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

int shot_hit(int i, int *slot, RvVec3 *where)
{
    Shot __far *sh = shot(i);
    LfVec a, b, hit;
    long ax = labs(sh->vx), ay = labs(sh->vy), az = labs(sh->vz);
    int s, t;
    b.x = sh->x; b.y = sh->y; b.z = sh->z;
    a.x = b.x - sh->vx; a.y = b.y - sh->vy; a.z = b.z - sh->vz;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++) {
        long r;
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
            return 1;
        }
    }
    return 0;
}

int shot_source(int i)
{
    Shot __far *sh = shot(i);
    int p = SHOT_PROXY_SLOT;
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

void shots_place(const LfVec *eye, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1)
{
    LzBolt __far *b = g_bolts ? bolts_pool() : 0;
    LzBall __far *ball = g_balls ? balls_pool() : 0;
    int i, nb = 0, nl = 0;
    if (!g_bolts && !g_balls)
        return;
    for (i = 0; i < g_n; i++) {
        const Shot __far *s = &g_s[i];
        if (s->look == RV_LOOK_BOLT || s->look == RV_LOOK_BEAM) {
            if (!b || nb >= g_bolts)
                continue;
            /* (a bolt's middle: its head less its half length; positions are sixteenths x 16) */
            b[nb].x = s->x - s->hx * 16L; b[nb].y = s->y - s->hy * 16L; b[nb].z = s->z - s->hz * 16L;
            b[nb].hx = s->hx; b[nb].hy = s->hy; b[nb].hz = s->hz;
            nb++;
        } else if (s->look >= RV_LOOK_SLUG && s->look <= RV_LOOK_STAR) {
            if (!ball || nl >= g_balls)
                continue;
            ball[nl].x = s->x; ball[nl].y = s->y; ball[nl].z = s->z;
            ball[nl].vx = ball[nl].vy = ball[nl].vz = 0;   /* (drawn where the list has it, not flown) */
            ball[nl].kind = (unsigned char)(s->look - RV_LOOK_SLUG);
            ball[nl].phase = (unsigned char)i;
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

void shots_draw_far(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    if (g_bolts)
        bolts_draw_far(y0, y1, ext, ext_y0, ext_rows);
    if (g_balls)
        balls_draw_far(y0, y1, ext, ext_y0, ext_rows);
}

void shots_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    if (g_bolts)
        bolts_draw_near(y0, y1, ext, ext_y0, ext_rows);
    if (g_balls)
        balls_draw_near(y0, y1, ext, ext_y0, ext_rows);
}
