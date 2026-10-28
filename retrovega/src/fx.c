/* fx.c: the flight's effects from what happens in the world (fx.h).  All its data is far (DGROUP is nearly full). */
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "rvdata.h"
#include "campaign.h"
#include "collide.h"
#include "world.h"
#include "explode.h"
#include "sfx.h"
#include "fx.h"

#define N (RV_MAX_SHIP_SLOT + 1)
#define HIDE_AFTER 2                    /* steps after its death a ship's sprite goes: the flash covers it by then */
#define NEAR_KM 700                     /* this near, a sound at its full volume */
#define GUNS_KM 9000                    /* gunfire heard out to this far */
#define BOOMS_KM 16000                  /* explosions */

static uint8_t __far g_type[N];         /* each slot's EntityType last step */
static uint8_t __far g_dying[N];        /* its explosion started */
static uint16_t __far g_died[N];        /* the step it began */
static RvVec3 __far g_pos[N], __far g_vel[N];   /* a missile's last place and velocity: its burst's */
static uint16_t __far g_last_frame[RV_SFX_COUNT];   /* a volley: one sound of a weapon a step and ship */
static uint8_t __far g_last_owner[RV_SFX_COUNT];
static uint32_t __far g_vis = 0x9E3779B9UL;   /* the effects' own random stream (never the play's) */

static uint16_t vis_rand(void)
{
    g_vis = g_vis * UINT32_C(1664525) + UINT32_C(1013904223);
    return (uint16_t)(g_vis >> 16);
}

/* km from the player to a place (positions), roughly: the largest axis and half the rest */
static int32_t km_to(const RvVec3 *p)
{
    int32_t dx = labs(p->x - gPositionVector[0].x) / 256, dy = labs(p->y - gPositionVector[0].y) / 256,
            dz = labs(p->z - gPositionVector[0].z) / 256, m = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    return m + (dx + dy + dz - m) / 2;
}

/* a sound's volume at km: full near, falling with the square to nothing at far */
static int16_t vol_at(int32_t km, int32_t far_km)
{
    int32_t v;
    if (km <= NEAR_KM)
        return 256;
    if (km >= far_km)
        return 0;
    v = (far_km - km) * 256 / (far_km - NEAR_KM);
    return (int16_t)(v * v / 256);
}

/* a sound a little off its own pitch (a shot's variety): 248 .. 264 */
static int16_t pitch_jitter(void)
{
    return (int16_t)(248 + vis_rand() % 17);
}

void fx_flight_start(void)
{
    int16_t s;
    for (s = 0; s < N; s++) {
        g_type[s] = (uint8_t)maybeEntityType[s];
        g_dying[s] = (uint8_t)(slot_is_ship(s) && shipAiState[s] == 9);
        g_died[s] = frameCounter;
    }
    _fmemset(g_last_frame, 0xFF, sizeof g_last_frame);
}

static void death(int16_t s)
{
    int32_t half_km = collide_half(s) / 256;
    RvVec3 at = gPositionVector[s];
    uint16_t seed = (uint16_t)(frameCounter * 31 + s * 977);
    g_dying[s] = 1;
    g_died[s] = frameCounter;
    if (s == 0) {                       /* the player's own: the camera is in it; only the sound */
        sfx_play(RV_SFX_BOOM_SHIP, 256, 256);
        return;
    }
    if (maybeEntityType[s] == ET_CAPSHIP) {
        /* a capital ship: bursts over its hull, then the last and biggest */
        int16_t k;
        for (k = 0; k < 6; k++) {
            RvVec3 p;
            p.x = at.x + ((int32_t)(vis_rand() % 512) - 256) * (half_km / 2);
            p.y = at.y + ((int32_t)(vis_rand() % 512) - 256) * (half_km / 6);
            p.z = at.z + ((int32_t)(vis_rand() % 512) - 256) * (half_km / 2);
            explode_add(&p, &gVelocityVector[s], half_km / 3, BOOM_SHIP, (uint16_t)(k * 9), (uint16_t)(seed + k));
        }
        explode_add(&at, &gVelocityVector[s], half_km * 5 / 4, BOOM_CAPITAL, 50, seed);
        sfx_play(RV_SFX_BOOM_CAPITAL, vol_at(km_to(&at), BOOMS_KM * 2), 256);
        return;
    }
    explode_add(&at, &gVelocityVector[s], half_km * 13 / 10, BOOM_SHIP, 0, seed);
    sfx_play(RV_SFX_BOOM_SHIP, vol_at(km_to(&at), BOOMS_KM), pitch_jitter());
}

void fx_frame(void)
{
    int16_t s;
    for (s = 0; s < N; s++) {
        uint8_t et = (uint8_t)maybeEntityType[s];
        if (slot_is_ship(s)) {
            if (shipAiState[s] == 9 && !g_dying[s])
                death(s);
            else if (shipAiState[s] != 9)
                g_dying[s] = 0;
        } else {
            g_dying[s] = 0;
        }
        if (et == ET_MISSILE) {
            if (g_type[s] != ET_MISSILE) {   /* away */
                const RvWeapon __far *w = weapon(gInstanceShipTypes[s]);
                int16_t id = w && w->sfx != RV_SFX_NONE ? w->sfx : RV_SFX_LAUNCH;
                sfx_play(id, parentShipForShip[s] == 0 ? 256 : vol_at(km_to(&gPositionVector[s]), GUNS_KM),
                         pitch_jitter());
            }
            g_pos[s] = gPositionVector[s];
            g_vel[s] = gVelocityVector[s];
        } else if (g_type[s] == ET_MISSILE) {   /* its end: a burst where it was */
            RvVec3 at = g_pos[s], vel = g_vel[s];   /* (near copies: the calls take near vectors) */
            explode_add(&at, &vel, 260, BOOM_MISSILE, 0, (uint16_t)(frameCounter * 13 + s));
            sfx_play(RV_SFX_BOOM_SMALL, vol_at(km_to(&at), BOOMS_KM), pitch_jitter());
        }
        g_type[s] = et;
    }
}

int16_t fx_hidden(int16_t s)
{
    return s >= 0 && s < N && g_dying[s] && shipAiState[s] == 9 && (uint16_t)(frameCounter - g_died[s]) >= HIDE_AFTER;
}

void fx_shot(int16_t owner, uint16_t weapon_type)
{
    const RvWeapon __far *w = weapon(weapon_type);
    int16_t id;
    if (!w || w->sfx == RV_SFX_NONE || owner < 0 || owner >= N)
        return;
    id = w->sfx;
    if (g_last_frame[id] == frameCounter && g_last_owner[id] == owner)
        return;                         /* (a volley: its guns sound as one) */
    g_last_frame[id] = frameCounter;
    g_last_owner[id] = (uint8_t)owner;
    sfx_play(id, owner == 0 ? 220 : vol_at(km_to(&gPositionVector[owner]), GUNS_KM), pitch_jitter());
}

void fx_hit(int16_t slot, const RvVec3 *where, int16_t owner)
{
    int16_t v;
    if (slot < 0 || slot >= N)
        return;
    if (slot == 0) {                    /* the player hit: the shield or the hull, by what is left of the shields */
        sfx_play(SHIELD(0, 0) + SHIELD(0, 1) > 0 ? RV_SFX_HIT_SHIELD : RV_SFX_HIT_HULL, 256, pitch_jitter());
        return;
    }
    explode_add(where, &gVelocityVector[slot], 70, BOOM_HIT, 0, (uint16_t)(frameCounter * 7 + slot));
    v = vol_at(km_to(where), GUNS_KM);
    if (owner == 0 && v < 150)
        v = 150;                        /* (the player's own hits always heard) */
    sfx_play(RV_SFX_HIT_FAR, v, pitch_jitter());
}
