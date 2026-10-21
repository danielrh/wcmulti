/* main.c: RV.EXE's start, its shutdown, and the game (game.c).
 *
 *   RV.EXE [/script FILE] [/winrate PERCENT] [/flight SECONDS] [/bench N] [/record N] [/mpu | /nomusic] [/midilog]
 *
 * The music plays on the Sound Blaster's OPL3; /mpu sends it to a General
 * MIDI synthesizer on the MPU-401 instead.
 * Needs RV.DAT and RVGAME.DAT beside it, 32 MB of XMS and VESA mode 100h. */
#include <stdio.h>
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

char g_people_names[16][28];
extern int g_winrate, g_flight_seconds, g_bench_wanted, g_record, g_capital, g_zoom, g_sky, g_can_quit;
extern long g_onfly_px;

void __far __cdecl rvHello_impl(void) { rv_log("rvHello: DS is ready"); }
void __far __cdecl uiMode_impl(unsigned short on) { (void)on; }

static void log_free(const char *when)
{
    union REGS r;
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    int86(0x21, &r, &r);
    rv_log("conventional memory %s: %lu KB free", when, (unsigned long)r.x.bx * 16 / 1024);
}

static void names(void)
{
    unsigned i;
    for (i = 0; i < g_npilots && i < 16; i++) {
        if (g_pilots[i].role == RV_ROLE_PILOT || g_pilots[i].role == RV_ROLE_TECH || g_pilots[i].role == RV_ROLE_BARMAN)
            sprintf(g_people_names[i], "%.16s \"%.10s\"", g_pilots[i].name, g_pilots[i].callsign);
        else
            sprintf(g_people_names[i], "%.26s", g_pilots[i].name);
    }
}

int main(int argc, char **argv)
{
    int i, ok;
    rvHello();
    for (i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "/script") || !strcmp(argv[i], "-script"))
            script_open(argv[++i]);
        else if (!strcmp(argv[i], "/winrate"))          /* tests: how often a launch is won */
            g_winrate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/flight"))           /* tests: seconds a flight lasts (20) */
            g_flight_seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/bench"))            /* tests: benchmark copies, fixed (0 none; else adapting) */
            g_bench_wanted = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/record"))           /* tests: a screenshot every N frames of flight */
            g_record = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/onfly"))            /* the exact draws' pixel budget a frame (32000) */
            g_onfly_px = atol(argv[++i]);
        else if (!strcmp(argv[i], "/stream"))           /* the stream's slots (4) */
            ships_stream_config(atoi(argv[++i]), -1);
        else if (!strcmp(argv[i], "/reads"))            /* the stream's reads a frame (1) */
            ships_stream_config(-1, atoi(argv[++i]));
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "/midilog"))               /* tests: what the music sent, and when */
            music_record(1);
        else if (!strcmp(argv[i], "/mpu"))
            music_device(MUSIC_MPU401);
        else if (!strcmp(argv[i], "/capital"))          /* tests: the carrier in the flight */
            g_capital = 1;
        else if (!strcmp(argv[i], "/zoom"))             /* tests: one ship ahead, swinging in and out: 1 it rolls, 2 it yaws, 3 yaws far out */
            g_zoom = atoi(argv[++i]);
        else if (!strcmp(argv[i], "/sky"))              /* tests: three ship views painted on the sky, to roll and turn round */
            g_sky = 1;
        else if (!strcmp(argv[i], "/noquit"))           /* the page: no Q in the bunk menu (reload is the way out) */
            g_can_quit = 0;
        else if (!strcmp(argv[i], "/nomusic"))
            music_device(MUSIC_OFF);
    }
    if (!xms_init(32768UL)) {
        printf("RetroVega needs 32 MB of XMS (HIMEM.SYS or DOSBox's xms=true, memsize=40).\n");
        return 1;
    }
    /* resident before base: XMS levels nest (xms.c); the songs are resident too */
    log_free("at the start");
    ok = camp_load_data("RVGAME.DAT");
    if (ok) {
        music_init();                   /* before the back buffer: its reading needs room */
        log_free("after the music");
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
