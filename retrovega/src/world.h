/* world.h: the flight's world as the game logic sees it (world.c): the names of the slot arrays' values, the
 * vector and orientation helpers, the gameplay random stream, a ship into a slot and out of it.
 *
 * The world is the fixed block's slot arrays (rvhooks.h, abi/rvabi.def): a slot is an entity, its arrays say
 * what it is (maybeEntityType), where (gPositionVector, 256 a km), which way (the three orientation vectors,
 * 256 = 1.0), how fast (gVelocityVector, positions a step; setSpeed / speedLo in kps), and how hurt (curShield,
 * armorState, damagePoints).  docs/logic.md is the guide. */
#ifndef WORLD_H
#define WORLD_H

#include "rvhooks.h"
#include "rvdata.h"

/* EntityType (maybeEntityType; spec 4.3): the layer compares them with < and >=, so the order matters */
enum { ET_NONE = 0, ET_NAV = 2, ET_NAVICON = 3, ET_DUST = 4, ET_EXPLOSION = 5, ET_SPARKLE = 6, ET_ENGINE = 7,
       ET_BOLT = 8, ET_ASTEROID = 9, ET_MINE = 10, ET_MISSILE = 11, ET_SHIP = 12, ET_CAPSHIP = 13 };
/* rvController (spec 6.4): who flies the slot */
enum { CTL_AI, CTL_PLAYER, CTL_PUPPET, CTL_SCRIPT, CTL_FROZEN };
/* commAnimInfo: the side */
enum { SIDE_TERRAN, SIDE_AERA, SIDE_RLAAN };

#define KM 256L                         /* positions a km */
#define ONE 256                         /* 1.0 in an orientation vector */
#define TURN 1024                       /* a whole turn in lf_sin / lf_cos / lf_atan2's angles */
#define KPS_HZ 20                       /* the step rate a kps is converted at: a kps is KM / KPS_HZ = 12.8 positions a
                                           step (spec 6.1).  The flight steps LOGIC_HZ times a second (logic.h): while
                                           the two differ everything moves LOGIC_HZ / KPS_HZ times its nominal speed */
#define KPS_STEP(kps) ((long)(kps) * KM / KPS_HZ)   /* a speed in kps as positions a step */

/* a slot's shield words (k: 0 front, 1 rear) and armour words (k: 0 front, 1 rear, 2 left, 3 right) */
#define SHIELD(s, k) (((unsigned short *)curShield)[(s) * 2 + (k)])
#define SHIELD_MAX(s, k) (((unsigned short *)shieldMax)[(s) * 2 + (k)])
#define ARMOR(s, k) (((unsigned short *)armorState)[(s) * 4 + (k)])
/* a slot's gun table (spec 4.5): its count, and gun record g's weapon type, mount and state */
#define GUN_COUNT(s) (gunTable[(s) * gunTable_SIZE])
#define GUN_REC(s, g) (gunTable + (s) * gunTable_SIZE + 1 + (g) * 5)
#define GUN_TYPE(s, g) (*(unsigned short *)GUN_REC(s, g))
#define GUN_MOUNT_X(s, g) (((signed char *)GUN_REC(s, g))[2])   /* across, 1/32 of the half box (collide_half) */
#define GUN_MOUNT_Y(s, g) (((signed char *)GUN_REC(s, g))[3])   /* up, the same */
#define GUN_STATE(s, g) (GUN_REC(s, g)[4])   /* bit 7 destroyed; a gun's cooldown steps, a launcher's rounds */
#define GUN_DESTROYED 0x80

/* vectors (RvVec3: positions, or 256 = 1.0 for a direction) */
long vec_dot(const RvVec3 *a, const RvVec3 *b);
void vec_norm(RvVec3 *v);               /* to length 256 (no change to a zero vector) */
void vec_cross(RvVec3 *out, const RvVec3 *a, const RvVec3 *b);   /* (a x b) / 256 */
void vec_turn(RvVec3 *a, RvVec3 *b, int t);   /* a and b turned by t (TURN a whole turn) in their plane:
                                           a' = a cos + b sin, b' = b cos - a sin.  (front, right): a yaw to the
                                           right; (front, up): a pitch up; (right, up): a roll to the left */

/* a slot's ship */
int slot_is_ship(int s);                /* 0 .. RV_MAX_SHIP_SLOT holding a ship or a capital ship */
const RvShipType __far *slot_type(int s);   /* its type's numbers (data/ships.csv), or NULL */
void ship_orthonormalise(int s);        /* its frame made unit length and square again (do it after turning it) */
void ship_face(int s, int yaw, int pitch);   /* its frame set to face that way (TURN a whole turn), level */
long player_km(int s);                  /* km from the player, roughly (the largest axis and half the rest) */
void ship_rel(int from, int s, RvVec3 *out);   /* s as from sees it: right, up, ahead, in km */

/* a ship into slot s: its type's numbers (speeds, shields, armour, the gun table its loadout), at a place
 * (positions), facing a way (TURN a whole turn of yaw), flown by `controller` (CTL_*) */
void ship_spawn(int s, int type, int side, int controller, long x, long y, long z, int yaw);
void ship_clear(int s);                 /* the slot empty (no explosion, nothing else: the caller's) */
int ship_free_slot(void);               /* the first empty ship slot 1 .. RV_MAX_SHIP_SLOT, or -1 */
void ship_guns(int s);                  /* its gun table from its type's loadout (ship_spawn does it) */

/* the gameplay random stream: randomSeed's (spec 6.3), so the layer can re-seed it before it replays a spawn or a
 * hit.  Only for what changes play; pictures take their own */
unsigned game_rand(void);               /* 0 .. 32767 */

#endif
