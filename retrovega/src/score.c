/* score.c: the flight's music, chosen from what is happening (docs/music.md).
 *
 * The user's design (2026-10-23): pensive, curious space songs while all is
 * quiet; battle songs when enemies are near; a short victory song when one is
 * splashed and a short tragic one when a friend is lost; three panic songs,
 * worse and worse as the ship falls apart; a very calm one for the way home,
 * once the mission is done and the carrier is targeted.
 *
 * It only watches: once a frame (flight_music) it reads the ships' slots and
 * the ship's systems (panel.h), so combat needs to tell it nothing.  A ship
 * splashed is an enemy that was near last frame and is gone now; a friend
 * lost the same for one of ours; the panic is the hull (100% whole):
 * under 60, 35, 15 the first, second and third.  The mission is done when
 * the enemies have all gone, and then (or while the player calls the
 * carrier to land) the way home plays.
 *
 * Each mood has its songs (rvdata.h's RV_MUSIC_*); one plays through once
 * (music_loops), then the next takes over on the bar, round and round, the
 * place in each list kept from flight to flight. */
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "rvdata.h"
#include "music.h"
#include "panel.h"
#include "score.h"

enum { CALM, FIGHT, HOME, PANIC, DEAD };
#define NEAR_KM 4000L                   /* an enemy this near starts a fight (a little beyond its hail) */
#define STAY_KM 6000L                   /* and one this near keeps it going */
#define GONE_KM 12000L                  /* a ship gone this near was splashed (farther: it left) */
#define CALM_AFTER (12 * 240UL)         /* the music clock's ticks with none within STAY_KM before it is over */

typedef struct {
    int mood, panic;                    /* and how bad the panic (0..2) */
    int cue;                            /* the song playing for it */
    unsigned long quiet_at;             /* the last time an enemy was near */
    unsigned char side[RV_MAX_SHIP_SLOT + 1];   /* a ship's side + 1 last frame, 0 no ship */
    unsigned char close[RV_MAX_SHIP_SLOT + 1];  /* and if it was within GONE_KM */
    unsigned char hostile_seen;         /* an enemy was out there this flight */
    unsigned char next[3];              /* the next of the cruise, battle and return songs (kept between flights) */
    unsigned char victory;
    unsigned char step;                 /* the test's (/scoretest) */
    unsigned long t0;
} Score;
static Score __far g_sc;

int g_score_test;                       /* RV.EXE /scoretest: the moods by the clock, not by the flight (a test) */

static long km_to(int s)                /* km from the player, roughly (the largest axis and half the rest) */
{
    long dx = labs(gPositionVector[s].x - gPositionVector[0].x) / 256, dy = labs(gPositionVector[s].y - gPositionVector[0].y) / 256,
         dz = labs(gPositionVector[s].z - gPositionVector[0].z) / 256, m = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    return m + (dx + dy + dz - m) / 2;
}

static int is_ship(int s)
{
    return maybeEntityType[s] == 0xC || maybeEntityType[s] == 0xD;
}

/* the mood's song: the next of its list (the panic's is its level's) */
static int song_for(int mood)
{
    int k;
    switch (mood) {
    case CALM:
        k = g_sc.next[0]++ % 6;
        return RV_MUSIC_CRUISE1 + k;
    case FIGHT:
        k = g_sc.next[1]++ % 4;
        return RV_MUSIC_BATTLE1 + k;
    case HOME:
        k = g_sc.next[2]++ % 2;
        return RV_MUSIC_RETURN1 + k;
    case PANIC:
        return RV_MUSIC_PANIC1 + g_sc.panic;
    }
    return RV_MUSIC_TRAGEDY;
}

static void play(int mood)
{
    static const int fade[] = { 2500, 700, 3000, 500, 200 };
    g_sc.mood = mood;
    g_sc.cue = song_for(mood);
    music_cue_at(g_sc.cue, fade[mood], mood == FIGHT || mood == PANIC || mood == DEAD ? MUSIC_BEAT : MUSIC_BAR);
}

/* the test: calm, a fight, a victory and a tragedy over it, the three panics, the way home, dead */
static void test_tick(unsigned long now)
{
    static const unsigned char at[] = { 8, 16, 24, 34, 42, 50, 58, 70 };
    if (g_sc.step >= sizeof at || now - g_sc.t0 < at[g_sc.step] * 240UL)
        return;
    switch (g_sc.step++) {
    case 0: play(FIGHT); break;
    case 1: music_sting(RV_MUSIC_VICTORY1); break;
    case 2: music_sting(RV_MUSIC_TRAGEDY); break;
    case 3: case 4: case 5: g_sc.panic = g_sc.step - 4; play(PANIC); break;
    case 6: play(HOME); break;
    case 7: play(DEAD); break;
    }
}

void score_start(void)
{
    int s;
    g_sc.step = 0;
    g_sc.t0 = clock240();
    g_sc.panic = 0;
    g_sc.quiet_at = 0;
    g_sc.hostile_seen = 0;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++)
        g_sc.side[s] = g_sc.close[s] = 0;
    play(CALM);
}

void score_tick(int landing)
{
    unsigned long now = clock240();
    int s, nearby = 0, staying = 0, hostiles = 0, mood = g_sc.mood, splashed = 0, lost = 0, target = g_sys.target;
    if (g_score_test) {
        test_tick(now);
        return;
    }
    if (mood == DEAD)
        return;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++) {
        int ship = is_ship(s), side = ship ? commAnimInfo[s] : -1;
        long km = ship ? km_to(s) : 0;
        if (ship && side == 1) {
            hostiles++;
            if (km < NEAR_KM)
                nearby++;
            if (km < STAY_KM)
                staying++;
        }
        if (!ship && g_sc.side[s] && g_sc.close[s]) {       /* gone, and near when last seen */
            if (g_sc.side[s] == 2)
                splashed++;
            else if (g_sc.side[s] == 1)
                lost++;
        }
        g_sc.side[s] = (unsigned char)(side + 1);
        g_sc.close[s] = ship && km < GONE_KM;
    }
    if (hostiles)
        g_sc.hostile_seen = 1;
    if (nearby || (staying && mood == FIGHT))
        g_sc.quiet_at = now;
    /* what it should be: dead, a panic, a fight, the way home, or calm */
    if (g_sys.hull <= 0) {
        play(DEAD);
        return;
    }
    if (g_sys.hull < 60) {
        int level = g_sys.hull < 15 ? 2 : g_sys.hull < 35 ? 1 : 0;
        if (mood != PANIC || level > g_sc.panic) {
            g_sc.panic = level;
            play(PANIC);
        }
        return;                         /* (a panic stays: the hull does not mend in flight) */
    }
    if (nearby || (mood == FIGHT && now - g_sc.quiet_at < CALM_AFTER))
        mood = FIGHT;
    else if (landing || (g_sc.hostile_seen && !hostiles)
             || (target > 0 && is_ship(target) && commAnimInfo[target] == 0 && maybeEntityType[target] == 0xD))
        mood = HOME;                    /* the mission done, or the carrier targeted, or calling it */
    else
        mood = CALM;
    if (mood != g_sc.mood)
        play(mood);
    else if (mood != PANIC && music_playing() == g_sc.cue && music_loops() > 0)
        play(mood);                     /* (the song has played through: the next of the list) */
    /* the stings over it all */
    if (lost)
        music_sting(RV_MUSIC_TRAGEDY);
    else if (splashed)
        music_sting(RV_MUSIC_VICTORY1 + g_sc.victory++ % 3);
}
