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

void logic_prepare_spawn(int16_t ship_index)
{
    if (ship_index < 0 || ship_index >= NUM_SHIP_SLOTS)   /* (only ship slots keep a turn: nothing to forget) */
        return;
    _fmemset(&g_turn_frac[ship_index][0], 0, sizeof(g_turn_frac[0]));
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
    _fmemset(systemDamage, 0, systemDamage_SIZE);   /* the player's systems whole (flight.c clears only the cockpit's) */
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

#define SHIELD_FULL_SHIFT 8             /* a shield comes back from nothing in 1 << this steps (256: 8.5 s at LOGIC_HZ),
                                           whatever its size (2 points to 900) */

/* the points a store of `most` gains this step to fill in 1 << SHIELD_FULL_SHIFT steps, spread evenly over them:
 * phase (the step's place among them) takes its share, and the shares add up to `most` */
static uint16_t refill(uint16_t most, uint16_t phase)
{
    return (uint16_t)(((uint32_t)(phase + 1) * most >> SHIELD_FULL_SHIFT) - ((uint32_t)phase * most >> SHIELD_FULL_SHIFT));
}

/* 1. what runs down or builds up by itself: the guns' cooldowns (GUN_STATE), gun energy (gunEnergy), the shields
 * coming back (SHIELD toward SHIELD_MAX), the afterburner's fuel.  (The guns' are M2's.)  The armour doesn't come
 * back in flight; a dying ship's shields stay down; a puppet's are the layer's */
static void step_timers(void)
{
    uint16_t phase = frameCounter & ((1 << SHIELD_FULL_SHIFT) - 1);
    int16_t s, k;
    for (s = 0; s < NUM_SHIP_SLOTS; s++) {
        if (!slot_is_ship(s) || shipAiState[s] == 9 || rvController[s] == CTL_PUPPET)
            continue;
        for (k = 0; k < 2; k++) {
            uint16_t most = SHIELD_MAX(s, k), up = refill(most, phase);
            SHIELD(s, k) = SHIELD(s, k) + up < most ? SHIELD(s, k) + up : most;
        }
    }
}

/* 2. each ship's mind, by its rvController: CTL_PLAYER flies by g_pilot; CTL_AI thinks through the aiFighterThink
 * or aiShipThink thunk (ET_CAPSHIP); CTL_PUPPET is the layer's (it writes the slot: nothing here); CTL_SCRIPT the
 * mission's; CTL_FROZEN nothing.  A mind decides: where to turn, the throttle (aiSetSpeed), whom to target */
static void step_minds(void)
{
}

/* 3. the fire loop: who pulls a trigger this step (the player's g_pilot.guns with g_sys.armed, g_pilot.missile;
 * the AI's own decisions) and which guns are ready (cooled, the energy there): each one fired through the
 * fireGunFromShip thunk (or maybe_fire_all_guns), which makes the shot */
static void step_fire(void)
{
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
                     turn_units(ship_index, 1, TURN_STEP(t->roll_dps, roll)));
        }
        ship_orthonormalise(ship_index);
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
            if (ship_index == 0) {
                step_turn(ship_index, g_pilot.steer_x, g_pilot.steer_y, g_pilot.roll);
            } else {
                // step_turn(ship_index, mind[something].steer_x, mind[something].steer_y, mind[something].roll);
            }
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
}

/* 7. ships into ships (collide_ships: the push that parts them, the damage both ways through do_damage) */
static void step_collisions(void)
{
}

/* 8. the dying (shipAiState 9: an explosion's time, then despawn) and the player's end (missionStatus 4) */
static void step_deaths(void)
{
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

/* s flying along its nose at its speed (after a jump: no momentum the other way to slide off) */
static void fly_ahead(int16_t s)
{
    const RvVec3 *F = &gOrientationFrontVector[s];
    int32_t step = KPS_STEP(speedLo[s]);
    gVelocityVector[s] = vec3(FIX_MUL(F->x, step), FIX_MUL(F->y, step), FIX_MUL(F->z, step));
}

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
    (void)ship;
    (void)gun;
    return 0xFFFF;
}

/* (ship): every one of its guns that is ready, fired through the fireGunFromShip thunk */
void __far __cdecl maybe_fire_all_guns_impl(uint16_t ship)
{
    (void)ship;
}

/* (slot): a fighter's mind for a step (an AI ship's: the layer skips it on the machines where the ship is a
 * puppet) */
void __far __cdecl aiFighterThink_impl(uint16_t s)
{
    (void)s;
}

/* (slot): a capital ship's mind for a step: its turrets, its course */
void __far __cdecl aiShipThink_impl(uint16_t s)
{
    (void)s;
}

/* (ship): the game's own ending of a ship (its explosion, its debris, a comm line), then the slot empty
 * (ship_clear).  The layer calls it when the host says a ship died */
void __far __cdecl despawn_impl(uint16_t ship)
{
    (void)ship;
}

/* (src, ship): a ship's end that src brought about (a kill): the same, after its explosion's time */
void __far __cdecl delayedDespawn_impl(uint16_t src, uint16_t ship)
{
    (void)src;
    (void)ship;
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
    return (uint16_t)s;
}
