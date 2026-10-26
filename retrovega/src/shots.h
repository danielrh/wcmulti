/* shots.h: the flight's gunfire (shots.c): the bolts and balls guns put into the world, outside the slot table.
 *
 * The slot table has room for few temporaries and the layer never needed bolts to be slots: it replicates a shot
 * as the fireGunFromShip call that made it (each machine runs the call and flies its own copy) and a hit as the
 * do_damage call it led to.  So the shots live here, SHOTS_MAX at once, each with its owner, its weapon, its damage
 * and its life, and are drawn with the bolts' and the balls' code (bolts.c, balls.c) by the look its weapon has.
 * What the layer does need from a shot is a slot to name as do_damage's source, a bolt (EntityType 8) whose parent
 * is the ship that fired: shot_source() makes one, the side channel's single stand-in (SHOT_PROXY_SLOT).
 * docs/logic.md is the guide. */
#ifndef SHOTS_H
#define SHOTS_H

#include "rvhooks.h"
#include "lightfield.h"

#define SHOTS_MAX 192                   /* in the world at once */
#define SHOT_PROXY_SLOT (RV_MAX_SHIP_SLOT + 1)   /* the stand-in bolt do_damage names (the first temporary slot) */

typedef struct {
    long x, y, z;                       /* its head, positions (256 a km) */
    long vx, vy, vz;                    /* positions a step */
    short hx, hy, hz;                   /* its drawn half length along its way, sixteenths of a km (a bolt's) */
    unsigned short weapon;              /* its weapon's type (data/weapons.csv) */
    unsigned short damage;              /* what a hit does (do_damage's qty): the weapon's, to change at will */
    unsigned short life;                /* steps left */
    unsigned char owner;                /* the slot that fired it */
    unsigned char look;                 /* RV_LOOK_*: how it is drawn (bolt, beam: a capsule; slug .. star: a ball) */
} Shot;                                 /* 38 bytes */

int shots_flight_start(int own_pools);  /* none in flight; room made for SHOTS_MAX (own_pools 0: a test scene has
                                           the bolts' and balls' pools: the shots are kept but not drawn) */
void shots_flight_end(void);

int shots_count(void);
Shot __far *shot(int i);                /* 0 .. shots_count()-1 */
/* a shot of a weapon (its look, damage and life from data/weapons.csv: the life its range at its speed) from pos
 * (positions) at vel (positions a step): its index, or -1 when SHOTS_MAX are flying.  The rest is the caller's to
 * change through shot(i) */
int shot_add(int owner, unsigned weapon_type, const RvVec3 *pos, const RvVec3 *vel);
void shot_remove(int i);                /* gone: the last one takes its index (walk the list backwards to remove) */
void shots_move(void);                  /* each a step of its velocity and of its life; the spent ones removed */

/* the first ship its last step (from its head less its velocity, to its head) went into, not its owner's: 1, the
 * slot, and where (the first point in the ship's hull: do_damage's `where`) */
int shot_hit(int i, int *slot, RvVec3 *where);
/* the slot to name as do_damage's source for shot i: SHOT_PROXY_SLOT made a bolt of its owner's, its weapon and
 * damage; good until the next shots_move */
int shot_source(int i);

/* the flight's drawing (flight.c): once a frame through the camera, then the far ones before the ships and the
 * near ones after, band by band */
void shots_place(const LfVec *eye, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1);
void shots_draw_far(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);
void shots_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

#endif
