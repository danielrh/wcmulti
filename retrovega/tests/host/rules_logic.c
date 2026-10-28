/* tests/host/rules_logic.c: a logic.c that breaks the layer's rules on purpose, one at a time at known steps, for
 * the host harness's self-test (tests/host/rvsim_test.sh, LOGIC=tests/host/rules_logic.c host/build.sh).  NOT the
 * game's logic: two ships that never move, and the breaks.  The environment turns on the ones that stop the run. */
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "world.h"
#include "shots.h"
#include "logic.h"

PilotInput __far g_pilot;

void logic_start(const RvMission __far *m)
{
    (void)m;
    ship_spawn(0, g_camp.player_ship, SIDE_TERRAN, CTL_PLAYER, 0, 0, 0, 0);
    ship_spawn(1, g_camp.player_ship, SIDE_AERA, CTL_AI, 0, 0, KM * 3000, TURN / 2);
}

void logic_frame(void)
{
    RvVec3 at;
    at.x = at.y = at.z = 0;
    switch (frameCounter) {
    case 10:                            /* a gun that refuses (fireGunFromShip_impl below makes nothing) */
        fireGunFromShip(0, 0);
        break;
    case 20:                            /* a hit whose amount depends on src (--check-src) */
        do_damage(1, 0, 1, RV_WHERE(&at));
        break;
    case 30:                            /* the C library's random numbers, not game_rand's */
        (void)rand();
        break;
    case 40:                            /* a frame bent out of square */
        gOrientationFrontVector[1].x += 100;
        break;
    case 50:                            /* 32-bit signed overflow: UBSan stops the run */
        if (getenv("RV_TEST_UB")) {
            volatile int32_t big = INT32_MAX;
            big += 1;
        }
        break;
    case 60:                            /* 300 x 200 kept in an int16_t (DOS wraps it to -5536): clang's truncation
                                           check stops the run */
        if (getenv("RV_TEST_TRUNC")) {
            volatile int16_t a = 300, b = 200;
            int16_t c = a * b;
            (void)c;
        }
        break;
    case 70:                            /* past the shot list's end: RV_CHECK stops the run */
        if (getenv("RV_TEST_BOUNDS"))
            (void)shot(5);
        break;
    }
}

uint16_t __far __cdecl fireGunFromShip_impl(uint16_t ship, uint16_t gun)
{
    (void)ship;
    (void)gun;
    return 0xFFFF;
}

uint16_t __far __cdecl do_damage_impl(uint16_t src, uint16_t dst, uint16_t qty, uint16_t where)
{
    uint16_t d = (uint16_t)(qty + (src == 0xFFFF ? 0 : 1));   /* (the break: one more when the source is known) */
    (void)where;
    SHIELD(dst, 0) = SHIELD(dst, 0) > d ? (uint16_t)(SHIELD(dst, 0) - d) : 0;
    return qty;
}

void logic_hud(void) {}
void logic_end(void) {}
int16_t logic_autopilot(void) { return 0; }
