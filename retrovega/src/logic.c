/* logic.c: the game's logic in flight (logic.h).  THIS FILE IS THE GAME's, to be written: docs/logic.md is its
 * guide (the order of a step, the helpers, the rules the multiplayer layer needs kept).
 *
 * The flight loop (rvflight.inc, flight.c) runs logic_frame once a frame, between reading the controls (g_pilot)
 * and drawing: everything that happens in the world happens here, in the slot arrays (world.h) and the shots
 * (shots.h); the drawing, the cockpit, the music and the asteroid fields then show the world as it stands.  The
 * thunk bodies below (the _impl functions) are reached through RVHOOK's thunks: call do_damage, fireGunFromShip,
 * aiFighterThink and the rest by their thunk names, never the _impl, so the layer sees every call.
 *
 * Everything below is empty but the mission's ships put in their slots at the start and the thunks whose whole job
 * the ABI fixes (marked PLUMBING).  The first flight's stand-ins (the steering, the throttle, the stand-in AI, the
 * damage model's first cut) are in git: `git show 65bc2a7b:retrovega/src/flight.c` (ship_physics, do_damage_impl). */
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "panel.h"
#include "collide.h"
#include "flight.h"
#include "world.h"
#include "shots.h"
#include "hud.h"
#include "logic.h"

PilotInput __far g_pilot;
static int16_t __far g_turn_frac[NUM_SHIP_SLOTS][3];

/* --- the minds' decisions: a turn a slot (step_minds fills it, step_motion flies it), and each mind's memory --- */

typedef struct { int8_t x, y, roll, pad; } Turn;   /* steer_x, steer_y, roll: -9 .. 9, as g_pilot gives them */
static Turn __far g_turn[NUM_SHIP_SLOTS];

/* an AI's memory (the AI, below): what it is doing, and to whom */
typedef struct {
    uint8_t tactic, phase;              /* the maneuver running (T_*), its phase */
    int16_t timer;                      /* steps left in it */
    uint8_t target;                     /* the slot it fights, 0xFF none (rvTarget says the same) */
    uint8_t leader;                     /* the slot it keeps station on, 0xFF none */
    int8_t dir;                         /* which way a break or a loop goes */
    uint8_t burn;                       /* the afterburner this tactic */
    uint8_t fire;                       /* FIRE_*: what step_fire does for it this step */
    uint8_t launcher;                   /* the launcher FIRE_MISSILE fires */
    uint8_t profile;                    /* its AiProfile (P_*) */
    uint8_t pad;
    uint16_t lock;                      /* steps its target has sat in its missiles' lock cone */
    uint16_t turrets;                   /* a capital ship's turrets to fire this step, a bit each */
    uint16_t since;                     /* the step it last fired a missile */
    uint8_t passing;                    /* a pass on its target is coming this step (the avoidance leaves it out) */
    uint8_t gap;                        /* steps since its last volley (saturating): the profile's fire_gap */
} Mind;
enum { FIRE_GUNS = 1, FIRE_MISSILE = 2 };
static Mind __far g_mind[NUM_SHIP_SLOTS];

/* a missile's own (its slot has no ship type) */
typedef struct { uint16_t weapon, life; } MissileState;
static MissileState __far g_missile[NUM_SHIP_SLOTS];

#define RAM_STEPS 10                    /* a ship hurt by a ram is not hurt by one again for this many steps */
static uint16_t __far g_rammed[NUM_SHIP_SLOTS];   /* the step a ship was last hurt by a ram */
static uint16_t __far g_died[NUM_SHIP_SLOTS];     /* the step a ship was seen dying (0: not), its explosion's start */
#define DEATH_STEPS (LOGIC_HZ * 3 / 2)  /* a fighter explodes this long before its slot ends; a capital ship longer */
#define DEATH_STEPS_CAPITAL (LOGIC_HZ * 4)

static uint8_t profile_of(uint16_t type, uint16_t side);

void logic_prepare_spawn(int16_t ship_index)
{
    if (ship_index < 0 || ship_index >= NUM_SHIP_SLOTS)   /* (only ship slots keep anything: nothing to forget) */
        return;
    _fmemset(&g_turn_frac[ship_index][0], 0, sizeof(g_turn_frac[0]));
    _fmemset(&g_turn[ship_index], 0, sizeof(Turn));
    _fmemset(&g_mind[ship_index], 0, sizeof(Mind));
    g_mind[ship_index].target = g_mind[ship_index].leader = 0xFF;
    g_mind[ship_index].profile = profile_of(gInstanceShipTypes[ship_index], commAnimInfo[ship_index]);
    g_missile[ship_index].weapon = 0xFFFF;
    g_missile[ship_index].life = 0;
    g_rammed[ship_index] = (uint16_t)(frameCounter - RAM_STEPS);
    g_died[ship_index] = 0;
}

/* the HUD's marks, worked out once a step (hud_marks, the step's end) and drawn band by band (logic_hud, some four
 * times a frame: it only draws).  arm 0: none yet */
static struct {
    int16_t cx, cy, arm;                /* the gunsight: the view's middle (where the nose points), its arms (px) */
    int16_t target;                     /* the target's brackets: 1 when it is on the view */
    int16_t x0, y0, x1, y1, colour;
} __far g_hud;

/* --- the mission: its nav points and ships, as camp_load_units put them in the tables the layer reads --- */

#define NAVS 16                         /* navPointTable's records: nav n is record n (nav 0 the launch, the origin) */
#define NAV_REC(n) (navPointTable + navPointTable_SIZE * (n))
#define NAV_AT(n, k) (*(const int16_t *)(NAV_REC(n) + 2 + 2 * (k)))   /* its place, k 0..2: x, y, z in km */
#define NAV_SHIPS(n) ((const uint16_t *)(NAV_REC(n) + 0x39))         /* its ten mission ships, 0xFFFF ends them */
#define MSHIP(r) (missionShipTable + missionShipTable_SIZE * ((r) & 63))
#define MSHIP_SIDE(r) (*(const uint16_t *)(MSHIP(r) + 2))
#define MSHIP_BEHAVIOUR(r) (*(const uint16_t *)(MSHIP(r) + 6))     /* 6 the player, 4 a wingman */
#define NAV_REACH_KM 5000               /* the player this near a nav point has reached it (enterNavPoint) */

static uint16_t g_navs_seen;            /* a bit per nav point the player has reached (nav 0 from the start) */
static uint8_t __far g_killed[64];      /* a mission ship (its record) seen dying: shipAiState 9 */

static int16_t nav_defined(int16_t n)
{
    return n == 0 || NAV_REC(n)[0] == n;
}

/* how far the player is from nav point n, km, roughly (the largest axis and half the rest, as player_km) */
static int32_t nav_km(int16_t n)
{
    int32_t dx = labs(TO_KM(gPositionVector[0].x) - NAV_AT(n, 0)), dy = labs(TO_KM(gPositionVector[0].y) - NAV_AT(n, 1)),
            dz = labs(TO_KM(gPositionVector[0].z) - NAV_AT(n, 2)), m = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    return m + ((dx + dy + dz - m) >> 1);
}

/* the mission's rule, a patrol's: every nav point reached and every enemy of the mission's killed */
static int16_t mission_done(void)
{
    const uint16_t *at;
    int16_t n, j;
    for (n = 0; n < NAVS; n++) {
        if (!nav_defined(n))
            continue;
        if (!(g_navs_seen >> n & 1))
            return 0;
        at = NAV_SHIPS(n);
        for (j = 0; j < 10 && at[j] != 0xFFFF; j++)
            if (MSHIP_SIDE(at[j]) != commAnimInfo[0] && !g_killed[at[j] & 63])
                return 0;
    }
    return 1;
}

/* --- the flight begins --- */

/* the player into slot 0 (the camera and the cockpit are slot 0's) at the launch point, facing ahead, and nav 0's
 * other mission ships (the wing) at it through outerSpawnShipEntity; the other nav points' come when the player
 * gets there (enterNavPoint).  The mission (m) is in the tables already: camp_load_units put it there */
void logic_start(const RvMission __far *m)
{
    /* Clear the accumulation of fractional turn progress */
    const uint16_t *at = NAV_SHIPS(0);
    int16_t j;
    (void)m;
    _fmemset(&g_pilot, 0, sizeof g_pilot);
    _fmemset(g_turn_frac, 0, sizeof(g_turn_frac));
    _fmemset(g_turn, 0, sizeof g_turn);
    _fmemset(g_mind, 0, sizeof g_mind);
    _fmemset(g_missile, 0, sizeof g_missile);
    _fmemset(g_rammed, 0, sizeof g_rammed);
    _fmemset(g_died, 0, sizeof g_died);
    memset(systemDamage, 0, systemDamage_SIZE);   /* the player's systems whole (flight.c clears only the cockpit's).  (A
                                           near memset: Watcom makes a far pointer from this fixed address with segment
                                           0, so _fmemset cleared the interrupt table instead and the damage stayed) */
    _fmemset(g_killed, 0, sizeof g_killed);
    _fmemset(&g_hud, 0, sizeof g_hud);   /* (no marks until the first step works them out) */
    g_navs_seen = 1;
    currentNavPoint = 0;
    victoryPoints = 0;                  /* (over the launch's roll: won when the mission is done, step_mission) */
    ship_spawn(0, g_camp.player_ship, SIDE_TERRAN, CTL_PLAYER, 0, 0, 0, 0);
    shipStateByte[0] = 8;               /* (a human) */
    slotMissionShip[0] = playerMissionShip;
    for (j = 0; j < 10 && at[j] != 0xFFFF; j++)
        if (MSHIP_BEHAVIOUR(at[j]) != 6)
            (void)outerSpawnShipEntity(at[j], 0);
}

/* --- a step: the main loop's body --- */

/* the points a store of `most` gains this step to fill in 1 << shift steps, spread evenly over them:
 * phase (the step's place among them) takes its share, and the shares add up to `most`.  The share is
 * (phase + 1) * most >> shift less phase * most >> shift: all that decides it is what adding `most` carries out of
 * the low `shift` bits of phase * most, so 16 bits do (tools/rvdata.py keeps most to 32768 and shift to 15) */
static uint16_t refill(uint16_t most, uint16_t phase, uint8_t shift)
{
    return (uint16_t)((((uint16_t)(phase * most) & ((1U << shift) - 1)) + most) >> shift);
}

/* 1. what runs down or builds up by itself: the guns' cooldowns (GUN_STATE), gun energy (gunEnergy), the shields
 * coming back (SHIELD toward SHIELD_MAX), the afterburner's fuel.  (The guns' are M2's.)  The rates are the type's
 * (gun_energy_steps, shield_full_shift).  The armour doesn't come back in flight; a dying ship's shields stay down;
 * a puppet's are the layer's */
static void step_timers(void)
{
    int16_t ship, k;
    for (ship = 0; ship < NUM_SHIP_SLOTS; ship++) {
        const RvShipType __far *type_of_ship;
        uint8_t shift, energy_steps;
        uint16_t phase;
        if (!slot_is_ship(ship) || shipAiState[ship] == 9 || rvController[ship] == CTL_PUPPET)
            continue;
        type_of_ship = slot_type(ship);
        shift = type_of_ship ? type_of_ship->shield_full_shift : RV_SHIELD_FULL_SHIFT;
        energy_steps = type_of_ship ? type_of_ship->gun_energy_steps : RV_GUN_ENERGY_STEPS;
        phase = frameCounter & ((1U << shift) - 1);
        for (k = 0; k < 2; k+=1) {
            uint16_t most = SHIELD_MAX(ship, k);
            if (SHIELD(ship, k) != most) {
                uint16_t up = refill(most, phase, shift);
                SHIELD(ship, k) = SHIELD(ship, k) + up < most ? SHIELD(ship, k) + up : most;
            }
        }
        if (gunEnergy[ship] < 100 && (uint16_t)(frameCounter + ship) % energy_steps == 0) {
            gunEnergy[ship] += 1;
        }
        {
            int16_t gun_count = GUN_COUNT(ship);
            for (k = 0; k < gun_count; k+=1) {
                const RvWeapon __far *w = weapon(GUN_TYPE(ship, k));
                if (w && (w->kind == RV_WK_GUN || w->kind == RV_WK_TURRET) && (GUN_STATE(ship, k) & 0x7F)) {
                    GUN_STATE(ship, k) -= 1;
                }
            }
        }
    }
}

/* 2. each ship's mind, by its rvController: CTL_PLAYER flies by g_pilot; CTL_AI thinks through the aiFighterThink
 * or aiShipThink thunk (ET_CAPSHIP); CTL_PUPPET is the layer's (it writes the slot: nothing here); CTL_SCRIPT the
 * mission's; CTL_FROZEN nothing.  A mind decides: where to turn, the throttle (aiSetSpeed), whom to target */
static void step_minds(void)
{
    int16_t s;
    for (s = 0; s < NUM_SHIP_SLOTS; s++) {
        if (!slot_is_ship(s))
            continue;
        g_turn[s].x = g_turn[s].y = g_turn[s].roll = 0;
        g_mind[s].fire = 0;
        g_mind[s].turrets = 0;
        if (shipAiState[s] == 9)        /* dying: it neither thinks nor steers (it coasts to its end) */
            continue;
        switch (rvController[s]) {
        case CTL_PLAYER:
            g_turn[s].x = g_pilot.steer_x;
            g_turn[s].y = g_pilot.steer_y;
            g_turn[s].roll = g_pilot.roll;
            rvTarget[s] = (uint8_t)(g_sys.target >= 0 ? g_sys.target : 0xFF);   /* (the layer reads them) */
            playerTarget = (uint16_t)(g_sys.target >= 0 ? g_sys.target : 0xFFFF);
            break;
        case CTL_AI:                    /* (always the thunk: the layer skips it where the ship is a puppet) */
            if (maybeEntityType[s] == ET_CAPSHIP)
                aiShipThink((uint16_t)s);
            else
                aiFighterThink((uint16_t)s);
            break;
        default:                        /* a puppet (the layer flies it), a script's, a frozen one: nothing of its own */
            break;
        }
    }
}

/* 3. the fire loop: who pulls a trigger this step (the player's g_pilot.guns with g_sys.armed, g_pilot.missile;
 * the AI's own decisions) and which guns are ready (cooled, the energy there): each one fired through the
 * fireGunFromShip thunk (or maybe_fire_all_guns), which makes the shot */
/* a ship's launcher with rounds left: the first (the panel has no weapon choice yet), -1 none */
static int16_t launcher_of(int16_t s)
{
    int16_t g;
    for (g = 0; g < GUN_COUNT(s); g++) {
        const RvWeapon __far *w = weapon(GUN_TYPE(s, g));
        if (w && (w->kind == RV_WK_MISSILE || w->kind == RV_WK_TORPEDO || w->kind == RV_WK_ROCKET)
            && !(GUN_STATE(s, g) & GUN_DESTROYED) && (GUN_STATE(s, g) & 0x7F))
            return g;
    }
    return -1;
}

static void step_fire(void)
{
    int16_t s, g;
    /* the player: the missile and gun key */
    if (slot_is_ship(0) && rvController[0] == CTL_PLAYER && shipAiState[0] != 9 &&  g_sys.armed)
    {
        if (g_pilot.missile) {
            int16_t g = launcher_of(0);
            if (g >= 0) {
                (void)fireGunFromShip(0, (uint16_t)g);
            }
        }
        if (g_pilot.guns) {
            maybe_fire_all_guns(0);
        }
    }
    /* the minds' choices (aiFighterThink, aiShipThink left them in g_mind): through the thunks */
    for (s = 1; s < NUM_SHIP_SLOTS; s++) {
        if (!slot_is_ship(s) || rvController[s] != CTL_AI || shipAiState[s] == 9)
            continue;
        if (g_mind[s].fire & FIRE_GUNS)
            maybe_fire_all_guns((uint16_t)s);
        if (g_mind[s].fire & FIRE_MISSILE)
            (void)fireGunFromShip((uint16_t)s, g_mind[s].launcher);
        if (g_mind[s].turrets)          /* a capital ship's turrets that bear */
            for (g = 0; g < GUN_COUNT(s) && g < 16; g++)
                if (g_mind[s].turrets & (1u << g))
                    (void)fireGunFromShip((uint16_t)s, (uint16_t)g);
    }
}


int32_t vec_len(const RvVec3 *a) {
    return lf_sqrt((uint32_t)(a->x * a->x + a->y * a->y + a->z * a->z));
}

/* the player's systems: systemDamage's bytes (one set, the player's ship's: 0 whole .. SYS_WRECKED), each shown hurt
 * by a cockpit overlay (its rvCockpitDamage bit, tools/cockpits.py BITS).  A hit through to the hull may hurt one
 * (do_damage_impl) */
enum { SYS_ABSORBERS,                   /* the acceleration absorbers: hurt, the slide comes on at slide_dmg_kps */
       SYS_COUNT };
#define SYS_WRECKED 4                   /* systemDamage's most */
#define CP_FLIGHT_CONTROLS 15           /* the absorbers' overlay: every cockpit kit has it, and no panel goes dark
                                           with it */
static const uint8_t __far k_sys_overlay[SYS_COUNT] = { CP_FLIGHT_CONTROLS };

static int32_t reduce_slide(int16_t ship_index)
{
    const RvShipType __far *t = slot_type(ship_index);
    /* the speed above which a full-rate turn slides (ships.csv) */
    int32_t slide = ship_index == 0 && systemDamage[SYS_ABSORBERS] ? t->slide_dmg_kps : t->slide_kps;
    int32_t target_speed = speedLo[ship_index];
    const RvVec3 *front = &gOrientationFrontVector[ship_index];
    int32_t step = KPS_STEP(target_speed);
    RvVec3 target = vec3(
        FIX_MUL(front->x, step),
        FIX_MUL(front->y, step),
        FIX_MUL(front->z, step));
    RvVec3 *velocity = &gVelocityVector[ship_index];
    RvVec3 delta = vec3(
        target.x - velocity->x,
        target.y - velocity->y,
        target.z - velocity->z);
    int32_t delta_len = vec_len(&delta);
    /* at full rate the nose turns yaw_dps / KPS_HZ degrees a step: at the slide speed that moves the velocity it asks
     * for by `a` a step, the most it can follow.  (The float part is all constants: it folds to 1146) */
    int32_t a = KPS_STEP(slide) * t->yaw_dps / (int32_t)(KPS_HZ * 180.0 / RV_PI + 0.5);
    if (delta_len <= a) {
        *velocity = target;
    } else {
        velocity->x += delta.x * a / delta_len;
        velocity->y += delta.y * a / delta_len;
        velocity->z += delta.z * a / delta_len;
    }
    return target_speed;
}


static void step_speed(int16_t ship_index, uint16_t phase)
{
    uint16_t v = (uint16_t)speedLo[ship_index];
    const RvShipType __far *t = slot_type(ship_index);
    uint16_t desired_speed = t->max_kps;
    if (rvAfterburner[ship_index] && t->ab_kps) {
        desired_speed = t->ab_kps;
    } else if (setSpeed[ship_index] < t->max_kps) {
        desired_speed = setSpeed[ship_index];
    }
    {
        uint16_t dv = t->accel / KPS_HZ + (phase < t->accel % KPS_HZ);

        if (v < desired_speed) {
            v = desired_speed - v > dv ? v + dv : desired_speed;
        } else {
            v = v - desired_speed > dv ? v - dv : desired_speed;
        }
    }
    speedLo[ship_index] = v;
}

static int16_t turn_units(int16_t ship_index, int16_t axis, int32_t amount)
{
    int32_t acc;
    int16_t whole_part;
    RV_CHECK(ship_index, NUM_SHIP_SLOTS);   /* (a ship slot always: step_motion's loop; rv_fail if not) */
    RV_CHECK(axis, 3);
    acc = g_turn_frac[ship_index][axis] + amount;
    /* Note that the implementation-defined behavior of >> on negatives is
       divide due to rv_sar_check. */
    whole_part = (int16_t)(acc >> TURN_FRAC_SHIFT);
    g_turn_frac[ship_index][axis] = (int16_t)(acc - (int32_t) whole_part * (1 << TURN_FRAC_SHIFT));
    return whole_part;
}


static void step_turn(int16_t ship_index,
                      int8_t steer_x,
                      int8_t steer_y,
                      int8_t roll)
{
    if (steer_x || steer_y || roll) {
        const RvShipType __far *t = slot_type(ship_index);
        if (steer_x) {
            vec_turn(&gOrientationFrontVector[ship_index],
                     &gOrientationRightVector[ship_index],
                     turn_units(ship_index, 0, TURN_STEP(t->yaw_dps, steer_x)));
        }
        if (steer_y) {
            vec_turn(&gOrientationFrontVector[ship_index],
                     &gOrientationUpVector[ship_index],
                     turn_units(ship_index, 1, TURN_STEP(t->pitch_dps, steer_y)));
        }
        if (roll) {
            vec_turn(&gOrientationRightVector[ship_index],
                     &gOrientationUpVector[ship_index],
                     turn_units(ship_index, 2, TURN_STEP(t->roll_dps, roll)));
        }
        ship_orthonormalise(ship_index);
    }
}
  

/* ====================================================================================================================
 * Helpers of the minds, the missiles and the collisions: angles and lengths in km (positions overflow when squared)
 * ==================================================================================================================== */

/* the angle of (x, y) in the plane of x (0) and y (a quarter turn), -512 .. 511 (TURN a whole turn): the arguments
 * halved until lf_atan2 takes them (under 2^16) */
static int16_t angle_of(int32_t y, int32_t x)
{
    int16_t t;
    while (x >= 32768L || x <= -32768L || y >= 32768L || y <= -32768L) {
        x /= 2;
        y /= 2;
    }
    t = lf_atan2(y, x);
    return (int16_t)(t >= 512 ? t - 1024 : t);
}

/* |(x, y, z)|, the parts halved until their squares fit */
static int32_t len_of(int32_t x, int32_t y, int32_t z)
{
    int32_t k = 1;
    while (labs(x) > 30000L || labs(y) > 30000L || labs(z) > 30000L) {
        x /= 2;
        y /= 2;
        z /= 2;
        k *= 2;
    }
    return (int32_t)lf_sqrt((uint32_t)(x * x) + (uint32_t)(y * y) + (uint32_t)(z * z)) * k;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* km between two slots */
static int32_t km_between(int16_t a, int16_t b)
{
    return len_of(TO_KM(gPositionVector[b].x - gPositionVector[a].x), TO_KM(gPositionVector[b].y - gPositionVector[a].y),
                  TO_KM(gPositionVector[b].z - gPositionVector[a].z));
}

/* a world vector (km, or anything under 2^23) as s's frame sees it: right, up, ahead */
static void to_body(int16_t s, const RvVec3 *d, RvVec3 *out)
{
    out->x = vec_dot(d, &gOrientationRightVector[s]) / 256;
    out->y = vec_dot(d, &gOrientationUpVector[s]) / 256;
    out->z = vec_dot(d, &gOrientationFrontVector[s]) / 256;
}

/* s flying along its nose at its speed (after a jump: no momentum the other way to slide off) */
static void fly_ahead(int16_t s)
{
    const RvVec3 *F = &gOrientationFrontVector[s];
    int32_t step = KPS_STEP(speedLo[s]);
    gVelocityVector[s] = vec3(FIX_MUL(F->x, step), FIX_MUL(F->y, step), FIX_MUL(F->z, step));
}

/* where a gun record's mount is (positions): across and up in 32nds of the half box, and the nose ahead */
static const RvShipType __far* gun_mount(int16_t s, int16_t g, RvVec3 *out)
{
    const RvShipType __far *t = slot_type(s);
    int32_t h = collide_half(s), ax = (int32_t)GUN_MOUNT_X(s, g) * (h / 32), ay = (int32_t)GUN_MOUNT_Y(s, g) * (h / 32);
    int32_t az = t ? (int32_t)t->hit[2] * (h / 127) : h / 2;
    const RvVec3 *R = &gOrientationRightVector[s], *U = &gOrientationUpVector[s], *F = &gOrientationFrontVector[s];
    out->x = gPositionVector[s].x + FIX_MUL(R->x, ax) + FIX_MUL(U->x, ay) + FIX_MUL(F->x, az);
    out->y = gPositionVector[s].y + FIX_MUL(R->y, ax) + FIX_MUL(U->y, ay) + FIX_MUL(F->y, az);
    out->z = gPositionVector[s].z + FIX_MUL(R->z, ax) + FIX_MUL(U->z, ay) + FIX_MUL(F->z, az);
    return t;
}

/* the hull left, percent */
static int16_t hull_pct(int16_t s)
{
    const RvShipType __far *t = slot_type(s);
    if (!t || !t->hull)
        return 100;
    return (int16_t)(100 - (int32_t)damagePoints[s] * 100 / t->hull);
}

/* the first of s's guns (a gun or a turret, not destroyed), -1 none: its weapon says how far and fast they shoot */
static int16_t gun_of(int16_t s)
{
    int16_t g;
    for (g = 0; g < GUN_COUNT(s); g++) {
        const RvWeapon __far *w = weapon(GUN_TYPE(s, g));
        if (w && (w->kind == RV_WK_GUN || w->kind == RV_WK_TURRET) && !(GUN_STATE(s, g) & GUN_DESTROYED))
            return g;
    }
    return -1;
}

/* ====================================================================================================================
 * The guns (M2: fireGunFromShip's guns and turrets, maybe_fire_all_guns): to be written (docs/gameplay.md)
 * ==================================================================================================================== */

/* (ship, gun, its weapon): a gun or a turret fires: a shot from its mount (gun_mount), along the nose at the
 * weapon's speed (a turret's toward rvTarget[ship]'s lead point: the ship's mind aims it); the cooldown into
 * GUN_STATE's low bits, the energy off gunEnergy.  Returns 0xFFFF */
static uint16_t gun_fire(uint16_t ship, uint16_t gun, const RvWeapon __far *w)
{
    RvVec3 physical_offset, vel;
    int32_t step = KPS_STEP(w->speed_kps);
    int16_t target = rvTarget[ship];
    RvVec3 front = gOrientationFrontVector[ship];
    (void)gun_mount(ship, gun, &physical_offset);
    if (w->kind == RV_WK_TURRET && target != 0xff && slot_is_ship(target)) {
        /* a turret's shot toward the target's lead point (km from the mount; the shot's time there, twice refined) */
        RvVec3 d, vt = vec3(TO_KM(gVelocityVector[target].x),
                            TO_KM(gVelocityVector[target].y),
                            TO_KM(gVelocityVector[target].z));
        int32_t km, tof, kps_step = w->speed_kps / KPS_HZ + 1;
        d = vec3(TO_KM(gPositionVector[target].x - physical_offset.x),
                 TO_KM(gPositionVector[target].y - physical_offset.y),
                 TO_KM(gPositionVector[target].z - physical_offset.z));
        tof = len_of(d.x, d.y, d.z) / kps_step;
        tof = len_of(d.x + vt.x * tof, d.y + vt.y * tof, d.z + vt.z * tof) / kps_step;
        d = vec3(d.x + vt.x * tof, d.y + vt.y * tof, d.z + vt.z * tof);
        km = len_of(d.x, d.y, d.z);
        while (labs(d.x) > 30000L || labs(d.y) > 30000L || labs(d.z) > 30000L) {   /* (a direction: 256 long) */
            d.x /= 2;
            d.y /= 2;
            d.z /= 2;
            km /= 2;
        }
        if (km) {
            d.x = d.x * 256 / km;
            d.y = d.y * 256 / km;
            d.z = d.z * 256 / km;
            front = d;
        }
    }
    vel = vec3(FIX_MUL(front.x, step),
               FIX_MUL(front.y, step),
               FIX_MUL(front.z, step));
    /* TODO: must we really add the ship's own velocity gVelocityVector[ship] */
    vel.x += gVelocityVector[ship].x;
    vel.y += gVelocityVector[ship].y;
    vel.z += gVelocityVector[ship].z;
    (void)shot_add((int16_t)ship, GUN_TYPE(ship, gun), &physical_offset, &vel);
    GUN_STATE(ship, gun) = (uint8_t)((GUN_STATE(ship, gun) & GUN_DESTROYED) | (w->refire & 0x7F));
     gunEnergy[ship] = (uint16_t)(gunEnergy[ship] > w->energy ? gunEnergy[ship] - w->energy : 0);
    return 0xFFFF;
}

/* (ship): each gun or turret that is ready (not destroyed, cooled, the energy there), through the fireGunFromShip
 * thunk; never a launcher */
static void guns_fire_ready(uint16_t ship)
{
    int16_t g;
    int16_t gun_count = GUN_COUNT(ship);
    uint16_t already_tried_bits = 0;
    for (g = 0; g < gun_count; g += 1) {
        int linked_count = 1;
        int16_t linked;
        uint16_t this_type = GUN_TYPE(ship, g);
        const RvWeapon __far *w = weapon(this_type);
        if (!w || (w->kind != RV_WK_GUN && w->kind != RV_WK_TURRET)) {
            continue;
        }
        if ((GUN_STATE(ship, g) & (0x7f | GUN_DESTROYED)) || gunEnergy[ship] < w->energy) {
            continue;
        }
        if ((1 << g) & already_tried_bits) {
            continue; /* we have tried to link-fire this weapon type */
        }


        for (linked = g + 1 ; linked < gun_count; linked += 1) {
            if (GUN_TYPE(ship, linked) == this_type && (GUN_STATE(ship, linked) & (0x7f | GUN_DESTROYED)) == 0) {
                already_tried_bits |= (1 << linked);
                linked_count += 1;
            }
        }
        if (gunEnergy[ship] < w->energy * linked_count) {
            continue;
        }
        for (linked = g ; linked < gun_count; linked += 1) {
            if (GUN_TYPE(ship, linked) == this_type && (GUN_STATE(ship, linked) & (0x7f | GUN_DESTROYED)) == 0) {
                (void)fireGunFromShip(ship, (uint16_t)linked);
            }
        }
    }
}

/* ====================================================================================================================
 * Collisions (M6): two ships inside one another are parted and both hurt
 * ==================================================================================================================== */

#define RAM_BASE 2                      /* a touch's damage */
#define RAM_PER 1600                    /* positions a step of closing speed a point more (125 kps: two fighters
                                           head on at their top speeds bump for a dozen points, as the old games had it) */

static void bump_away(int16_t s, int8_t dir);   /* (an AI ship's mind after a bump: the AI, below) */

/* a ship gives way in a collision: a frozen one or a puppet (the layer's) never */
static int16_t ram_moves(int16_t s)
{
    return rvController[s] != CTL_FROZEN && rvController[s] != CTL_PUPPET;
}

/* moved by part / 256 of push (positions); its velocity's part toward the other ship (into: the sign of the push that
 * points at it) taken away, and its speed quartered: a collision all but stops a ship */
static void ram_shove(int16_t s, const LfVec *push, int16_t part, int16_t into)
{
    RvVec3 n, *v = &gVelocityVector[s];
    int32_t d;
    gPositionVector[s].x += push->x / 256 * part + (push->x % 256) * part / 256;
    gPositionVector[s].y += push->y / 256 * part + (push->y % 256) * part / 256;
    gPositionVector[s].z += push->z / 256 * part + (push->z % 256) * part / 256;
    n = vec3(TO_KM(push->x), TO_KM(push->y), TO_KM(push->z));   /* (its way: km, so the squares fit) */
    vec_norm(&n);
    d = vec_dot(v, &n) / 256 * into;    /* (the velocity toward the other ship) */
    if (d > 0) {
        v->x -= FIX_MUL(n.x, d) * into;
        v->y -= FIX_MUL(n.y, d) * into;
        v->z -= FIX_MUL(n.z, d) * into;
    }
    speedLo[s] = speedLo[s] / 4;        /* (a bump takes the way off a ship) */
}

/* a collision's damage: the two ships' closing speed, positions a step, into points */
static uint16_t ram_damage(int16_t a, int16_t b)
{
    int32_t closing = len_of(gVelocityVector[a].x - gVelocityVector[b].x, gVelocityVector[a].y - gVelocityVector[b].y,
                             gVelocityVector[a].z - gVelocityVector[b].z);
    return (uint16_t)(RAM_BASE + closing / RAM_PER);
}

/* a and b touch coarsely: the fine test, then the push that parts them (the lighter, by its box, gives way the more;
 * one that cannot move leaves it all to the other) and the hurt, each with the other as the source and the other's
 * place as the hit's (the side facing it); a pair touching on hurts every RAM_STEPS */
static void ram(int16_t a, int16_t b)
{
    LfVec push;
    int32_t depth, ha, hb;
    int16_t ma, mb, pa, pb;
    uint16_t qty;
    if (!collide_ships(a, b, &push, &depth))
        return;
    push.x += push.x / 2;               /* (half as far again as touching, so the next step is not inside again) */
    push.y += push.y / 2;
    push.z += push.z / 2;
    ma = ram_moves(a);
    mb = ram_moves(b);
    ha = collide_half(a);
    hb = collide_half(b);
    if (ma && mb) {                     /* push is b's move: shared in the other's weight */
        pb = (int16_t)(ha * 256 / (ha + hb));
        pa = (int16_t)(256 - pb);
    } else {
        pa = ma ? 256 : 0;
        pb = mb ? 256 : 0;
    }
    qty = ram_damage(a, b);
    if (pb)
        ram_shove(b, &push, pb, -1);
    if (pa)
        ram_shove(a, &push, (int16_t)-pa, 1);
    if (rvController[a] == CTL_AI)      /* (a bump: the minds break away, each its own way) */
        bump_away(a, 1);
    if (rvController[b] == CTL_AI)
        bump_away(b, -1);
    if ((uint16_t)(frameCounter - g_rammed[a]) >= RAM_STEPS) {
        g_rammed[a] = frameCounter;
        (void)do_damage((uint16_t)b, (uint16_t)a, qty, RV_WHERE(&gPositionVector[b]));
    }
    if ((uint16_t)(frameCounter - g_rammed[b]) >= RAM_STEPS) {
        g_rammed[b] = frameCounter;
        (void)do_damage((uint16_t)a, (uint16_t)b, qty, RV_WHERE(&gPositionVector[a]));
    }
}

/* ====================================================================================================================
 * Missiles (M7): a launcher's round is a ship slot of ET_MISSILE that flies, homes and bursts on what it meets
 * ==================================================================================================================== */

#define MISSILE_HALF_KM 1               /* its half box (gMaybeShipRadius), km */
/* a seeker's turn rate by its lock (RV_LOCK_*: none, heat, image, auto, proximity), degrees a second; a torpedo's
 * own.  (weapons.csv has no column for it yet) */
static const uint8_t __far k_seeker_dps[5] = { 0, 70, 60, 90, 0 };
#define TORPEDO_DPS 25

static int16_t missile_dps(const RvWeapon __far *w)
{
    return w->kind == RV_WK_TORPEDO ? TORPEDO_DPS : w->lock < 5 ? k_seeker_dps[w->lock] : 0;
}

static int16_t nearest_enemy(int16_t s, int16_t capital_only);

/* whom a missile launched by `ship` homes on: a dumbfire nobody; the player's the cockpit's lock (a friend-or-foe
 * the target, else the nearest enemy); an AI's its own target */
static uint8_t missile_target_of(int16_t ship, const RvWeapon __far *w)
{
    int16_t t;
    if (w->lock == RV_LOCK_NONE || w->lock == RV_LOCK_PROXIMITY)
        return 0xFF;
    if (rvController[ship] != CTL_PLAYER)
        return rvTarget[ship];
    if (g_sys.target >= 0 && slot_is_ship(g_sys.target) && (g_sys.lock || w->lock == RV_LOCK_AUTO))
        return (uint8_t)g_sys.target;
    if (w->lock == RV_LOCK_AUTO && (t = nearest_enemy(ship, 0)) >= 0)
        return (uint8_t)t;
    return 0xFF;
}

/* (ship, gun, its weapon): a launcher fires: a round fewer, and the missile into a free ship slot at the mount,
 * facing the ship's way at its weapon's speed, its parent the ship, its target by the lock.  The slot (the layer
 * maps it), 0xFFFF when every slot is taken (the round is spent and nothing flies) */
static uint16_t missile_launch(uint16_t ship, uint16_t gun, const RvWeapon __far *w)
{
    int16_t s = ship_free_slot(), k;
    uint8_t rounds = (uint8_t)(GUN_STATE(ship, gun) & 0x7F);
    RvVec3 at;
    if (rounds)
        GUN_STATE(ship, gun) = (uint8_t)((GUN_STATE(ship, gun) & GUN_DESTROYED) | (rounds - 1));
    if (s < 0)
        return 0xFFFF;
    gun_mount((int16_t)ship, (int16_t)gun, &at);
    maybeEntityType[s] = ET_MISSILE;
    gInstanceShipTypes[s] = GUN_TYPE(ship, gun);   /* (its weapon: the drawing and the effects read it; no ship type) */
    commAnimInfo[s] = commAnimInfo[ship];
    rvController[s] = CTL_AI;
    parentShipForShip[s] = (uint8_t)ship;
    shipStateByte[s] = 0xFF;
    slotMissionShip[s] = 0xFFFF;
    gPositionVector[s] = at;
    gOrientationFrontVector[s] = gOrientationFrontVector[ship];
    gOrientationRightVector[s] = gOrientationRightVector[ship];
    gOrientationUpVector[s] = gOrientationUpVector[ship];
    setSpeed[s] = speedLo[s] = w->speed_kps;
    fly_ahead(s);
    for (k = 0; k < 2; k++)
        SHIELD(s, k) = SHIELD_MAX(s, k) = 0;
    for (k = 0; k < 4; k++)
        ARMOR(s, k) = 0;
    damagePoints[s] = 0;
    coreHp[s] = 0;
    gunEnergy[s] = 0;
    gunDamage[s] = 0;
    engineFlag[s] = 0;
    GUN_COUNT(s) = 0;
    shipAiState[s] = 0;
    gMaybeShipRadius[s] = MISSILE_HALF_KM;
    damageProperty[s] = w->damage;
    rvFlags[s] = 0;
    rvAfterburner[s] = 0;
    logic_prepare_spawn(s);
    missileTarget[s] = rvTarget[s] = missile_target_of((int16_t)ship, w);
    g_missile[s].weapon = GUN_TYPE(ship, gun);
    g_missile[s].life = (uint16_t)((int32_t)w->range_km * KPS_HZ / (w->speed_kps ? w->speed_kps : 1));
    return (uint16_t)s;
}

/* a seeker's nose toward its target's lead point (where the target will be when it gets there), by at most its rate */
static void missile_seek(int16_t s, int16_t t, int16_t dps)
{
    RvVec3 d, b;
    int32_t tof, horiz;
    int16_t yaw, pitch, most;
    d = vec3(TO_KM(gPositionVector[t].x - gPositionVector[s].x), TO_KM(gPositionVector[t].y - gPositionVector[s].y),
             TO_KM(gPositionVector[t].z - gPositionVector[s].z));
    tof = len_of(d.x, d.y, d.z) / (speedLo[s] / KPS_HZ + 1);   /* steps to it (km a step: kps / KPS_HZ) */
    d.x += TO_KM(gVelocityVector[t].x) * tof;
    d.y += TO_KM(gVelocityVector[t].y) * tof;
    d.z += TO_KM(gVelocityVector[t].z) * tof;
    to_body(s, &d, &b);
    horiz = len_of(b.x, 0, b.z);
    yaw = angle_of(b.x, b.z);
    pitch = angle_of(b.y, horiz);
    most = turn_units(s, 0, TURN_STEP(dps, 9));   /* (its rate's units this step, the fraction carried) */
    yaw = (int16_t)clamp32(yaw, -most, most);
    most = turn_units(s, 1, TURN_STEP(dps, 9));
    pitch = (int16_t)clamp32(pitch, -most, most);
    if (yaw)
        vec_turn(&gOrientationFrontVector[s], &gOrientationRightVector[s], yaw);
    if (pitch)
        vec_turn(&gOrientationFrontVector[s], &gOrientationUpVector[s], pitch);
    if (yaw || pitch)
        ship_orthonormalise(s);
}

/* a missile's step: its life down (gone at its range), its seeking, its move, and its hit: its step through a ship's
 * hull (any ship but its launcher), where it bursts (do_damage with the missile as the source) and its slot ends */
static void missile_step(int16_t s)
{
    const RvWeapon __far *w;
    RvVec3 last, hit;
    LfVec lh;
    int32_t reach;
    int16_t t, dps, k;
    if (rvController[s] == CTL_PUPPET) {   /* (the layer flies it) */
        gPositionVector[s].x += gVelocityVector[s].x;
        gPositionVector[s].y += gVelocityVector[s].y;
        gPositionVector[s].z += gVelocityVector[s].z;
        return;
    }
    w = weapon(g_missile[s].weapon);
    if (!w || !g_missile[s].life) {
        ship_clear(s);
        return;
    }
    g_missile[s].life--;
    t = missileTarget[s];
    dps = missile_dps(w);
    if (dps && t != 0xFF && slot_is_ship(t) && shipAiState[t] != 9)
        missile_seek(s, t, dps);
    last = gPositionVector[s];
    fly_ahead(s);
    gPositionVector[s].x += gVelocityVector[s].x;
    gPositionVector[s].y += gVelocityVector[s].y;
    gPositionVector[s].z += gVelocityVector[s].z;
    reach = KPS_STEP(speedLo[s]);
    for (k = 0; k < NUM_SHIP_SLOTS; k++) {
        int32_t h;
        if (!slot_is_ship(k) || k == parentShipForShip[s] || shipAiState[k] == 9)
            continue;
        h = collide_half(k) + reach;    /* (its step's box against the ship's: nothing near, nothing more) */
        if (labs(gPositionVector[s].x - gPositionVector[k].x) > h || labs(gPositionVector[s].y - gPositionVector[k].y) > h
            || labs(gPositionVector[s].z - gPositionVector[k].z) > h)
            continue;
        if (collide_segment(k, (const LfVec *)&last, (const LfVec *)&gPositionVector[s], &lh, 0)) {
            hit = vec3(lh.x, lh.y, lh.z);
            (void)do_damage((uint16_t)s, (uint16_t)k, w->damage, RV_WHERE(&hit));
            ship_clear(s);
            return;
        }
    }
}

/* ====================================================================================================================
 * The AI (M5): minds for the ships of CTL_AI, through aiFighterThink and aiShipThink (docs/ai.md)
 *
 * A mind runs a TACTIC (T_*: a maneuver with phases) picked from its PROFILE's table by the SITUATION it is in
 * (SIT_*: the target far, near ahead, on its tail, just passed, itself hurt, nobody about), with weighted dice from
 * game_rand.  The tactic decides where the nose should point (a direction in the ship's own frame), the throttle
 * (aiSetSpeed), the afterburner and whether to fire; the STEERING turns that direction into the steer and roll the
 * player's controls give (g_turn), within the profile's PULLS (which ways it may pull the nose: a ship that cannot
 * turn left rolls until the target is above or below it and pulls there) and its style (bank then pull, as the
 * fighters of the early nineties turned, or yaw and pitch at once).  The flight model (step_motion) flies it like
 * any ship, within its type's rates and speeds.  Every number is a table entry: the profiles are measured against
 * the old games' flights (docs/ai.md), and a new behaviour is a new row.
 * ==================================================================================================================== */

enum { T_IDLE, T_CRUISE, T_FORMATION, T_ESCORT, T_WAIT,         /* nobody to fight (T_WAIT: still, until a chance) */
       T_PURSUE, T_HEADON, T_STRAFE, T_SIT, T_RAM, T_TORPEDO,    /* attacks */
       T_BREAK, T_EVADE, T_LOOP, T_FLEE,                        /* getting away */
       T_COUNT };
enum { SIT_IDLE, SIT_FAR, SIT_NEAR, SIT_TAILED, SIT_PASSED, SIT_HURT, SIT_COUNT };
enum { PULL_RIGHT = 1, PULL_LEFT = 2, PULL_UP = 4, PULL_DOWN = 8, PULL_ALL = 15 };
enum { P_INTERCEPTOR, P_FIGHTER, P_HEAVY, P_ACE, P_BOMBER, P_WINGMAN, P_CAPITAL, P_WAITER, P_NOLEFT, P_COUNT };

#define PICKS 4
typedef struct { uint8_t tactic, weight; } Pick;
typedef struct {
    uint16_t engage_km;                 /* an enemy this near is fought */
    uint16_t fire_km;                   /* the guns fire within this (and their own range) */
    uint16_t break_km;                  /* a closing attacker breaks off this near (0: never) */
    uint16_t pounce_km;                 /* T_WAIT: a target this near is a chance */
    uint8_t aim_cos;                    /* the fire cone: the lead point within it (cos x 256) */
    uint8_t pulls;                      /* PULL_*: which ways the steering may pull the nose */
    uint8_t bank;                       /* 1: roll toward the target, then pull (near the nose, a yaw and a pitch at
                                           once: the fine aim); 0: yaw and pitch at once (with every pull), or the one
                                           pull always (without) */
    uint8_t ab_pct;                     /* percent of its closing and fleeing on the afterburner */
    uint8_t flee_pct;                   /* it runs with its hull below this percent (0: never) */
    uint8_t missile_pct;                /* percent of its locks that end in a launch */
    uint8_t wait_s;                     /* T_WAIT: the most it waits for a chance */
    uint8_t idle;                       /* its tactic with nobody to fight */
    uint16_t pass_pct;                  /* a pass goes by the target this percent of the two hulls' half boxes apart */
    uint16_t avoid_pct;                 /* another ship ahead within this percent of the two half boxes is steered round
                                           (when coming together at a quarter of the top speed or more) */
    uint8_t turn_pct;                   /* the AI uses this percent of its type's turn rate at most */
    uint8_t burst;                      /* steps of afterburner at the start of every break (the old games' dash; 0 none) */
    uint8_t lead_pct;                   /* how much of the lead it aims off (100: the shots meet the target; 0: it aims at
                                           the target itself and its passes come of the target's own motion, as the old
                                           game's fighters shot at 0 degrees off) */
    uint8_t tail_pct;                   /* behind its target within the guns' range it flies this percent of its top
                                           speed (the old game's fighters tailed at some 60%) */
    uint8_t fire_gap;                   /* steps between its volleys with the target in the cone (the old game's
                                           fighters: a volley every 0.75 s, not every frame their guns were cool) */
    Pick pick[SIT_COUNT][PICKS];        /* the dice by situation (SIT_IDLE's row is unused: idle says) */
} AiProfile;

#define AIM_CONE 250                    /* cos x 256 of the usual fire cone: some 7 degrees */
#define LOCK_CONE 230                   /* a missile locks with the target within this: some 26 degrees */
#define TAIL_KM 3500                    /* an attacker behind, this near, is on my tail */
#define RELAUNCH_STEPS (4 * LOGIC_HZ)   /* missiles no closer together than this */

static const AiProfile __far k_profiles[P_COUNT] = {
    /* P_INTERCEPTOR: light and fast: head-on passes, flat turns round, runs when hurt */
    { 25000, 3000, 900, 0, AIM_CONE, PULL_RIGHT | PULL_LEFT, 1, 40, 30, 50, 0, T_CRUISE, 170, 300, 100, 10, 0, 60, 22,
      { { { 0, 0 } }, { { T_HEADON, 60 }, { T_PURSUE, 40 } }, { { T_PURSUE, 60 }, { T_STRAFE, 40 } },
        { { T_PURSUE, 50 }, { T_BREAK, 30 }, { T_EVADE, 20 } }, { { T_PURSUE, 60 }, { T_BREAK, 40 } },
        { { T_FLEE, 60 }, { T_PURSUE, 40 } } } },
    /* P_FIGHTER: the line fighter, as the old game's Dralthi measured (tests/wc1/README.md): straight at the
     * target at full speed, a hard flat yaw away at 900 km (its passes 250-900 km), round again in a second or two
     * with a dash of afterburner at every pass, a tail at 60% of its speed, a volley every 0.75 s, keeps attacking
     * when hunted; it yaws (rolling to bring the target level) and does not pitch */
    { 25000, 3000, 900, 0, AIM_CONE, PULL_RIGHT | PULL_LEFT, 1, 30, 20, 50, 0, T_CRUISE, 170, 300, 100, 10, 0, 60, 22,
      { { { 0, 0 } }, { { T_PURSUE, 100 } }, { { T_PURSUE, 100 } },
        { { T_PURSUE, 50 }, { T_BREAK, 50 } }, { { T_PURSUE, 60 }, { T_BREAK, 40 } },
        { { T_PURSUE, 70 }, { T_FLEE, 30 } } } },
    /* P_HEAVY: slow and armoured: head on mostly, pulls rather than yaws, rarely runs */
    { 25000, 3000, 0, 0, AIM_CONE, PULL_ALL, 1, 20, 10, 60, 0, T_CRUISE, 170, 200, 100, 10, 0, 70, 22,
      { { { 0, 0 } }, { { T_HEADON, 80 }, { T_PURSUE, 20 } }, { { T_HEADON, 60 }, { T_PURSUE, 40 } },
        { { T_BREAK, 60 }, { T_EVADE, 40 } }, { { T_BREAK, 70 }, { T_LOOP, 30 } },
        { { T_PURSUE, 80 }, { T_FLEE, 20 } } } },
    /* P_ACE: strafing runs, hard evasion, never runs, uses the afterburner half the time */
    { 30000, 3000, 0, 0, AIM_CONE, PULL_ALL, 1, 50, 0, 70, 0, T_CRUISE, 150, 180, 100, 15, 50, 80, 15,
      { { { 0, 0 } }, { { T_STRAFE, 50 }, { T_PURSUE, 50 } }, { { T_STRAFE, 50 }, { T_PURSUE, 50 } },
        { { T_EVADE, 60 }, { T_BREAK, 20 }, { T_LOOP, 20 } }, { { T_LOOP, 50 }, { T_BREAK, 50 } },
        { { T_PURSUE, 100 } } } },
    /* P_BOMBER: torpedo runs on capital ships (off at 1,500 km once the torpedo is away), else a heavy's fight */
    { 25000, 2800, 1500, 0, AIM_CONE, PULL_ALL, 1, 20, 25, 80, 0, T_CRUISE, 170, 200, 100, 10, 100, 70, 22,
      { { { 0, 0 } }, { { T_TORPEDO, 70 }, { T_PURSUE, 30 } }, { { T_TORPEDO, 60 }, { T_PURSUE, 40 } },
        { { T_BREAK, 70 }, { T_EVADE, 30 } }, { { T_BREAK, 70 }, { T_LOOP, 30 } },
        { { T_FLEE, 60 }, { T_PURSUE, 40 } } } },
    /* P_WINGMAN: the player's side: keeps station, fights what comes near like a fighter */
    { 12000, 3000, 900, 0, AIM_CONE, PULL_ALL, 0, 30, 25, 50, 0, T_FORMATION, 170, 300, 100, 10, 0, 60, 22,
      { { { 0, 0 } }, { { T_PURSUE, 70 }, { T_HEADON, 30 } }, { { T_PURSUE, 80 }, { T_HEADON, 20 } },
        { { T_BREAK, 50 }, { T_EVADE, 50 } }, { { T_PURSUE, 60 }, { T_BREAK, 40 } },
        { { T_FLEE, 50 }, { T_PURSUE, 50 } } } },
    /* P_CAPITAL: holds its course; its turrets and launchers are aiShipThink's */
    { 8000, 6000, 0, 0, AIM_CONE, PULL_ALL, 0, 0, 0, 100, 0, T_CRUISE, 100, 100, 100, 0, 100, 100, 0,
      { { { 0, 0 } }, { { T_CRUISE, 100 } }, { { T_CRUISE, 100 } }, { { T_CRUISE, 100 } }, { { T_CRUISE, 100 } },
        { { T_CRUISE, 100 } } } },
    /* P_WAITER: lies still (cloaked) until a target comes near unawares, then a fighter's fight on the afterburner */
    { 25000, 3000, 900, 5000, AIM_CONE, PULL_ALL, 0, 60, 30, 50, 30, T_WAIT, 170, 300, 100, 10, 0, 60, 22,
      { { { 0, 0 } }, { { T_PURSUE, 70 }, { T_HEADON, 30 } }, { { T_PURSUE, 60 }, { T_STRAFE, 40 } },
        { { T_PURSUE, 50 }, { T_BREAK, 30 }, { T_EVADE, 20 } }, { { T_PURSUE, 60 }, { T_BREAK, 40 } },
        { { T_FLEE, 60 }, { T_PURSUE, 40 } } } },
    /* P_NOLEFT: a fighter whose controls cannot turn left: it rolls and pulls instead */
    { 25000, 3000, 900, 0, AIM_CONE, PULL_RIGHT | PULL_UP | PULL_DOWN, 0, 30, 20, 50, 0, T_CRUISE, 170, 300, 100, 10, 0, 60, 22,
      { { { 0, 0 } }, { { T_HEADON, 60 }, { T_PURSUE, 40 } }, { { T_PURSUE, 70 }, { T_HEADON, 30 } },
        { { T_EVADE, 40 }, { T_LOOP, 40 }, { T_BREAK, 20 } }, { { T_LOOP, 50 }, { T_BREAK, 50 } },
        { { T_PURSUE, 60 }, { T_FLEE, 40 } } } },
};

/* a type's profile (data/ships.csv's roles; a mission ship's record may name another) */
static uint8_t profile_of(uint16_t type, uint16_t side)
{
    const RvShipType __far *t = ship_type(type);
    if (!t)
        return P_FIGHTER;
    if (t->type >= 0x20)
        return P_CAPITAL;
    if (side == SIDE_TERRAN)
        return P_WINGMAN;
    switch (type) {
    case 0x0B: case 0x10: case 0x15: return P_INTERCEPTOR;   /* Agis, Shizu, Gaozong */
    case 0x0A: case 0x12: return P_BOMBER;   /* Areus, Taizong */
    case 0x0C: case 0x11: return P_HEAVY;    /* Cleomenes, Zhuangzong */
    case 0x0D: return P_ACE;                 /* Convolution */
    default: return P_FIGHTER;
    }
}

/* the target as a mind sees it */
typedef struct {
    RvVec3 rel;                         /* right, up, ahead, km */
    RvVec3 aim;                         /* its lead point the same way: where the guns' shots meet it */
    int32_t km;
    int16_t cos_nose;                   /* how straight my nose points at it (x 256) */
    int16_t cos_aim;                    /* at its lead point */
    int16_t cos_its;                    /* how straight ITS nose points at me */
    int32_t closing;                    /* how fast the two come together, km a step (negative: apart) */
} View;

static int16_t cos_of(const RvVec3 *v, int32_t len)
{
    return (int16_t)(len ? clamp32(v->z * 256 / len, -256, 256) : 256);
}

/* s's view of t: where it is, where its lead point is for s's guns (the first gun's speed), who points at whom */
static void view_of(int16_t s, int16_t t, int16_t lead, View *v)
{
    RvVec3 d, w, vt;
    int16_t g = gun_of(s);
    const RvWeapon __far *gw = g >= 0 ? weapon(GUN_TYPE(s, g)) : 0;
    int32_t tof, kps = gw && gw->speed_kps ? gw->speed_kps : 2000, alen;
    ship_rel(s, t, &v->rel);
    v->km = len_of(v->rel.x, v->rel.y, v->rel.z);
    v->cos_nose = cos_of(&v->rel, v->km);
    /* the lead: the target's move over the shot's flight, twice refined */
    vt = vec3(TO_KM(gVelocityVector[t].x), TO_KM(gVelocityVector[t].y), TO_KM(gVelocityVector[t].z));
    to_body(s, &vt, &w);
    tof = v->km / (kps / KPS_HZ);
    v->aim = vec3(v->rel.x + w.x * tof, v->rel.y + w.y * tof, v->rel.z + w.z * tof);
    tof = len_of(v->aim.x, v->aim.y, v->aim.z) / (kps / KPS_HZ);
    v->aim = vec3(v->rel.x + w.x * tof, v->rel.y + w.y * tof, v->rel.z + w.z * tof);
    v->aim = vec3(v->rel.x + (v->aim.x - v->rel.x) * lead / 100, v->rel.y + (v->aim.y - v->rel.y) * lead / 100,
                  v->rel.z + (v->aim.z - v->rel.z) * lead / 100);
    alen = len_of(v->aim.x, v->aim.y, v->aim.z);
    v->cos_aim = cos_of(&v->aim, alen);
    /* its nose on me: me from it, along its front */
    d = vec3(TO_KM(gPositionVector[s].x - gPositionVector[t].x), TO_KM(gPositionVector[s].y - gPositionVector[t].y),
             TO_KM(gPositionVector[s].z - gPositionVector[t].z));
    v->cos_its = (int16_t)(v->km ? clamp32(vec_dot(&d, &gOrientationFrontVector[t]) / v->km, -256, 256) : 0);
    /* closing: the relative velocity (positions a step) along the line from me to it, in km a step */
    w = vec3(TO_KM(gVelocityVector[s].x - gVelocityVector[t].x), TO_KM(gVelocityVector[s].y - gVelocityVector[t].y),
             TO_KM(gVelocityVector[s].z - gVelocityVector[t].z));
    v->closing = v->km ? -vec_dot(&w, &d) / v->km : 0;
}

/* the nearest living ship of another side (not cloaked), or of the capital ships only; -1 none */
static int16_t nearest_enemy(int16_t s, int16_t capital_only)
{
    int16_t k, best = -1;
    int32_t bd = INT32_C(0x7FFFFFFF), d;
    for (k = 0; k < NUM_SHIP_SLOTS; k++) {
        if (k == s || !slot_is_ship(k) || commAnimInfo[k] == commAnimInfo[s] || shipAiState[k] == 9 || (rvFlags[k] & 16))
            continue;
        if (capital_only && maybeEntityType[k] != ET_CAPSHIP)
            continue;
        d = km_between(s, k);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best;
}

/* the slot s keeps station on: its mission record's leader, else (on the player's side) the player; 0xFF none */
static uint8_t leader_of(int16_t s)
{
    int16_t k;
    if (slotMissionShip[s] < 64) {
        uint16_t lead = *(const uint16_t *)(MSHIP(slotMissionShip[s]) + 4);
        if (lead < 64)
            for (k = 0; k < NUM_SHIP_SLOTS; k++)
                if (k != s && slot_is_ship(k) && slotMissionShip[k] == lead)
                    return (uint8_t)k;
    }
    if (s != 0 && commAnimInfo[s] == commAnimInfo[0] && slot_is_ship(0))
        return 0;
    return 0xFF;
}

/* --- the steering: the nose toward a direction in the ship's own frame, as the profile allows --- */

#define FULL_AT 28                      /* a full steer this far off the way (1024 a turn: 10 degrees), less within */

static int8_t steer_of(int16_t angle)
{
    return (int8_t)clamp32((int32_t)angle * 9 / FULL_AT, -9, 9);
}

/* (dx right, dy up, dz ahead): g_turn[s].  The straight way is a yaw and a pitch at once; a profile that banks, or
 * one whose pulls do not allow the straight way, rolls until the target lies along an allowed pull (right, left,
 * up or down of the nose) and pulls there.  A roll of +t (the right wing up) turns a bearing in the right-up plane
 * by -t: to bring a bearing b to the pull c, roll by b - c */
/* the two hulls' half boxes together, km: the unit the passes and the avoidance are measured in */
static int32_t room_of(int16_t a, int16_t b)
{
    return TO_KM(collide_half(a) + collide_half(b));
}

/* collision avoidance: another ship (the mind's own target too, unless `target` says so) within the profile's avoid
 * distance and coming together with this one pushes the wanted direction sideways away from it, the harder the
 * nearer */
static void avoid(int16_t s, const AiProfile __far *p, uint8_t target, int32_t *dx, int32_t *dy)
{
    int16_t k;
    int32_t limit;
    RvVec3 r;
    if (!p->avoid_pct)
        return;
    for (k = 0; k < NUM_SHIP_SLOTS; k++) {
        if (k == s || k == target || !slot_is_ship(k))
            continue;
        limit = room_of(s, k) * p->avoid_pct / 100;
        if (labs(TO_KM(gPositionVector[k].x - gPositionVector[s].x)) > limit
            || labs(TO_KM(gPositionVector[k].y - gPositionVector[s].y)) > limit
            || labs(TO_KM(gPositionVector[k].z - gPositionVector[s].z)) > limit)
            continue;
        ship_rel(s, k, &r);             /* (any way round: one coming from behind is dodged too) */
        {
            int32_t d = len_of(r.x, r.y, r.z), away = limit - d, closing;
            RvVec3 dv;
            if (away <= 0)
                continue;
            /* only when coming together at a quarter of the top speed or more: a tail or an orbit at the same speed
             * is no collision coming, and the nose stays on the target */
            dv = vec3(TO_KM(gVelocityVector[s].x - gVelocityVector[k].x), TO_KM(gVelocityVector[s].y - gVelocityVector[k].y),
                      TO_KM(gVelocityVector[s].z - gVelocityVector[k].z));
            {
                RvVec3 w = vec3(TO_KM(gPositionVector[k].x - gPositionVector[s].x), TO_KM(gPositionVector[k].y - gPositionVector[s].y),
                                TO_KM(gPositionVector[k].z - gPositionVector[s].z));
                closing = d ? vec_dot(&dv, &w) / d : 0;
            }
            if (closing * KPS_HZ * 4 < (slot_type(s) ? slot_type(s)->max_kps : 300))
                continue;
            if (labs(r.x) + labs(r.y) < 10) {   /* (dead ahead: to the right) */
                *dx += away;
                continue;
            }
            *dx -= r.x * away / d;
            *dy -= r.y * away / d;
        }
    }
}

static void steer_to(int16_t s, const AiProfile __far *p, int32_t dx, int32_t dy, int32_t dz)
{
    static const int16_t __far k_pull_at[4] = { 0, 512, 256, 768 };   /* PULL_RIGHT, LEFT, UP, DOWN: their bearings */
    int32_t horiz, across;
    int16_t yaw, pitch, off, cap = (int16_t)((9 * p->turn_pct + 50) / 100);
    int16_t bearing, best = -1, bd = 1024, k, c, d, roll, pull;
    Turn __far *t = &g_turn[s];
    avoid(s, p, 0xFF, &dx, &dy);        /* (the target too: a pass goes round it, as the old game's did) */
    horiz = len_of(dx, 0, dz);
    across = len_of(dx, dy, 0);
    yaw = angle_of(dx, dz);
    pitch = angle_of(dy, horiz);
    off = angle_of(across, dz);
    bearing = angle_of(dy, dx);
    if (cap < 1)
        cap = 1;
    if (off < 0)
        off = (int16_t)-off;
    /* the straight way (a yaw and a pitch at once): a profile with every pull, unless it banks and the way is well
     * off the nose; a profile without them all only when it banks and the way is near the nose (the fine aim: the
     * old game's fighters shot at 0 degrees off); else the roll and the one pull */
    if ((p->pulls & PULL_ALL) == PULL_ALL ? (!p->bank || off <= 2 * FULL_AT) : (p->bank && off <= 2 * FULL_AT)) {
        t->x = (int8_t)clamp32(steer_of(yaw), -cap, cap);
        t->y = (int8_t)clamp32(steer_of(pitch), -cap, cap);
        t->roll = 0;
        return;
    }
    /* the allowed pull nearest the bearing */
    for (k = 0; k < 4; k++) {
        if (!(p->pulls & (1 << k)))
            continue;
        d = (int16_t)(bearing - k_pull_at[k]);
        d = (int16_t)(((d + 512) & 1023) - 512);   /* (the shorter way round) */
        if (d < 0)
            d = (int16_t)-d;
        if (d < bd || (d == bd && k == 2)) {
            bd = d;
            best = k;
        }
    }
    if (best < 0) {                     /* (a profile that may pull nowhere: it flies straight) */
        t->x = t->y = t->roll = 0;
        return;
    }
    c = (int16_t)(bearing - k_pull_at[best]);
    c = (int16_t)(((c + 512) & 1023) - 512);
    roll = steer_of(c);                 /* (toward the pull) */
    /* the pull: full when off the nose, eased in near it; only once the target is within a quarter turn of the pull */
    pull = (int16_t)((int32_t)steer_of(off) * lf_cos(c) / 16384);
    if (pull < 0)
        pull = 0;
    if (pull > cap)
        pull = cap;
    t->x = (int8_t)(best == 0 ? pull : best == 1 ? -pull : 0);
    t->y = (int8_t)(best == 2 ? pull : best == 3 ? -pull : 0);
    t->roll = (int8_t)roll;
}

/* --- the situation, the dice, and the tactics --- */

static int16_t situation(int16_t s, const Mind __far *m, const AiProfile __far *p, const View *v)
{
    if (p->flee_pct && hull_pct(s) < p->flee_pct)
        return SIT_HURT;
    if (v->rel.z < 0 && v->km < TAIL_KM && v->cos_its > 180)
        return SIT_TAILED;
    if (m->target != 0xFF && v->km < room_of(s, m->target) * 4 && (v->rel.z < 0 || v->cos_nose < 100))
        return SIT_PASSED;              /* (just passed, or about to: within four hulls and off the nose) */
    if (v->km < (int32_t)p->fire_km * 4 / 3 && v->cos_nose > 0)
        return SIT_NEAR;
    return SIT_FAR;
}

/* a tactic's length, steps */
static int16_t tactic_steps(int16_t s, int16_t tactic)
{
    const RvShipType __far *t = slot_type(s);
    switch (tactic) {
    case T_PURSUE: return 8 * LOGIC_HZ;
    case T_HEADON: case T_STRAFE: case T_TORPEDO: return 20 * LOGIC_HZ;
    case T_SIT: return 3 * LOGIC_HZ;
    case T_BREAK: return LOGIC_HZ;
    case T_EVADE: return 4 * LOGIC_HZ;
    case T_LOOP: return (int16_t)(t && t->pitch_dps ? INT32_C(360) * KPS_HZ / t->pitch_dps + 4 : 60);
    case T_FLEE: return 6 * LOGIC_HZ;
    case T_CRUISE: case T_FORMATION: case T_ESCORT: return 2 * LOGIC_HZ;
    default: return 0x7FFF;             /* (T_RAM, T_WAIT: until something else) */
    }
}

/* a tactic taken up: its timer, its way and its afterburner */
static void take_tactic(int16_t s, Mind __far *m, const AiProfile __far *p, int16_t tactic)
{
#ifdef RV_HOST
    if (getenv("RV_TACTICS")) {
        static const char __far f[] = "tactic %u: slot %d %d -> %d (target %d, km %ld, passing %d)";
        rv_logf(f, (unsigned)frameCounter, s, m->tactic, tactic, m->target, m->target != 0xFF ? km_between(s, m->target) : -1L, m->passing);
    }
#endif
    m->tactic = (uint8_t)tactic;
    m->phase = 0;
    m->timer = tactic_steps(s, tactic);
    m->dir = (int8_t)(game_rand() & 1 ? 1 : -1);
    m->burn = (uint8_t)(p->ab_pct && game_rand() % 100 < p->ab_pct);
    if (tactic == T_WAIT) {
        m->timer = (int16_t)(p->wait_s * LOGIC_HZ);
        rvFlags[s] |= 16;               /* (cloaked: unseen by the others' minds) */
    }
}

/* the dice over the profile's picks for a situation (a torpedo run only with a torpedo and a capital ship to run at:
 * else a pursuit) */
static void pick_tactic(int16_t s, Mind __far *m, const AiProfile __far *p, int16_t sit)
{
    const Pick __far *row = p->pick[sit];
    int16_t k, sum = 0, tactic = T_PURSUE;
    uint16_t r;
    for (k = 0; k < PICKS; k++)
        sum = (int16_t)(sum + row[k].weight);
    if (sum) {
        r = (uint16_t)(game_rand() % sum);
        for (k = 0; k < PICKS; k++) {
            if (r < row[k].weight) {
                tactic = row[k].tactic;
                break;
            }
            r = (uint16_t)(r - row[k].weight);
        }
    }
    if (tactic == T_TORPEDO && (launcher_of(s) < 0 || m->target == 0xFF || maybeEntityType[m->target] != ET_CAPSHIP))
        tactic = T_PURSUE;
    take_tactic(s, m, p, tactic);
}

/* the throttle, through the thunk, and the afterburner */
static void throttle(int16_t s, int32_t kps, int16_t burn)
{
    const RvShipType __far *t = slot_type(s);
    int32_t most = t ? t->max_kps : 300;
    aiSetSpeed((uint16_t)s, (uint16_t)clamp32(kps, 0, most));
    rvAfterburner[s] = (uint8_t)(burn && t && t->ab_kps ? 1 : 0);
    if (!rvAfterburner[s] && speedLo[s] > (uint32_t)most)   /* (off the burner, an AI is at its top speed at once) */
        speedLo[s] = (uint32_t)most;
    if (rvAfterburner[s] && speedLo[s] < (uint32_t)t->ab_kps)   /* (on it, at its burner speed at once: the old game's
                                                                   fighters dashed to 3x in a frame; the player's ramps) */
        speedLo[s] = (uint32_t)t->ab_kps;
}

/* the guns, when the lead point is in the cone and the target in range; a missile after a lock */
static void aim_and_fire(int16_t s, Mind __far *m, const AiProfile __far *p, const View *v, int16_t guns)
{
    int16_t g = gun_of(s), l;
    const RvWeapon __far *w = g >= 0 ? weapon(GUN_TYPE(s, g)) : 0;
    int32_t reach = w ? w->range_km : 0;
    if (reach > p->fire_km)
        reach = p->fire_km;
    if (m->gap < 255)
        m->gap++;
    if (guns && w && v->km <= reach && v->cos_aim >= p->aim_cos && gunEnergy[s] > 0 && m->gap >= p->fire_gap) {
        m->fire |= FIRE_GUNS;
        m->gap = 0;
    }
    /* a lock: the target in the cone for the weapon's lock time, then the dice, then the launch */
    if ((l = launcher_of(s)) >= 0 && (w = weapon(GUN_TYPE(s, l))) != 0 && v->cos_nose >= LOCK_CONE
        && v->km <= w->range_km && (uint16_t)(frameCounter - m->since) >= RELAUNCH_STEPS) {
        if (++m->lock >= (uint16_t)(w->lock_tenths * 3 + 1)) {
            m->lock = 0;
            m->since = frameCounter;
            if (game_rand() % 100 < p->missile_pct) {
                m->fire |= FIRE_MISSILE;
                m->launcher = (uint8_t)l;
            }
        }
    } else {
        m->lock = 0;
    }
}

/* T_FORMATION: station on the leader (formationOffset, km in its frame: taken from where the ship is when it has
 * none), the leader's heading once there, and a throttle that closes the gap and then matches */
static void formation_step(int16_t s, Mind __far *m, const AiProfile __far *p)
{
    int16_t L = m->leader;
    int16_t *off = (int16_t *)(formationOffset + 6 * s);
    RvVec3 st, d, b;
    int32_t km, along, kps;
    const RvVec3 *R, *U, *F;
    if (L == 0xFF || !slot_is_ship(L) || shipAiState[L] == 9) {
        throttle(s, slot_type(s) ? slot_type(s)->cruise_kps : 200, 0);
        return;
    }
    R = &gOrientationRightVector[L];
    U = &gOrientationUpVector[L];
    F = &gOrientationFrontVector[L];
    if (!off[0] && !off[1] && !off[2]) {   /* its place: where it is now off the leader, at least 800 km to a side */
        ship_rel(L, s, &b);
        off[0] = (int16_t)clamp32(b.x, -6000, 6000);
        off[1] = (int16_t)clamp32(b.y, -6000, 6000);
        off[2] = (int16_t)clamp32(b.z, -6000, 6000);
        if (labs(off[0]) < 800 && labs(off[1]) < 800)
            off[0] = (int16_t)(off[0] < 0 ? -800 : 800);
    }
    st = vec3(gPositionVector[L].x + (FIX_MUL(R->x, off[0]) + FIX_MUL(U->x, off[1]) + FIX_MUL(F->x, off[2])) * KM,
              gPositionVector[L].y + (FIX_MUL(R->y, off[0]) + FIX_MUL(U->y, off[1]) + FIX_MUL(F->y, off[2])) * KM,
              gPositionVector[L].z + (FIX_MUL(R->z, off[0]) + FIX_MUL(U->z, off[1]) + FIX_MUL(F->z, off[2])) * KM);
    d = vec3(TO_KM(st.x - gPositionVector[s].x), TO_KM(st.y - gPositionVector[s].y), TO_KM(st.z - gPositionVector[s].z));
    km = len_of(d.x, d.y, d.z);
    along = vec_dot(&d, F) / 256;       /* (the gap along the leader's way: behind it, or ahead of it) */
    if (km > 200) {
        to_body(s, &d, &b);
        steer_to(s, p, b.x, b.y, b.z);
    } else {
        RvVec3 f = *F;
        to_body(s, &f, &b);
        steer_to(s, p, b.x, b.y, b.z);
    }
    kps = speedLo[L];
    if (km > 4000)
        kps = 9999;
    else if (along > 0)
        kps += along / 8 + 20;
    else
        kps += along / 8;
    throttle(s, kps, km > 8000 && m->burn);
}

/* T_ESCORT: round the leader at a distance, a turn every so often */
static void escort_step(int16_t s, Mind __far *m, const AiProfile __far *p)
{
    int16_t L = m->leader, a = (int16_t)((frameCounter / 4) & 1023);
    RvVec3 st, d, b;
    const RvVec3 *R, *F;
    if (L == 0xFF || !slot_is_ship(L) || shipAiState[L] == 9) {
        formation_step(s, m, p);
        return;
    }
    R = &gOrientationRightVector[L];
    F = &gOrientationFrontVector[L];
    st = vec3(gPositionVector[L].x + (FIX_MUL(R->x, lf_cos(a) / 64 * 2500 / 256) + FIX_MUL(F->x, lf_sin(a) / 64 * 2500 / 256)) * KM,
              gPositionVector[L].y,
              gPositionVector[L].z + (FIX_MUL(R->z, lf_cos(a) / 64 * 2500 / 256) + FIX_MUL(F->z, lf_sin(a) / 64 * 2500 / 256)) * KM);
    d = vec3(TO_KM(st.x - gPositionVector[s].x), TO_KM(st.y - gPositionVector[s].y), TO_KM(st.z - gPositionVector[s].z));
    to_body(s, &d, &b);
    steer_to(s, p, b.x, b.y, b.z);
    throttle(s, slot_type(s) ? slot_type(s)->cruise_kps : 200, 0);
}

/* a pass is coming: closing at a tenth of the top speed or more (a target tailed at its own speed is aimed at, a
 * slow overtake is a pass) */
static int16_t passing(int16_t s, const View *v)
{
    const RvShipType __far *t = slot_type(s);
    return v->closing * KPS_HZ * 10 >= (t ? t->max_kps : 300);
}

/* an attack aims beside its target rather than through it: by a lane (the attackers of one target, in slot order,
 * spread across it a hull's width apart, so a wing does not fly together into one point and into each other), and
 * near the target by the tactic's own side (a pass goes by the two half boxes and a margin): the old games' fighters
 * pass close by, they seldom collide */
static int32_t pass_side(int16_t s, const Mind __far *m, const AiProfile __far *p, const View *v)
{
    int32_t room, lane = 0, n = 0, rank = 0;
    int16_t k;
    if (m->target == 0xFF)
        return 0;
    room = room_of(s, m->target) * p->pass_pct / 100;
    for (k = 0; k < NUM_SHIP_SLOTS; k++)
        if (k != s && slot_is_ship(k) && rvController[k] == CTL_AI && shipAiState[k] != 9 && g_mind[k].target == m->target
            && commAnimInfo[k] == commAnimInfo[s] && maybeEntityType[k] == ET_SHIP) {
            n++;
            if (k < s)
                rank++;
        }
    if (n)                              /* (lanes -n/2 .. n/2 about the target) */
        lane = (rank * 2 - n) * room / 2;
    if (passing(s, v) && v->km < v->closing * LOGIC_HZ)   /* (the pass within a second: by its own side of the target) */
        lane += (int32_t)m->dir * room;
    return lane;
}

/* the tactic's step: the nose, the throttle, the fire; 1 when it is over before its time */
static int16_t run_tactic(int16_t s, Mind __far *m, const AiProfile __far *p, const View *v)
{
    const RvShipType __far *t = slot_type(s);
    int32_t cruise = t ? t->cruise_kps : 200, max = t ? t->max_kps : 300;
    switch (m->tactic) {
    case T_PURSUE:                      /* at its lead point; closing, then matching; off before a collision */
        steer_to(s, p, v->aim.x + pass_side(s, m, p, v), v->aim.y, v->aim.z);
        /* (closing at full speed, head on too; behind it within the guns' range, at the profile's tail speed; past
         * it, full speed round) */
        throttle(s, v->km > p->fire_km || v->rel.z < 0 || v->cos_its >= 0 ? max : max * p->tail_pct / 100, v->km > 5000 && m->burn);
        aim_and_fire(s, m, p, v, 1);
        if (p->break_km && v->km < p->break_km && v->cos_nose > 0 && m->passing) {
            take_tactic(s, m, p, T_BREAK);   /* (the old game's fighters: straight at it, then a hard flat yaw away) */
            return 0;
        }
        return v->rel.z < 0 && v->km < TAIL_KM;
    case T_HEADON:                      /* straight at it at full speed, firing; off at the break distance */
    case T_STRAFE:                      /* the same, but through: it keeps going until it has passed */
        steer_to(s, p, v->aim.x + pass_side(s, m, p, v), v->aim.y, v->aim.z);
        throttle(s, max, m->burn && v->km > 4000);
        aim_and_fire(s, m, p, v, 1);
        if (p->break_km && v->km < p->break_km && (m->tactic == T_HEADON || (m->passing && v->cos_its < 0))) {
            take_tactic(s, m, p, T_BREAK);   /* (a strafe goes through a head-on pass, not through a tail) */
            return 0;
        }
        return v->rel.z < 0;            /* (through the pass: the target behind, the dice again) */
    case T_SIT:                         /* still, turning onto it, firing */
        steer_to(s, p, v->aim.x, v->aim.y, v->aim.z);
        throttle(s, 0, 0);
        aim_and_fire(s, m, p, v, 1);
        return 0;
    case T_RAM:                         /* at it, as fast as it goes, never off */
        steer_to(s, p, v->rel.x, v->rel.y, v->rel.z);
        throttle(s, max, 1);
        aim_and_fire(s, m, p, v, 1);
        return 0;
    case T_TORPEDO: {                   /* at a capital ship until a torpedo is away, then off */
        int16_t l = launcher_of(s);
        steer_to(s, p, v->aim.x, v->aim.y, v->aim.z);
        throttle(s, v->km > 4000 ? max : cruise, 0);
        aim_and_fire(s, m, p, v, v->km < 2000);
        if (m->fire & FIRE_MISSILE) {
            take_tactic(s, m, p, T_BREAK);
            return 0;
        }
        return l < 0 || (p->break_km && v->km < p->break_km);
    }
    case T_BREAK: {                     /* a hard turn one way at full throttle: a flat yaw (away from a target still
                                           ahead, after it once it is behind: the old game's fighters) for a profile
                                           that yaws, a pull with a roll toward the target's side for one that banks
                                           and may pull up or down; the afterburner for the profile's burst at its
                                           start */
        int16_t at = (int16_t)(tactic_steps(s, T_BREAK) - m->timer), cap = (int16_t)((9 * p->turn_pct + 50) / 100);
        int16_t flat = !(p->bank && (p->pulls & (PULL_UP | PULL_DOWN)));
        if (!m->phase) {
            m->phase = 1;
            if (m->target != 0xFF)
                m->dir = (int8_t)(flat ? ((v->rel.x < 0) == (v->rel.z > 0) ? 1 : -1) : (v->rel.x < 0 ? -1 : 1));
        }
        if (!flat) {
            g_turn[s].y = (int8_t)(cap * m->dir);
            g_turn[s].roll = (int8_t)(5 * m->dir);
        } else {
            g_turn[s].x = (int8_t)(cap * m->dir);
        }
        throttle(s, max, at < p->burst);   /* (the dash: the old game's fighters lit it at every pass) */
        return m->target != 0xFF && v->rel.z < 0 && v->cos_nose >= 230 ? 0 : (m->target != 0xFF && v->cos_nose >= 230 && at > 2);
        /* (a turn back toward a target that is behind goes on blind; one that has the nose on a target ahead again is
         * over: the pursuit takes it from there, and breaks away again if it is too near) */
    }
    case T_EVADE: {                     /* jinks: a pull each way with a roll, a stop by chance, then away */
        int16_t at = (int16_t)(tactic_steps(s, T_EVADE) - m->timer);
        if (at < LOGIC_HZ) {
            g_turn[s].y = (int8_t)(9 * m->dir);
            g_turn[s].roll = 9;
            throttle(s, max, 0);
        } else if (at < 2 * LOGIC_HZ) {
            g_turn[s].y = (int8_t)(-9 * m->dir);
            g_turn[s].roll = -9;
            throttle(s, max, 0);
        } else if (m->burn) {           /* (the stop: the pursuer overshoots) */
            g_turn[s].x = (int8_t)(5 * m->dir);
            throttle(s, at < 3 * LOGIC_HZ ? 0 : max, at >= 3 * LOGIC_HZ);
        } else {
            g_turn[s].y = 9;
            g_turn[s].roll = (int8_t)(4 * m->dir);
            throttle(s, max, 1);
        }
        return v->rel.z > 0 && v->km > TAIL_KM;
    }
    case T_LOOP:                        /* a whole loop, nose up */
        g_turn[s].y = 9;
        throttle(s, cruise, 0);
        return 0;
    case T_FLEE:                        /* away from it, as fast as it goes */
        steer_to(s, p, -v->rel.x, -v->rel.y, -v->rel.z);
        throttle(s, max, m->burn);
        return 0;
    default:
        return 1;
    }
}

/* a bump (step_collisions): a fighter breaks away from it, a capital ship holds its course */
static void bump_away(int16_t s, int8_t dir)
{
    Mind __far *m = &g_mind[s];
    if (maybeEntityType[s] != ET_SHIP || shipAiState[s] == 9 || m->tactic == T_BREAK)
        return;
    take_tactic(s, m, &k_profiles[m->profile < P_COUNT ? m->profile : P_FIGHTER], T_BREAK);
    m->phase = 1;                       /* (its way is the bump's, not the target's side) */
    m->dir = dir;
    m->burn = 1;
}

/* a fighter's mind for a step: its target, its situation, its tactic, its decisions (g_turn, the throttle, fire) */
static void fighter_think(int16_t s)
{
    Mind __far *m = &g_mind[s];
    const AiProfile __far *p = &k_profiles[m->profile < P_COUNT ? m->profile : P_FIGHTER];
    View v;
    int16_t sit, k, near_ = -1;
    int32_t d, bd = INT32_C(0x7FFFFFFF);
    m->leader = leader_of(s);
    if (m->tactic == T_IDLE && p->idle == T_WAIT)   /* (its first thought: one that waits lies in wait from the start) */
        take_tactic(s, m, p, T_WAIT);
    /* the target: kept while it lives and stays in reach, else the nearest enemy within the engage distance */
    if (m->target != 0xFF && (!slot_is_ship(m->target) || shipAiState[m->target] == 9 || commAnimInfo[m->target] == commAnimInfo[s]
                              || km_between(s, m->target) > (int32_t)p->engage_km * 3 / 2))
        m->target = 0xFF;
    for (k = 0; k < NUM_SHIP_SLOTS; k++) {
        if (k == s || !slot_is_ship(k) || commAnimInfo[k] == commAnimInfo[s] || shipAiState[k] == 9 || (rvFlags[k] & 16))
            continue;
        d = km_between(s, k);
        if (d < bd) {
            bd = d;
            near_ = k;
        }
    }
    if (near_ >= 0 && bd <= p->engage_km && (m->target == 0xFF || bd * 3 < km_between(s, m->target) * 2))
        m->target = (uint8_t)near_;
    rvTarget[s] = m->target;
    if (m->target == 0xFF) {            /* nobody to fight: the idle tactic */
        if (m->tactic != p->idle) {
            take_tactic(s, m, p, p->idle);
            m->timer = tactic_steps(s, p->idle);
            if (p->idle == T_WAIT)
                m->timer = (int16_t)(p->wait_s * LOGIC_HZ);
        }
        if (m->tactic == T_FORMATION)
            formation_step(s, m, p);
        else if (m->tactic == T_ESCORT)
            escort_step(s, m, p);
        else if (m->tactic == T_WAIT) {
            throttle(s, 0, 0);
            if (--m->timer <= 0)
                rvFlags[s] &= ~16;      /* (its patience over: it shows itself and flies on) */
        } else
            throttle(s, slot_type(s) ? slot_type(s)->cruise_kps : 200, 0);
        return;
    }
    view_of(s, m->target, p->lead_pct, &v);
    m->passing = (uint8_t)passing(s, &v);
    if (m->tactic == T_WAIT) {          /* still until a chance: the target near and not pointing at it, or very near */
        throttle(s, 0, 0);
        if (--m->timer > 0 && !(v.km < p->pounce_km && (v.cos_its < 100 || v.km < p->pounce_km / 2)))
            return;
        rvFlags[s] &= ~16;
        take_tactic(s, m, p, T_PURSUE);
        m->burn = 1;
    }
    sit = situation(s, m, p, &v);
    if (m->tactic < T_PURSUE || m->timer <= 0)
        pick_tactic(s, m, p, sit);
    m->timer--;
    if (run_tactic(s, m, p, &v))
        pick_tactic(s, m, p, sit);
}

/* a capital ship's mind for a step: its course held at cruise; its turrets at the nearest enemy in their range (each
 * that is cooled: fired by step_fire through the thunk, aimed at rvTarget by the gun's own code); a launcher at an
 * enemy capital ship now and then */
static void ship_think(int16_t s)
{
    Mind __far *m = &g_mind[s];
    const RvShipType __far *t = slot_type(s);
    int16_t near_ = nearest_enemy(s, 0), g;
    throttle(s, t ? t->cruise_kps : 100, 0);
    m->target = (uint8_t)(near_ >= 0 ? near_ : 0xFF);
    rvTarget[s] = m->target;
    if (near_ < 0)
        return;
    for (g = 0; g < GUN_COUNT(s) && g < 16; g++) {
        const RvWeapon __far *w = weapon(GUN_TYPE(s, g));
        if (!w || (GUN_STATE(s, g) & GUN_DESTROYED))
            continue;
        if (w->kind == RV_WK_TURRET) {
            if (!(GUN_STATE(s, g) & 0x7F) && km_between(s, near_) <= w->range_km)
                m->turrets |= (uint16_t)(1u << g);
        } else if (w->kind == RV_WK_TORPEDO || w->kind == RV_WK_MISSILE) {
            int16_t cap = nearest_enemy(s, 1);
            if (cap >= 0 && (GUN_STATE(s, g) & 0x7F) && km_between(s, cap) <= w->range_km
                && (uint16_t)(frameCounter - m->since) >= (uint16_t)(10 * LOGIC_HZ)) {
                m->since = frameCounter;
                rvTarget[s] = (uint8_t)cap;
                m->fire |= FIRE_MISSILE;
                m->launcher = (uint8_t)g;
            }
        }
    }
}

/* 4. every ship flies its step: its turn applied to its orientation (vec_turn, then ship_orthonormalise), its speed
 * toward setSpeed, gVelocityVector (positions a step: KPS_STEP), the velocity added to gPositionVector.  A puppet
 * only coasts (position += velocity) */
static void step_motion(void)
{
    uint16_t phase = frameCounter % KPS_HZ;
    int16_t ship_index;
    for (ship_index = 0; ship_index < NUM_SHIP_SLOTS; ship_index +=1){
        if (!slot_is_ship(ship_index) || rvController[ship_index] == CTL_FROZEN) {
            continue;
        }
        if (rvController[ship_index] != CTL_PUPPET) {
            step_turn(ship_index, g_turn[ship_index].x, g_turn[ship_index].y, g_turn[ship_index].roll);
            step_speed(ship_index, phase);
            reduce_slide(ship_index);
        }
        {
            const RvVec3 *velocity = &gVelocityVector[ship_index];
            RvVec3 *position = &gPositionVector[ship_index];
            position->x += velocity->x;
            position->y += velocity->y;
            position->z += velocity->z;
        }
    }
}

/* 5. the shots fly (shots_move) and hit (shot_hit: the ship and the place; do_damage through its thunk with
 * shot_source(i) as the source; shot_remove).  Backwards: a removed shot's index goes to the last one */
static void step_shots(void)
{
    int16_t i, slot;
    RvVec3 where;
    shots_move();
    for (i = shots_count() - 1; i >= 0; i--)
        if (shot_hit(i, &slot, &where)) {
            (void)do_damage((uint16_t)shot_source(i), (uint16_t)slot, shot(i)->damage, RV_WHERE(&where));
            shot_remove(i);
        }
}

/* 6. missiles (ship slots of ET_MISSILE: they steer like ships, home on missileTarget, burst by a proximity test) */
static void step_missiles(void)
{
    int16_t s;
    for (s = 1; s < NUM_SHIP_SLOTS; s++)
        if (maybeEntityType[s] == ET_MISSILE)
            missile_step(s);
}

/* 7. ships into ships (collide_ships: the push that parts them, the damage both ways through do_damage) */
static void step_collisions(void)
{
    int16_t a, b;
    for (a = 0; a < NUM_SHIP_SLOTS - 1; a++) {
        if (!slot_is_ship(a))
            continue;
        for (b = a + 1; b < NUM_SHIP_SLOTS; b++)   /* (a dying ship is still a hull: the others bump into it) */
            if (slot_is_ship(b) && collide_coarse(a, b))
                ram(a, b);
    }
}

/* 8. the dying (shipAiState 9: an explosion's time, then despawn) and the player's end (missionStatus 4) */
static void step_deaths(void)
{
    int16_t s;
    for (s = 0; s < NUM_SHIP_SLOTS; s++) {
        if (!slot_is_ship(s) || shipAiState[s] != 9) {
            g_died[s] = 0;
            continue;
        }
        if (!g_died[s]) {               /* seen now: its explosion begins (fx.c shows it); it drifts to a stop */
            g_died[s] = (uint16_t)(frameCounter ? frameCounter : 1);
            setSpeed[s] = 0;
            continue;
        }
        if ((uint16_t)(frameCounter - g_died[s]) < (maybeEntityType[s] == ET_CAPSHIP ? DEATH_STEPS_CAPITAL : DEATH_STEPS))
            continue;
        if (s == 0)                     /* the player's end: the flight ends at the loop's next check */
            missionStatus = 4;
        else
            despawn((uint16_t)s);
    }
}

/* 9. the mission: nav points reached (enterNavPoint), waves, objectives, the score (victoryPoints, nonzero won).
 * A kill is the mission ship seen dying; the score is the mission done, as it stands (logic_end has the last word) */
static void step_mission(void)
{
    int16_t s, n;
    for (s = 1; s < NUM_SHIP_SLOTS; s++)
        if (slot_is_ship(s) && shipAiState[s] == 9 && slotMissionShip[s] < 64)
            g_killed[slotMissionShip[s]] = 1;
    if (slot_is_ship(0) && shipAiState[0] != 9)
        for (n = 0; n < NAVS; n++)
            if (nav_defined(n) && !(g_navs_seen >> n & 1) && nav_km(n) < NAV_REACH_KM)
                enterNavPoint((uint16_t)n);
    victoryPoints = (uint16_t)mission_done();
}

/* --- over the view: a band of rows at a time (hud.h's calls are clipped to it and to the opening) --- */

#define HUD_BRACKET_MIN 4               /* brackets no tighter than this about the target's middle (px): far off, its
                                           box is a dot */

static void hud_marks(void)
{
    RvCamera cam;
    EntBox b;
    int16_t half;
    flight_camera(&cam);
    g_hud.cx = cam.cx;
    g_hud.cy = cam.cy;
    g_hud.arm = (int16_t)((cam.vx1 - cam.vx0) >> 6);   /* (10 px at 640 across, 5 at 320) */
    g_hud.target = g_sys.target >= 0 && flight_ent_box(g_sys.target, &cam, &b) && b.visible;
    if (!g_hud.target)
        return;
    half = b.half > HUD_BRACKET_MIN ? b.half : HUD_BRACKET_MIN;   /* (b.half: steady as it turns) */
    g_hud.x0 = (int16_t)(b.sx - half);
    g_hud.y0 = (int16_t)(b.sy - half);
    g_hud.x1 = (int16_t)(b.sx + half + 1);
    g_hud.y1 = (int16_t)(b.sy + half + 1);
    g_hud.colour = commAnimInfo[g_sys.target] == commAnimInfo[0] ? UI_HILITE : UI_RED;
}

void logic_frame(void)
{
    step_timers();
    step_minds();
    step_fire();
    step_motion();
    step_shots();
    step_missiles();
    step_collisions();
    step_deaths();
    step_mission();
    hud_marks();
}

/* the gunsight at the view's middle, and brackets round the target (red an enemy, cyan a friend) */
void logic_hud(void)
{
    if (!g_hud.arm)
        return;
    hud_cross(g_hud.cx, g_hud.cy, g_hud.arm, UI_AMBER2);
    if (g_hud.target)
        hud_brackets(g_hud.x0, g_hud.y0, g_hud.x1, g_hud.y1, g_hud.colour);
}

/* the final score: won when the player landed (missionStatus 1) home (at nav 0) with the mission done; dead,
 * ejected, landed elsewhere or with it undone, lost */
void logic_end(void)
{
    victoryPoints = (uint16_t)(missionStatus == 1 && mission_done() && nav_km(0) < NAV_REACH_KM);
}

#define AUTOPILOT_CLEAR_KM 8000         /* no enemy this near (the engine's autopilot lamp says the same: flight.c) */
#define AUTOPILOT_SHORT_KM 3000         /* it stops this far short of the nav point, facing it */
#define AUTOPILOT_WING_KM 10000         /* the friendly fighters this near come along, keeping their places */

/* the autopilot key (A), between two steps: with no enemy near, the player (and the wing) taken on to the next nav
 * point, as the old games cut to the arrival; 1 when it went, 0 when it may not.  (On a multiplayer client the layer
 * steps over the key's call: the host's autopilot moves everyone).  The next nav point is the first not reached
 * yet, home (nav 0) when all are; the player arrives AUTOPILOT_SHORT_KM short of it, facing it */
int16_t logic_autopilot(void)
{
    RvVec3 d, to;
    int32_t horiz;
    int16_t s, n, next = 0, yaw, pitch;
    if (!slot_is_ship(0) || shipAiState[0] == 9)
        return 0;
    for (s = 1; s < NUM_SHIP_SLOTS; s++)
        if (slot_is_ship(s) && commAnimInfo[s] != commAnimInfo[0] && shipAiState[s] != 9
            && player_km(s) < AUTOPILOT_CLEAR_KM)
            return 0;
    for (n = 1; n < NAVS && !next; n++)
        if (nav_defined(n) && !(g_navs_seen >> n & 1))
            next = n;
    if (nav_km(next) <= AUTOPILOT_SHORT_KM)
        return 0;                       /* (there already) */
    /* the way there, km; its level part's length in 16 km steps (the squares of km overflow) */
    d = vec3(NAV_AT(next, 0) - TO_KM(gPositionVector[0].x), NAV_AT(next, 1) - TO_KM(gPositionVector[0].y),
             NAV_AT(next, 2) - TO_KM(gPositionVector[0].z));
    horiz = (int32_t)lf_sqrt((uint32_t)((d.x >> 4) * (d.x >> 4) + (d.z >> 4) * (d.z >> 4))) << 4;
    yaw = lf_atan2(d.x, d.z);
    pitch = lf_atan2(d.y, horiz);
    ship_face(0, yaw, pitch);
    to = vec3((NAV_AT(next, 0) - FIX_MUL(gOrientationFrontVector[0].x, AUTOPILOT_SHORT_KM)) * KM,
              (NAV_AT(next, 1) - FIX_MUL(gOrientationFrontVector[0].y, AUTOPILOT_SHORT_KM)) * KM,
              (NAV_AT(next, 2) - FIX_MUL(gOrientationFrontVector[0].z, AUTOPILOT_SHORT_KM)) * KM);
    /* the wing: moved as the player is, facing the same way (player_km while the player is still where it was) */
    for (s = 1; s < NUM_SHIP_SLOTS; s++)
        if (slot_is_ship(s) && maybeEntityType[s] == ET_SHIP && commAnimInfo[s] == commAnimInfo[0]
            && rvController[s] == CTL_AI && shipAiState[s] != 9 && player_km(s) < AUTOPILOT_WING_KM) {
            gPositionVector[s].x += to.x - gPositionVector[0].x;
            gPositionVector[s].y += to.y - gPositionVector[0].y;
            gPositionVector[s].z += to.z - gPositionVector[0].z;
            ship_face(s, yaw, pitch);
            fly_ahead(s);
        }
    gPositionVector[0] = to;
    fly_ahead(0);
    if (next)
        enterNavPoint((uint16_t)next);
    return 1;
}

/* --- the thunks' bodies (abi/rvabi.def, spec 5; docs/logic.md has each one's contract) --- */

/* (src, dst, qty, where): a hit of qty on the ship in slot dst at the place `where` (a near RvVec3, positions):
 * returns what it did, 0 nothing.  The player takes none while playerCanBeDamaged is 0.  Everything that hurts goes
 * through here (shots, rocks: rocks.c calls it, rams), and the layer replays a hit on the other machines with src a
 * bolt of the shooter's, the shooter's ship or 0xFFFF: what it does must not depend on src */
/* a store (a shield, an armour) takes what it can of a hit, down to 0: what's left over */
static uint16_t spend(uint16_t *store, uint16_t qty)
{
    uint16_t took = *store < qty ? *store : qty;
    *store = (uint16_t)(*store - took);
    return (uint16_t)(qty - took);
}

/* a hit of `left` through to the player's hull (of `hull` in all) hurts a system with odds left / hull: which one,
 * and whether, from game_rand (the layer re-seeds it to replay a hit).  A point of its systemDamage, and its overlay */
static void hurt_system(uint16_t left, uint16_t hull)
{
    uint16_t k;
    if (game_rand() % hull >= left)
        return;
    k = game_rand() % SYS_COUNT;
    if (systemDamage[k] < SYS_WRECKED)
        systemDamage[k]++;
    rvCockpitDamage |= 1UL << k_sys_overlay[k];
}

uint16_t __far __cdecl do_damage_impl(uint16_t src, uint16_t dst, uint16_t qty, uint16_t where)
{
    const RvShipType __far *t;
    const RvVec3 *w;
    RvVec3 d;
    int32_t f, r;
    int16_t sh, ar;
    uint16_t left;
    (void)src;                          /* (blame and effects only: the amount must not depend on it) */
    if (!slot_is_ship((int16_t)dst) || !qty || (dst == 0 && !playerCanBeDamaged) || (rvFlags[dst] & 1)
        || shipAiState[dst] == 9 || (t = slot_type((int16_t)dst)) == 0)
        return 0;
    /* which side: the hit's place from the ship's middle, in km (in positions the products overflow), along its
     * front and its right.  Front or rear picks the shield; the larger of along and across, the armour */
    w = RV_WHERE_VEC(where);
    d = vec3(TO_KM(w->x - gPositionVector[dst].x), TO_KM(w->y - gPositionVector[dst].y),
             TO_KM(w->z - gPositionVector[dst].z));
    f = vec_dot(&d, &gOrientationFrontVector[dst]);
    r = vec_dot(&d, &gOrientationRightVector[dst]);
    sh = f >= 0 ? 0 : 1;
    ar = labs(f) >= labs(r) ? sh : r >= 0 ? 3 : 2;
    /* that shield, then that armour, then the hull: its end is the ship's (shipAiState 9, the layer's sign of a
     * death; step_deaths takes it from there) */
    left = spend(&ARMOR(dst, ar), spend(&SHIELD(dst, sh), qty));
    if (left) {
        if (damagePoints[dst] >= t->hull || left >= t->hull - damagePoints[dst]) {
            damagePoints[dst] = t->hull;
            shipAiState[dst] = 9;
        } else {
            damagePoints[dst] = (uint16_t)(damagePoints[dst] + left);
        }
        if (dst == 0 && t->hull)
            hurt_system(left, t->hull);
    }
    return qty;
}

/* (ship, gun): gun record `gun` of the ship fires: a gun or a turret puts a shot in the world (shot_add, from its
 * mount: GUN_MOUNT_X / Y), a launcher a missile into a free ship slot.  It fires whatever its cooldown and the
 * energy: the deciding is the caller's (the layer replays this call on the other machines with the gun record as
 * the shooter's machine had it, so it must not refuse there); it sets the cooldown and spends the energy itself.
 * Returns a missile's slot (the layer maps it), else 0xFFFF */
uint16_t __far __cdecl fireGunFromShip_impl(uint16_t ship, uint16_t gun)
{
    const RvWeapon __far *w;
    if (!slot_is_ship((int16_t)ship) || gun >= GUN_COUNT(ship) || (w = weapon(GUN_TYPE(ship, gun))) == 0)
        return 0xFFFF;
    if (w->kind == RV_WK_MISSILE || w->kind == RV_WK_TORPEDO || w->kind == RV_WK_ROCKET)
        return missile_launch(ship, gun, w);
    return gun_fire(ship, gun, w);
}

/* (ship): every one of its guns that is ready, fired through the fireGunFromShip thunk */
void __far __cdecl maybe_fire_all_guns_impl(uint16_t ship)
{
    if (slot_is_ship((int16_t)ship))
        guns_fire_ready(ship);
}

/* (slot): a fighter's mind for a step (an AI ship's: the layer skips it on the machines where the ship is a
 * puppet) */
void __far __cdecl aiFighterThink_impl(uint16_t s)
{
    if (RV_SLOT_OK(s) && slot_is_ship((int16_t)s))
        fighter_think((int16_t)s);
}

/* (slot): a capital ship's mind for a step: its turrets, its course */
void __far __cdecl aiShipThink_impl(uint16_t s)
{
    if (RV_SLOT_OK(s) && slot_is_ship((int16_t)s))
        ship_think((int16_t)s);
}

/* (ship): the game's own ending of a ship (its explosion, its debris, a comm line), then the slot empty
 * (ship_clear).  The layer calls it when the host says a ship died */
void __far __cdecl despawn_impl(uint16_t ship)
{
    if (RV_SLOT_OK(ship) && ship <= RV_MAX_SHIP_SLOT)
        ship_clear((int16_t)ship);
}

/* (src, ship): a ship's end that src brought about (a kill): the same, after its explosion's time */
void __far __cdecl delayedDespawn_impl(uint16_t src, uint16_t ship)
{
    (void)src;                          /* (the blame is the layer's: the explosion's time has passed in step_deaths) */
    if (RV_SLOT_OK(ship) && ship <= RV_MAX_SHIP_SLOT)
        ship_clear((int16_t)ship);
}

/* (nav): the player has reached nav point `nav`: its ships and hazards come (each of its mission ships through
 * outerSpawnShipEntity, at its place off the nav point), once */
void __far __cdecl enterNavPoint_impl(uint16_t nav)
{
    static const char __far s_nav[] = "logic: nav %u reached: %d ships came";
    const uint16_t *at;
    int16_t j, n = 0;
    if (nav >= NAVS)
        return;
    currentNavPoint = nav;
    g_navs_seen |= 1u << nav;
    at = NAV_SHIPS(nav);
    for (j = 0; j < 10 && at[j] != 0xFFFF; j++)
        if (MSHIP_BEHAVIOUR(at[j]) != 6 && outerSpawnShipEntity(at[j], nav) != 0xFFFF)
            n++;
    rv_logf(s_nav, nav, n);
}

/* PLUMBING (ship, speed): the AI's throttle (the layer skips the call for a puppet) */
void __far __cdecl aiSetSpeed_impl(uint16_t ship, uint16_t speed)
{
    if (RV_SLOT_OK(ship))
        setSpeed[ship] = speed;
}

/* PLUMBING (slot, controller): who flies a slot (CTL_*): the layer's way to make a ship a puppet or give it back */
void __far __cdecl setController_impl(uint16_t slot, uint16_t controller)
{
    if (RV_SLOT_OK(slot))
        rvController[slot] = (uint8_t)controller;
}

/* PLUMBING (slot): gone with nothing to show (the layer's: a ship too far to keep, a duplicate) */
void __far __cdecl despawnQuietly_impl(uint16_t slot)
{
    if (RV_SLOT_OK(slot))
        ship_clear(slot);
}

/* PLUMBING (slot, km): 1 when a ship is too far from the player to keep (spec 5.3: the layer asks before it drops
 * one) */
uint16_t __far __cdecl farShipTest_impl(uint16_t slot, uint16_t km)
{
    return slot_is_ship(slot) && player_km(slot) > (int32_t)km;
}

/* PLUMBING (missionShip, navPoint): a mission ship (missionShipTable's record) into a free slot at its nav point
 * (navPointTable's, km), at its formation place off it; behaviour 6 (the player) takes slot 0.  The layer calls it
 * to put another player's ship into this machine's flight (it switches the record's behaviour from 6 to 4 around
 * the call): returns the slot, 0xFFFF none */
uint16_t __far __cdecl outerSpawnShipEntity_impl(uint16_t missionShip, uint16_t navPoint)
{
    const uint8_t *rec = missionShipTable + missionShipTable_SIZE * (missionShip & 63);
    const uint8_t *nav = navPointTable + navPointTable_SIZE * (navPoint & 15);
    uint16_t behaviour = *(const uint16_t *)(rec + 6);
    int16_t s = behaviour == 6 ? 0 : ship_free_slot();
    if (missionShip >= 64 || s < 0)
        return 0xFFFF;
    ship_spawn(s, *(const uint16_t *)rec, *(const uint16_t *)(rec + 2), s == 0 ? CTL_PLAYER : CTL_AI,
               KM * (*(const int16_t *)(nav + 2) + *(const int16_t *)(rec + 10)),
               KM * (*(const int16_t *)(nav + 4) + *(const int16_t *)(rec + 12)),
               KM * (*(const int16_t *)(nav + 6) + *(const int16_t *)(rec + 14)), 0);
    slotMissionShip[s] = missionShip;
    shipStateByte[s] = s == 0 ? 8 : rec[16];
    if (rec[17] && rec[17] <= P_COUNT)  /* (its AI's profile, when the mission names one: docs/ai.md) */
        g_mind[s].profile = (uint8_t)(rec[17] - 1);
    return (uint16_t)s;
}
