/* main.c: RV.EXE's start, its shutdown, and the game (game.c).
 *
 *   RV.EXE [/script FILE] [/winrate PERCENT] [/flight SECONDS] [/res 1|2] [/onfly N] [/skyrows N] [/field D]
 *          [/rockxms 0|1] [/mpu | /nomusic] [/noquit]
 *
 * RVT.EXE, the test build (build.sh compiles the modules that have test code
 * again with -dRV_TESTS; tests/run.sh runs it when a test switch is given),
 * takes those and the aids: [/bench N] [/record N] [/zoom N] [/capital] [/sky]
 *          [/midilog] [/scoretest] [/ship T] [/lasers N [/near N] [/farships N] [/bigships N]] [/boltdemo]
 *          [/flames] [/flametest] [/balls N [/nearballs N]] [/balldemo] [/missiledemo] [/skydemo] [/boxes]
 *          [/collidedemo] [/hulldemo [/hulltype N]] [/commdemo]
 *
 * The music plays on the Sound Blaster's OPL3; /mpu sends it to a General
 * MIDI synthesizer on the MPU-401 instead.
 * Needs RV.DAT and RVGAME.DAT beside it, 32 MB of XMS (it takes all there is) and VESA mode 100h. */
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include "rv.h"
#include "rvhooks.h"
#include "room.h"
#include "campaign.h"
#include "game.h"
#include "music.h"
#include "ships.h"
#include "flames.h"
#include "balls.h"
#include "rocks.h"
#include "sky.h"
#include "collide.h"

char __far g_people_names[16][28];         /* (far: DGROUP is full) */
extern int g_winrate, g_flight_seconds, g_flight_timed, g_can_quit, g_res, g_field;
extern long g_onfly_px;
#ifdef RV_TESTS                         /* the test build's aids (flight.c, score.c) */
extern int g_score_test, g_bench_wanted, g_record, g_capital, g_zoom, g_sky;
extern int g_lasers, g_near_bolts, g_far_ships, g_big_ships, g_bolt_demo, g_flames_demo, g_balls, g_near_balls,
    g_ball_demo, g_missile_demo, g_sky_demo, g_boxes, g_collide_demo, g_hull_demo, g_hull_type, g_flame_test,
    g_comm_demo, g_test_ship;
long twins_selftest(void);              /* (twins.c: RVT.EXE /twins) */
#endif

void __far __cdecl rvHello_impl(void) { rv_log("rvHello: DS is ready"); }
void __far __cdecl uiMode_impl(unsigned short on) { (void)on; }

/* DOS's free conventional memory, by walking its arena chain below the video segment (the DOS call
 * gives only the largest block: a freed table under a later one would not show in it) */
static void dos_free(unsigned long *total, unsigned long *largest)
{
    union REGS r;
    struct SREGS sr;
    unsigned seg;
    *total = *largest = 0;
    segread(&sr);
    r.h.ah = 0x52;
    intdosx(&r, &r, &sr);
    seg = *(unsigned short __far *)MK_FP(sr.es, r.x.bx - 2);
    while (seg < 0xA000) {
        unsigned char __far *mcb = MK_FP(seg, 0);
        unsigned owner = *(unsigned short __far *)(mcb + 1);
        unsigned size = *(unsigned short __far *)(mcb + 3);
        if (owner == 0) {
            *total += size;
            if (size > *largest)
                *largest = size;
        }
        if (mcb[0] != 'M')
            break;
        seg += size + 1;
    }
    *total = *total * 16 / 1024;
    *largest = *largest * 16 / 1024;
}

/* what the C far heap already holds free (freed memory stays in it: the DOS figure does not move when a table
 * leaves): the free total and the largest free entry, by walking it */
static void heap_free(unsigned long *total, unsigned long *largest)
{
    struct _heapinfo hi;
    hi._pentry = 0;
    *total = *largest = 0;
    while (_fheapwalk(&hi) == _HEAPOK)
        if (hi._useflag == _FREEENTRY) {
            *total += hi._size;
            if (hi._size > *largest)
                *largest = hi._size;
        }
}

static void log_free(const char *when)
{
    unsigned long dtotal, dlargest, total, largest;
    _fheapshrink();     /* (what the heap holds free in whole segments goes back to DOS first) */
    dos_free(&dtotal, &dlargest);
    heap_free(&total, &largest);
    rv_log("conventional memory %s: %lu KB free in DOS (the largest block %lu KB), %lu KB free in the heap (largest %lu KB)",
           when, dtotal, dlargest, total, largest);
}

static void names(void)
{
    unsigned i;
    char t[28];
    for (i = 0; i < g_npilots && i < 16; i++) {
        if (g_pilots[i].role == RV_ROLE_PILOT || g_pilots[i].role == RV_ROLE_TECH || g_pilots[i].role == RV_ROLE_BARMAN)
            sprintf(t, "%.16s \"%.10s\"", g_pilots[i].name, g_pilots[i].callsign);
        else
            sprintf(t, "%.26s", g_pilots[i].name);
        _fstrcpy(g_people_names[i], t);
    }
}

#ifdef RV_HOST                          /* (on the host the front end has the program's main, and calls this) */
#define main rv_main
#endif
int main(int argc, char **argv)
{
    int i, ok;
    rvHello();
#ifdef RV_TESTS
    rv_log("RVT.EXE: the test build (the demos, the benchmarks and the self-tests are in)");
#endif
    for (i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "/script") || !strcmp(argv[i], "-script"))
            script_open(argv[++i]);
        else if (!strcmp(argv[i], "/winrate"))          /* tests: how often a launch is won */
            g_winrate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/flight"))           /* tests: a flight lands by itself after N s (else on C) */
            g_flight_seconds = atoi(argv[++i]), g_flight_timed = 1;
        else if (!strcmp(argv[i], "/res"))              /* 1 the sets as they are; 2 the block mode (SHIPnn.RVH) */
            g_res = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/onfly"))            /* the exact draws' pixel budget a frame (32000) */
            g_onfly_px = atol(argv[++i]);
        else if (!strcmp(argv[i], "/skyrows"))          /* the sky's far things: 0 each its own way, 1 all rows, 2
                                                           doubled, 3 interlaced */
            sky_rows(atoi(argv[++i]));
        else if (!strcmp(argv[i], "/field"))            /* flights start in an asteroid field: D rocks per
                                                           (10,000 km)^3 (a stand-in for a nav point's hazard) */
            rocks_want((g_field = atoi(argv[++i])) > 0);
        else if (!strcmp(argv[i], "/rockxms"))          /* 1 (the default): a big rock's rows moved by XMS one by one;
                                                           0: the rock's pixels moved once, then copied */
            g_rock_xms = atoi(argv[++i]);
#ifdef RV_TESTS
        else if (!strcmp(argv[i], "/bench"))            /* tests: benchmark copies, fixed (0 none; else adapting) */
            g_bench_wanted = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/record"))           /* tests: a screenshot every N frames of flight */
            g_record = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/zoom"))             /* tests: one ship ahead, swinging in and out: 1 it rolls, 2 it yaws, 3 yaws far out */
            g_zoom = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/lasers"))           /* tests: the laser benchmark, N far bolts (-1 adapting) */
            g_lasers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/near"))             /* tests: its near bolts (12) */
            g_near_bolts = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/farships"))         /* tests: its far ships (16) */
            g_far_ships = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/bigships"))         /* tests: its big near ships (2) */
            g_big_ships = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/hulldemo")) {       /* tests: a turning fighter shot through by rays */
            g_hull_demo = 1;
            g_lasers = 1;
            g_near_bolts = 0;
            g_far_ships = 0;
            g_big_ships = 1;
        } else if (!strcmp(argv[i], "/hulltype"))       /* tests: the hull demo's ship type */
            g_hull_type = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/collidedemo")) {    /* tests: two fighters flown into each other */
            g_collide_demo = 1;
            g_lasers = 1;
            g_near_bolts = 0;
            g_far_ships = 0;
            g_big_ships = 2;
        } else if (!strcmp(argv[i], "/boxes"))          /* tests: every ship's box (flight_ent_box) bracketed */
            g_boxes = 1;
        else if (!strcmp(argv[i], "/flames"))           /* tests: six ships turning every way, their engines' flames */
            g_flames_demo = 1;
        else if (!strcmp(argv[i], "/flametest"))       /* tests: the flames' self-test (their ways), then the scene */
            g_flames_demo = g_flame_test = 1;
        else if (!strcmp(argv[i], "/commdemo"))        /* tests: the pilots on the comm in turn */
            g_comm_demo = 1;
        else if (!strcmp(argv[i], "/ship")) {          /* tests: the player's ship type (hex), whatever the deck chose */
            const char *h = argv[++i];
            for (g_test_ship = 0; *h; h++)
                g_test_ship = g_test_ship * 16 + (*h <= '9' ? *h - '0' : (*h | 32) - 'a' + 10);
            g_test_ship++;
        }
        else if (!strcmp(argv[i], "/balls")) {          /* tests: balls in the laser benchmark, N far (-1 adapting) */
            g_balls = atoi(argv[++i]);
            if (!g_lasers) {            /* (alone: the scene with one bolt) */
                g_lasers = 1;
                g_near_bolts = 0;
            }
        } else if (!strcmp(argv[i], "/nearballs"))      /* tests: its near balls (12) */
            g_near_balls = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/balldemo")) {       /* tests: the four kinds of ball flying at the eye */
            g_ball_demo = 1;
            g_balls = 1;
            g_near_balls = 0;
            g_lasers = 1;
            g_near_bolts = 0;
            g_big_ships = g_far_ships = 0;
        } else if (!strcmp(argv[i], "/skydemo")) {      /* tests: the sky's far things as the camera rolls */
            g_sky_demo = 1;
            g_lasers = 1;
            g_near_bolts = 0;
            g_big_ships = g_far_ships = 0;
        } else if (!strcmp(argv[i], "/missiledemo")) {  /* tests: six missiles seen every way */
            g_missile_demo = 1;
            g_lasers = 1;
            g_near_bolts = 0;
            g_big_ships = g_far_ships = 0;
        } else if (!strcmp(argv[i], "/boltdemo")) {     /* tests: the player's own bolts */
            g_bolt_demo = 1;
            g_lasers = 1;
            g_near_bolts = 0;
            g_big_ships = g_far_ships = 0;
        }
#endif
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "/mpu"))
            music_device(MUSIC_MPU401);
        else if (!strcmp(argv[i], "/nomusic"))
            music_device(MUSIC_OFF);
        else if (!strcmp(argv[i], "/noquit"))           /* the page: no Q in the bunk menu (reload is the way out) */
            g_can_quit = 0;
#ifdef RV_TESTS
        else if (!strcmp(argv[i], "/midilog"))          /* tests: what the music sent, and when */
            music_record(1);
        else if (!strcmp(argv[i], "/capital"))          /* tests: the carrier in the flight */
            g_capital = 1;
        else if (!strcmp(argv[i], "/scoretest"))        /* tests: the flight's music by the clock (score.c); 2: the remixes; 3: the cycle's end */
            g_score_test = i + 1 < argc && argv[i + 1][0] >= '1' && argv[i + 1][0] <= '9' ? atoi(argv[++i]) : 1;
        else if (!strcmp(argv[i], "/sky"))              /* tests: three ship views painted on the sky, to roll and turn round */
            g_sky = 1;
        else if (!strcmp(argv[i], "/twins"))            /* tests: the asm's C twins against it (twins.c), then out */
            exit(twins_selftest() < 0 ? 4 : 0);
#endif
    }
    if (!xms_init(32768UL, 0x7FFFFFFFUL)) {
        printf("RetroVega needs 32 MB of XMS (HIMEM.SYS or DOSBox's xms=true, memsize=63).\n");
        return 1;
    }
    /* resident before base: XMS levels nest (xms.c); the songs are resident too */
    log_free("at the start");
    ok = camp_load_data("RVGAME.DAT");
    if (ok) {
        music_init();                   /* before the back buffer: its reading needs room */
        log_free("after the music");
        flames_reserve();               /* (resident too) */
        balls_reserve();                /* the balls' sprites (resident) */
        sky_reserve();                  /* the sky's far things (resident) */
        collide_reserve();              /* the ships' silhouettes (resident) */
        ok = dat_open("RV.DAT");
    }
    if (!ok) {
        printf("RetroVega: RV.DAT or RVGAME.DAT is missing or from another build. See RVLOG.TXT.\n");
        xms_shutdown();
        return 1;
    }
    names();
    if (!vid_init()) {
        printf("RetroVega needs a VESA card with mode 100h (640x400, 256 colours).\n");
        xms_shutdown();
        return 1;
    }
    gfx_font_load();
    flames_bake();                      /* the engines' flames, once */
    log_free("after the screen and the fonts");
    in_init();
    uiMode(1);
    ok = game_run();
    uiMode(0);
    music_shutdown();
    vid_shutdown();
    xms_shutdown();
    rv_log("bye (%d)", ok);
    return 0;
}
