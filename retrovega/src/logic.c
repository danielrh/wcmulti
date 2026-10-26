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

/* --- the flight begins --- */

/* the mission's ships into the slots: the player in slot 0 (the camera and the cockpit are slot 0's), then every
 * other ship of the mission 4,000 .. 12,000 km out within 60 degrees of the nose, the wingman facing ahead, the rest
 * facing the player.  (The first flight's placement: the mission's nav points are TODO, each ship's record in
 * missionShipTable says which, +8; outerSpawnShipEntity puts a record in at its nav point) */
void logic_start(const RvMission __far *m)
{
    static const char __far s_spawn[] = "logic: slot %d type %02X spawned at bearing %d, %ld km: %ld,%ld";
    int i, k, j = 0, n = 1;
    _fmemset(&g_pilot, 0, sizeof g_pilot);
    ship_spawn(0, g_camp.player_ship, SIDE_TERRAN, CTL_PLAYER, 0, 0, 0, 0);
    shipStateByte[0] = 8;               /* (a human) */
    for (i = 0; i < m->nship; i++) {
        const RvShip __far *sh = &m->ship[i];
        int type = sh->type == RV_SHIP_PLAYER || sh->type == RV_SHIP_WINGMAN ? g_camp.player_ship : sh->type;
        for (k = 0; k < sh->count; k++, j++) {
            int a;
            long dist, sx, sz;
            if (sh->type == RV_SHIP_PLAYER) {
                slotMissionShip[0] = (unsigned short)j;
                continue;
            }
            if (n > RV_MAX_SHIP_SLOT)
                continue;
            a = (int)(game_rand() % 342) - 171;   /* within 60 degrees of the nose */
            /* (sin x distance would overflow a long: the distance goes in as km) */
            dist = KM * (4000 + (long)(game_rand() % 8000));
            sx = (lf_sin(a) * (dist >> 8)) >> 6;
            sz = (lf_cos(a) * (dist >> 8)) >> 6;
            ship_spawn(n, type, sh->side, CTL_AI, sx, (long)(game_rand() % 20000) * 20 - 200000L, sz,
                       sh->type == RV_SHIP_WINGMAN ? 0 : a + 512);
            slotMissionShip[n] = (unsigned short)j;
            shipStateByte[n] = sh->pilot;
            rv_logf(s_spawn, n, type, a, dist / KM, sx, sz);
            n++;
        }
    }
}

/* --- a step: the main loop's body --- */

/* 1. what runs down or builds up by itself: the guns' cooldowns (GUN_STATE), gun energy (gunEnergy), the shields
 * coming back (SHIELD toward SHIELD_MAX), the afterburner's fuel */
static void step_timers(void)
{
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

/* 4. every ship flies its step: its turn applied to its orientation (vec_turn, then ship_orthonormalise), its speed
 * toward setSpeed, gVelocityVector (positions a step: KPS_STEP), the velocity added to gPositionVector.  A puppet
 * only coasts (position += velocity) */
static void step_motion(void)
{
}

/* 5. the shots fly (shots_move) and hit (shot_hit: the ship and the place; do_damage through its thunk with
 * shot_source(i) as the source; shot_remove) */
static void step_shots(void)
{
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

/* 9. the mission: nav points reached (enterNavPoint), waves, objectives, the score (victoryPoints, nonzero won) */
static void step_mission(void)
{
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
}

/* --- over the view: a band of rows at a time (hud.h's calls are clipped to it and to the opening) --- */

void logic_hud(void)
{
}

void logic_end(void)
{
}

/* --- the thunks' bodies (abi/rvabi.def, spec 5; docs/logic.md has each one's contract) --- */

/* (src, dst, qty, where): a hit of qty on the ship in slot dst at the place `where` (a near RvVec3, positions):
 * returns what it did, 0 nothing.  The player takes none while playerCanBeDamaged is 0.  Everything that hurts goes
 * through here (shots, rocks: rocks.c calls it, rams), and the layer replays a hit on the other machines with src a
 * bolt of the shooter's, the shooter's ship or 0xFFFF: what it does must not depend on src */
unsigned short __far __cdecl do_damage_impl(unsigned short src, unsigned short dst, unsigned short qty,
                                            unsigned short where)
{
    (void)src;
    (void)dst;
    (void)qty;
    (void)where;
    return 0;
}

/* (ship, gun): gun record `gun` of the ship fires: a gun or a turret puts a shot in the world (shot_add, from its
 * mount: GUN_MOUNT_X / Y), a launcher a missile into a free ship slot.  It fires whatever its cooldown and the
 * energy: the deciding is the caller's (the layer replays this call on the other machines with the gun record as
 * the shooter's machine had it, so it must not refuse there); it sets the cooldown and spends the energy itself.
 * Returns a missile's slot (the layer maps it), else 0xFFFF */
unsigned short __far __cdecl fireGunFromShip_impl(unsigned short ship, unsigned short gun)
{
    (void)ship;
    (void)gun;
    return 0xFFFF;
}

/* (ship): every one of its guns that is ready, fired through the fireGunFromShip thunk */
void __far __cdecl maybe_fire_all_guns_impl(unsigned short ship)
{
    (void)ship;
}

/* (slot): a fighter's mind for a step (an AI ship's: the layer skips it on the machines where the ship is a
 * puppet) */
void __far __cdecl aiFighterThink_impl(unsigned short s)
{
    (void)s;
}

/* (slot): a capital ship's mind for a step: its turrets, its course */
void __far __cdecl aiShipThink_impl(unsigned short s)
{
    (void)s;
}

/* (ship): the game's own ending of a ship (its explosion, its debris, a comm line), then the slot empty
 * (ship_clear).  The layer calls it when the host says a ship died */
void __far __cdecl despawn_impl(unsigned short ship)
{
    (void)ship;
}

/* (src, ship): a ship's end that src brought about (a kill): the same, after its explosion's time */
void __far __cdecl delayedDespawn_impl(unsigned short src, unsigned short ship)
{
    (void)src;
    (void)ship;
}

/* (nav): the player has reached nav point `nav`: its ships and hazards come */
void __far __cdecl enterNavPoint_impl(unsigned short nav)
{
    currentNavPoint = nav;
}

/* PLUMBING (ship, speed): the AI's throttle (the layer skips the call for a puppet) */
void __far __cdecl aiSetSpeed_impl(unsigned short ship, unsigned short speed)
{
    if (RV_SLOT_OK(ship))
        setSpeed[ship] = speed;
}

/* PLUMBING (slot, controller): who flies a slot (CTL_*): the layer's way to make a ship a puppet or give it back */
void __far __cdecl setController_impl(unsigned short slot, unsigned short controller)
{
    if (RV_SLOT_OK(slot))
        rvController[slot] = (unsigned char)controller;
}

/* PLUMBING (slot): gone with nothing to show (the layer's: a ship too far to keep, a duplicate) */
void __far __cdecl despawnQuietly_impl(unsigned short slot)
{
    if (RV_SLOT_OK(slot))
        ship_clear(slot);
}

/* PLUMBING (slot, km): 1 when a ship is too far from the player to keep (spec 5.3: the layer asks before it drops
 * one) */
unsigned short __far __cdecl farShipTest_impl(unsigned short slot, unsigned short km)
{
    return slot_is_ship(slot) && player_km(slot) > (long)km;
}

/* PLUMBING (missionShip, navPoint): a mission ship (missionShipTable's record) into a free slot at its nav point
 * (navPointTable's, km), at its formation place off it; behaviour 6 (the player) takes slot 0.  The layer calls it
 * to put another player's ship into this machine's flight (it switches the record's behaviour from 6 to 4 around
 * the call): returns the slot, 0xFFFF none */
unsigned short __far __cdecl outerSpawnShipEntity_impl(unsigned short missionShip, unsigned short navPoint)
{
    const unsigned char *rec = missionShipTable + missionShipTable_SIZE * (missionShip & 63);
    const unsigned char *nav = navPointTable + navPointTable_SIZE * (navPoint & 15);
    unsigned short behaviour = *(const unsigned short *)(rec + 6);
    int s = behaviour == 6 ? 0 : ship_free_slot();
    if (missionShip >= 64 || s < 0)
        return 0xFFFF;
    ship_spawn(s, *(const unsigned short *)rec, *(const unsigned short *)(rec + 2), s == 0 ? CTL_PLAYER : CTL_AI,
               KM * (*(const short *)(nav + 2) + *(const short *)(rec + 10)),
               KM * (*(const short *)(nav + 4) + *(const short *)(rec + 12)),
               KM * (*(const short *)(nav + 6) + *(const short *)(rec + 14)), 0);
    slotMissionShip[s] = missionShip;
    shipStateByte[s] = s == 0 ? 8 : rec[16];
    return (unsigned short)s;
}
