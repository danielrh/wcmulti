/* main.c: RV.EXE's start, its shutdown, and the game (game.c).
 *
 *   RV.EXE [/script FILE] [/winrate PERCENT]
 *
 * Needs RV.DAT and RVGAME.DAT beside it, 32 MB of XMS and VESA mode 100h. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "room.h"
#include "campaign.h"
#include "game.h"

char g_people_names[16][28];
extern int g_winrate;

void __far __cdecl rvHello_impl(void) { rv_log("rvHello: DS is ready"); }
void __far __cdecl uiMode_impl(unsigned short on) { (void)on; }

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
    }
    if (!xms_init(32768UL)) {
        printf("RetroVega needs 32 MB of XMS (HIMEM.SYS or DOSBox's xms=true, memsize=40).\n");
        return 1;
    }
    /* resident before base: XMS levels nest (xms.c) */
    if (!camp_load_data("RVGAME.DAT") || !dat_open("RV.DAT")) {
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
    in_init();
    uiMode(1);
    ok = game_run();
    uiMode(0);
    vid_shutdown();
    xms_shutdown();
    rv_log("bye (%d)", ok);
    return 0;
}
