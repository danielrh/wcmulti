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
#define RV_PI 3.141592653589793238462643383279
typedef char rv_sar_check[(INT32_C(-1) >> 1) == -1 ? 1 : -1];   /* >> on a negative number shifts in its sign
                                           (implementation-defined in C; wcc, gcc and clang do): TO_KM, FIX_MUL and
                                           vec_turn rely on it.  A compiler that shifts in zeros fails here */
#define KM_SHIFT 8                      /* KM as a shift */
#define KM (INT32_C(1) << KM_SHIFT)     /* positions a km */
#define TO_KM(p) ((p) >> KM_SHIFT)      /* positions as km: a shift, where / KM is a runtime divide on DOS.  Rounds
                                           down (-0.5 km to -1) where / rounds toward zero */
#define ONE_SHIFT 8                     /* ONE as a shift */
#define ONE (1 << ONE_SHIFT)            /* 1.0 in an orientation vector */
#define FIX_MUL(a, d) (((int32_t)(a) * (d)) >> ONE_SHIFT) /* a times d, one of them in ONEs (an orientation
                                           vector's component): a shift, where / ONE is a runtime divide on DOS.
                                           Rounds down (-0.5 to -1) where / rounds toward zero */
#define TURN 1024                       /* a whole turn in lf_sin / lf_cos / lf_atan2's angles */
#define KPS_HZ 20                       /* the step rate a kps is converted at: a kps is KM / KPS_HZ = 12.8 positions a
                                           step (spec 6.1).  The flight steps LOGIC_HZ times a second (logic.h): while
                                           the two differ everything moves LOGIC_HZ / KPS_HZ times its nominal speed */
#define NUM_SHIP_SLOTS (RV_MAX_SHIP_SLOT + 1) /* The number of active ship slots, including 0 and RV_MAX_SHIP_SLOT. */
#define KPS_STEP(kps) ((int32_t)(kps) * KM / KPS_HZ)   /* a speed in kps as positions a step */

/* fixed-point constants: dividing by a constant without a runtime divide (on DOS a 32-bit / is a library call,
 * __I4D; a 32-bit * is one too, __I4M, but cheaper).  QCONST(num, den, q) is num / den with q fraction bits,
 * rounded, worked out by the compiler: integer constants only (the double is there because num << q can pass 32
 * bits; a variable in it would bring floating point into the DOS build), and QCONST_CHECK fails the build on
 * anything else.  QMUL(x, k, q) is x times such a constant, rounded to the nearest (a half up), q >= 1: |x| times
 * k must stay under 2^31.  Approximate: TURN_STEP says what that costs and how to be exact instead */
#ifdef __GNUC__
#define QCONST_ONE(x) (__builtin_constant_p(x) ? 1 : -1)   /* (gcc and clang fold 0 x a variable to 0: ask them) */
#else
#define QCONST_ONE(x) (1 + 0 * (x))     /* (wcc: a variable makes it no integer constant, so no width) */
#endif
/* 0, but a bit-field's width must be an integer constant (in every C: an array's size need not be in C99, where a
 * variable makes it a VLA), so a struct with QCONST_ONE of each argument as one fails the build when any isn't */
#define QCONST_CHECK(num, den, q) \
    ((int32_t)(0 * sizeof(struct { char qconst_; unsigned : QCONST_ONE(num); unsigned : QCONST_ONE(den); \
                                   unsigned : QCONST_ONE(q); })))
#define QCONST(num, den, q) (QCONST_CHECK(num, den, q) + (int32_t)((double)(num) * (INT32_C(1) << (q)) / (den) + 0.5))
#define QMUL(x, k, q) (((int32_t)(x) * (k) + (INT32_C(1) << ((q) - 1))) >> (q))

/* a step's turn.  A ship turns dps x steer / 9 degrees a second at a steer of -9..9 (the stick's scale, as the
 * layer reads it in steerInput, spec 4), a step's share at KPS_HZ: dps x TURN x steer / (360 x KPS_HZ x 9) TURN
 * units.  That is under a unit a step for the slowest ships (4 dps: 0.57), so a turn is kept in 1/256ths of a unit
 * and the fraction carried from step to step (logic.c) */
#define TURN_FRAC_SHIFT 8               /* a step's turn in 1 << this parts of a TURN unit */
#define TURN_STEP_DIV (360L * KPS_HZ * 9)   /* the formula's divisor: 64,800 */
#define TURN_STEP_Q 16                  /* TURN_STEP_K's fraction bits (16: QMUL's >> is a word move on DOS) */
#define TURN_STEP_K QCONST((int32_t)TURN << TURN_FRAC_SHIFT, TURN_STEP_DIV, TURN_STEP_Q)   /* 4.0454 in Q16: 265,121 */
/* the turn a step for a rate of dps (0..255, ships.csv) at a steer (-9..9), in 1/256ths of a TURN unit: dps x steer
 * (a 16-bit multiply) times TURN_STEP_K, then a shift; no divide.
 *
 * Its error: against the exact dps x steer x TURN x 256 / TURN_STEP_DIV rounded to the nearest, 18 of the 4,864
 * (dps, steer) pairs come out 1 off (1/256 of a unit, 0.0014 degrees), none more: TURN_STEP_K is 4.0454254 for
 * 4.0454321.  It is the same on every machine (integer maths only), so the host and DOS agree and recordings replay;
 * the carried fraction swallows the difference.
 *
 * To change it:
 *   - TURN, KPS_HZ or TURN_FRAC_SHIFT: TURN_STEP_K follows by itself.  rv_turn_step_check (below) fails the build
 *     when 255 x 9 x TURN_STEP_K could pass 2^31 (a finer TURN_FRAC_SHIFT or a smaller KPS_HZ, say): lower
 *     TURN_STEP_Q until it fits, at some accuracy (Q12: 65 of the 4,864 pairs 1 off), and below 16 QMUL's >> is a
 *     shift loop on DOS.
 *   - Exact (every pair the rounded quotient): no 32-bit constant can do it.  The quotient's fractions are multiples
 *     of 1/2,025 (TURN x 256 / TURN_STEP_DIV is 8,192 / 2,025), so telling a half from its neighbours over x up to
 *     2,295 needs some 23 fraction bits, a product of about 2^36, and 16-bit C reaches only the low 32 bits of a
 *     product (Granlund and Montgomery's and Robison's exact methods want the high half).  The exact way that fits
 *     is the plain divide, one runtime call more a turn (n is at most 2,295 x 262,144, under 2^31; / rounds toward
 *     zero, so the half goes away from it):
 *         n = (int32_t)dps * steer * ((int32_t)TURN << TURN_FRAC_SHIFT);
 *         turn = (n + (n < 0 ? -TURN_STEP_DIV / 2 : TURN_STEP_DIV / 2)) / TURN_STEP_DIV;
 *     or a 32 x 32 -> 64 multiply (the 386's imul through #pragma aux on DOS, an int64_t twin on the host).
 *   - Either way the turns change a little, and every turning ship's path with them.  The host and DOS change
 *     together (the play tests' --dos hashes still match), but recordings made before (rvsim --replay) no longer
 *     fly the same */
#define TURN_STEP(dps, steer) QMUL((int16_t)(dps) * (int16_t)(steer), TURN_STEP_K, TURN_STEP_Q)
/* (a bound on TURN_STEP_K in integers, as an array's size needs: the ceiling of TURN x 256 / TURN_STEP_DIV, << Q) */
typedef char rv_turn_step_check[((TURN * (INT32_C(1) << TURN_FRAC_SHIFT) + TURN_STEP_DIV - 1) / TURN_STEP_DIV
                                 << TURN_STEP_Q) <= INT32_C(0x7FFFFFFF) / (255 * 9) ? 1 : -1];

/* a slot's shield words (k: 0 front, 1 rear) and armour words (k: 0 front, 1 rear, 2 left, 3 right) */
#define SHIELD(s, k) (((uint16_t *)curShield)[(s) * 2 + (k)])
#define SHIELD_MAX(s, k) (((uint16_t *)shieldMax)[(s) * 2 + (k)])
#define ARMOR(s, k) (((uint16_t *)armorState)[(s) * 4 + (k)])
/* a slot's gun table (spec 4.5): its count, and gun record g's weapon type, mount and state */
#define GUN_COUNT(s) (gunTable[(s) * gunTable_SIZE])
#define GUN_REC(s, g) (gunTable + (s) * gunTable_SIZE + 1 + (g) * 5)
#define GUN_TYPE(s, g) (*(uint16_t *)GUN_REC(s, g))
#define GUN_MOUNT_X(s, g) (((int8_t *)GUN_REC(s, g))[2])   /* across, 1/32 of the half box (collide_half) */
#define GUN_MOUNT_Y(s, g) (((int8_t *)GUN_REC(s, g))[3])   /* up, the same */
#define GUN_STATE(s, g) (GUN_REC(s, g)[4])   /* bit 7 destroyed; a gun's cooldown steps, a launcher's rounds */
#define GUN_DESTROYED 0x80

/* vectors (RvVec3: positions, or 256 = 1.0 for a direction) */
int32_t vec_dot(const RvVec3 *a, const RvVec3 *b);
void vec_norm(RvVec3 *v);               /* to length 256 (no change to a zero vector) */
void vec_cross(RvVec3 *out, const RvVec3 *a, const RvVec3 *b);   /* (a x b) / 256 */
void vec_turn(RvVec3 *a, RvVec3 *b, int16_t t);   /* a and b turned by t (TURN a whole turn) in their plane:
                                           a' = a cos + b sin, b' = b cos - a sin.  (front, right): a yaw to the
                                           right; (front, up): a pitch up; (right, up): a roll to the left */
inline RvVec3 vec3(int32_t x, int32_t y, int32_t z)
{
    RvVec3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}
/* do_damage's `where`: a near pointer to an RvVec3 (on DOS its offset in DGROUP; the host harness maps one).  Pass
 * RV_WHERE(&v) for a vector of yours (a local: the stack is in DGROUP), and read the one do_damage_impl was given
 * through RV_WHERE_VEC(where) (only read it: on the host it can be a copy) */
#ifdef RV_HOST
uint16_t rv_where(const RvVec3 *v);
#define RV_WHERE(p) rv_where(p)
#define RV_WHERE_VEC(w) ((const RvVec3 *)(rv_dgroup + (uint16_t)(w)))
#else
#define RV_WHERE(p) ((uint16_t)(p))
#define RV_WHERE_VEC(w) ((const RvVec3 *)(w))
#endif

/* a slot's ship */
int16_t slot_is_ship(int16_t s);        /* 0 .. RV_MAX_SHIP_SLOT holding a ship or a capital ship */
const RvShipType __far *slot_type(int16_t s);   /* its type's numbers (data/ships.csv), or NULL */
void ship_orthonormalise(int16_t s);    /* its frame made unit length and square again (do it after turning it) */
void ship_face(int16_t s, int16_t yaw, int16_t pitch);   /* its frame set to face that way (TURN a whole turn), level */
int32_t player_km(int16_t s);           /* km from the player, roughly (the largest axis and half the rest) */
void ship_rel(int16_t from, int16_t s, RvVec3 *out);   /* s as from sees it: right, up, ahead, in km */

/* a ship into slot s: its type's numbers (speeds, shields, armour, the gun table its loadout), at a place
 * (positions), facing a way (TURN a whole turn of yaw), flown by `controller` (CTL_*) */
void ship_spawn(int16_t s, int16_t type, int16_t side, int16_t controller, int32_t x, int32_t y, int32_t z,
                int16_t yaw);
void ship_clear(int16_t s);             /* the slot empty (no explosion, nothing else: the caller's) */
int16_t ship_free_slot(void);           /* the first empty ship slot 1 .. RV_MAX_SHIP_SLOT, or -1 */
void ship_guns(int16_t s);              /* its gun table from its type's loadout (ship_spawn does it) */

/* the gameplay random stream: randomSeed's (spec 6.3), so the layer can re-seed it before it replays a spawn or a
 * hit.  Only for what changes play; pictures take their own */
uint16_t game_rand(void);               /* 0 .. 32767 */

#endif
