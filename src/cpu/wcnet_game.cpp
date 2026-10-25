#include <stdio.h>
#include <string.h>
#include <string>
#include "wc_net.h"
#include "wcnet_game.h"
#include <stdlib.h>
#include "mem.h"
#include "regs.h"
#include "wcnet_memory.h"
#include "wcnet_code.h"
#include "wcnet_log.h"
#include "wcnet_session.h"

namespace wc {

GameId g_game = GAME_NONE;
BuildId g_build = BUILD_NONE;
Bit16u g_loadSeg = 0;
GameParams g_params = { 0x40, 0x2a, 0, 2, 0x16, 0x17, 0x4d, 0x39, 0, 0x33, 5, 0, 0x6f };

// WC.EXE starts with the Vega campaign's table (CAMP.000) ...
static const GameParams kWc1Params = { 0x40, 0x2a, 0, 2, 0x16, 0x17, 0x4d, 0x39, 0, 0x33, 5, 0, 0x6f };
// ... and SM2.EXE, the same program otherwise, with Crusade's (CAMP.002).
static const GameParams kSm2Params = { 0x40, 0x2a, 0, 2, 0x16, 0x17, 0x4d, 0x39, 0, 0x33, 5, 2, 0x6f };
// WC2: 70 slots; a mission ship is 0x3c bytes with its class word at +0x15
// (5 and 6 are fields: ovr116:1D72); nav points are 0x65 bytes
static const GameParams kWc2Params = { 0x46, 0x3c, 0x15, 2, 5, 6, 0x65, 0x51, 0x35, 0xa1, 10, -1, 0xf3 };
Bit16u DS = 0;
PhysPt DS_OFF = 0;
static Bit16u g_gamePsp = 0;

namespace ds {
#define WC_DS(name, wc1, sm2, wc2, so1, so2) Bit16u name = 0;
#include "wcnet_ds.def"
#undef WC_DS
}  // namespace ds

namespace code {
#define X(name) Loc name = { 0, 0, 0, 0, 0, 0, #name };
WC_CODE_LIST(X)
#undef X
}  // namespace code

// ---------------------------------------------------------------------------
// Tables

static void load_ds(BuildId build) {
#define WC_DS(name, wc1, sm2, wc2, so1, so2) { static const Bit16u at[] = { 0, wc1, sm2, wc2, so1, so2, 0 }; ds::name = at[build]; }
#include "wcnet_ds.def"
#undef WC_DS
}

static void clear_code() {
#define X(name) code::name.seg = 0;
    WC_CODE_LIST(X)
#undef X
}

// An overlay function or place: the stub's paragraph in the image, a thunk
// in the stub, the offset in the overlay.
static void ovr(Loc &l, Bit16u stubPara, Bit16u stubOff, Bit16u off, int nargs = 0, bool pascal = false) {
    l.seg = (Bit16u)(g_loadSeg + stubPara);
    l.stubOff = stubOff;
    l.off = off;
    l.overlay = 1;
    l.nargs = (Bit8u)nargs;
    l.pascal = pascal ? 1 : 0;
}

static void root(Loc &l, Bit16u para, Bit16u off, int nargs = 0, bool pascal = false) {
    l.seg = (Bit16u)(g_loadSeg + para);
    l.stubOff = 0;
    l.off = off;
    l.overlay = 0;
    l.nargs = (Bit8u)nargs;
    l.pascal = pascal ? 1 : 0;
}

// WC.EXE.  Paragraphs from the executable's segment table (scripts/wcexe.py
// segs); the comments give the names in the IDA database.
static void load_wc1_code() {
    using namespace code;
    enum { SEG001 = 0x03BE, STUB133 = 0x10C4, STUB134 = 0x10C8, STUB140 = 0x110B, STUB143 = 0x1135, STUB145 = 0x1150,
           STUB148 = 0x1176, STUB150 = 0x1185, STUB161 = 0x11BF, STUB162 = 0x11C4, STUB163 = 0x11CE, STUB168 = 0x121B };
    // overlay 143: ships, damage, weapons
    ovr(do_damage, STUB143, 0x0084, 0x0A99, 4);            // do_damage(src, dst, quantity, vec*)
    ovr(delayedDespawn, STUB143, 0x008E, 0x1F15, 2);       // delayedDespawn(src, ship)
    ovr(capshipDeathTimer, STUB143, 0x00C0, 0x1CE2);       // outerSpawnExplosion+149: a capital ship's dying explosion was just given hull/4 + 8 frames ([BB8E + 2*SI])
    ovr(fireGunFromShip, STUB143, 0x012E, 0x2978, 2);      // fireGunFromShip(ship, gun)
    ovr(maybe_fire_all_guns, STUB143, 0x0156, 0x2E76, 1);  // maybe_fire_all_guns(ship)
    ovr(aiSetSpeed, STUB143, 0x010B, 0x0874);              // ship_stats_every_frame+26E (AI set speed)
    ovr(aiSetSpeedReturn, STUB143, 0x010B, 0x0918);        // ship_stats_every_frame+312 (early return)
    // overlay 145: mission spawning
    ovr(outerSpawnShipEntity, STUB145, 0x00B6, 0x115D, 2); // outerSpawnShipEntity(missionShip, situation)
    ovr(enterNavPoint, STUB145, 0x0075, 0x098F, 1);        // set up a nav point (situation): despawn, fields, spawn
    ovr(loadNavTypes, STUB145, 0x003E, 0x08EC, 1);         // loadNavTypes(nav point record*): load the ship types it names (+0x35), drop the others
    // overlay 140: entities
    ovr(despawn, STUB140, 0x01DD, 0x1C16, 1);              // despawn(ship)
    // overlay 163: AI
    ovr(aiShipThink, STUB163, 0x00CA, 0x160E, 1);          // per-frame ship AI (from ovr141:28D0): the capital ships'
    ovr(aiFighterThink, STUB163, 0x0101, 0x13CF, 1);       // the fighters' (ovr141:28D0 sends fighters here): held off a human's slot too
    ovr(farShipTest1, STUB163, 0x0048, 0x003D);            // the AI's `cmp ax,3E80h`: the wingman more than 16000 km from the player is removed
    ovr(farShipTest2, STUB163, 0x00C0, 0x0317);            // the same test on a ship with no target (ovr163:0228)
    // overlay 134: VDU / comms
    ovr(showCommMessage, STUB134, 0x025F, 0x33ED, 2);      // outerSomeCommThing(ship, msg): msg 0 shows dseg:8DF8 on the VDU
    ovr(cockpitPointer, STUB134, 0x0214, 0x2823);          // the frame's last touches (ovr134:26FE): call far [7A4A] draws the mouse pointer
    // overlay 133: autopilot
    ovr(autoAnimation, STUB133, 0x0025, 0x0000, 3);        // autoAnimation(camShipType, camMode, duration)
    ovr(autoAnimationBody, STUB133, 0x002A, 0x0003);       // autoAnimation+3 (after push bp/mov bp,sp)
    ovr(autopilotFinished, STUB133, 0x002A, 0x05C9);       // autopilot camera finished
    // overlay 148: briefing
    ovr(briefingStarted, STUB148, 0x0020, 0x07C1);         // outerLoadBriefingAnimation entry
    // overlay 150: barracks
    ovr(enterBarracks, STUB150, 0x00AC, 0x105D);           // enterBarracks
    ovr(enterBarracksReturn, STUB150, 0x00AC, 0x1391);     // its epilogue (AX = 7: fly)
    // overlay 161: mission lifecycle
    ovr(missionStarting, STUB161, 0x0020, 0x0470);         // runHangarMission+7C: mission about to start
    ovr(missionVictoryCalc, STUB161, 0x0020, 0x0251);      // compute_victory+70: mission over, computing score
    ovr(missionEnded, STUB161, 0x0020, 0x04DD);            // runHangarMission+E9: mission ended
    ovr(runHangarMission, STUB161, 0x0039, 0x03F4);        // runHangarMission
    // overlay 162: simulator
    ovr(simulatorStart, STUB162, 0x002A, 0x112E);          // simulator
    ovr(simulatorEnd, STUB162, 0x002A, 0x132C);            // simulator+1FE
    // overlay 168: asteroid and mine fields
    ovr(clearFields, STUB168, 0x003E, 0x01AE);             // remove the current field's rocks or mines
    // root image
    root(mainLoopTop, SEG001, 0x20E3);                     // main_loop+9A: top of the in-flight frame loop
    root(statusCheckAfterKeys, SEG001, 0x20F4);            // main_loop+AB: cmp missionStatus after handle_key
    root(statusCheckAfterFrame, SEG001, 0x2108);           // main_loop+BF: cmp missionStatus after the frame
    root(statusSetByExitKey, SEG001, 0x20F2);              // main_loop+A9: after missionStatus = EndExit
    root(skipOrchestra, SEG001, 0x04F2);                   // possible_main+358: far call we skip (orchestra)
    root(afterStartup, SEG001, 0x0512);                    // possible_main+378: program started
    root(afterHangarMission, SEG001, 0x0536);              // possible_main+39C: runHangarMission returned
    root(autopilotKey, SEG001, 0x1695);                    // handle_key+12E: autopilot key far call
}

// WC2.EXE: see docs/wc2-port.md for how each was found (scripts/wcmap.py
// pairs the functions with WC.EXE's).  Many of them are pascal.
static void load_wc2_code() {
    using namespace code;
    enum { SEG001 = 0x03CA, SEG005 = 0x073C, SEG006 = 0x0BD7, SEG092 = 0x1677, STUB107 = 0x1743, STUB114 = 0x1764, STUB116 = 0x1783,
           STUB120 = 0x17AE, STUB128 = 0x17D6, STUB133 = 0x1812, STUB136 = 0x1836, STUB129 = 0x17DB, STUB134 = 0x1826, STUB141 = 0x1850,
           STUB148 = 0x1880 };
    // overlay 114: ships, damage, weapons
    ovr(do_damage, STUB114, 0x00AC, 0x1128, 4, true);        // do_damage(src, dst, quantity, vec*)
    ovr(delayedDespawn, STUB114, 0x00B6, 0x2B10, 2, true);   // destroy(src, ship): wrapper of ovr114:2B69
    ovr(fireGunFromShip, STUB114, 0x0183, 0x3847, 2, true);  // fireGunFromShip(ship, gun)
    ovr(aiSetSpeed, STUB114, 0x013D, 0x0E5A, 3, true);       // AI set speed (ship, ...)
    // overlay 116: mission spawning
    ovr(outerSpawnShipEntity, STUB116, 0x002A, 0x1CED, 2);   // outerSpawnShipEntity(missionShip, navPoint)
    ovr(enterNavPoint, STUB116, 0x0089, 0x1511, 1);          // set up a nav point: loads its ship types, despawns, spawns
    ovr(navStepAltIntel, STUB116, 0x0093, 0x1428);          // a nav point step "ship N flies with intelligence file M" (ovr116:1395): about to load the packet into the slot's record
    // root: entities
    root(despawn, SEG006, 0x1AB7, 1);                        // despawn(ship)
    // overlay 141: the per-entity AI dispatcher (fighters go on to ovr129:150E,
    // capital ships to ovr129:1F13, ...)
    ovr(aiShipThink, STUB141, 0x005C, 0x2CD3, 1);            // entity_ai(slot)
    ovr(farShipTest1, STUB129, 0x0052, 0x004E);              // the fighter AI's `cmp ax,3E80h`: a friendly more than 16000 km from the player is removed
    ovr(farShipTest2, STUB129, 0x00D4, 0x036A);              // the same test on a ship with no target (ovr129:0285)
    // overlay 107: autopilot
    ovr(autoAnimation, STUB107, 0x0025, 0x0000, 3);          // autoAnimation(camShipType, camMode, duration)
    ovr(autoAnimationBody, STUB107, 0x0025, 0x0003);         // after its push bp / mov bp,sp
    ovr(autopilotFinished, STUB107, 0x0025, 0x0667);         // ovr107:014C (the autopilot) puts the camera back
    // overlay 128: the campaign loop around a flight (ovr128:02CE)
    ovr(flyMission, STUB120, 0x0043, 0x0800);                // barracks: "Fly mission" chosen, the briefing scene is next
    ovr(missionStarting, STUB128, 0x0039, 0x0486);           // about to load the mission and fly
    ovr(missionEnded, STUB128, 0x0039, 0x04C3);              // the flight loop returned
    // overlay 134: asteroid and mine fields
    // overlay 148: the scene player
    ovr(playScene, STUB148, 0x0020, 0x00D3, 2);              // playScene(scene, inFlight): comm answers, the mission's cutscenes (seg001:05BD, ovr114:21DE, ovr107:01EC), the landing
    ovr(cloak, STUB133, 0x0057, 0x0034, 1);                  // cloak(ship): the ship vanishes (sound, state 1)
    ovr(uncloak, STUB133, 0x0110, 0x0085, 1);                // uncloak(ship): it comes back over 40 frames (state 0)
    ovr(setView, STUB141, 0x0066, 0x0AC1, 2);                // setView(view, 0): what F1..F4 call (0 pilot, 2 left, 1 right, 3 rear)
    ovr(turretFire, STUB136, 0x003E, 0x0510, 0);             // the player's fire key in a turret: a pair of bolts along the camera
    ovr(turretFireShot, STUB136, 0x003E, 0x0536);            // in it: energy and cooldown allow the shot
    ovr(turretAutoNext, STUB136, 0x005C, 0x08D8);            // automatic turret fire of ship SI: next turret [bp-0x1C]
    ovr(turretAutoSkip, STUB136, 0x005C, 0x11BF);            // in it: this turret does not fire
    ovr(clearFields, STUB134, 0x004D, 0x01B8);               // remove the current field's rocks or mines
    // root image: the flight loop (seg001:1BED)
    root(mainLoopTop, SEG001, 0x1CA3);                       // top of the in-flight frame loop
    root(statusCheckAfterFrame, SEG001, 0x1CF2);             // cmp missionStatus after the frame
    root(statusCheckAfterKeys, SEG001, 0x1D07);              // the loop's own test of missionStatus
    root(autopilotKey, SEG001, 0x1079);                      // handle_key: the autopilot key's far call
    root(freeMainMemory, SEG092, 0x0004);                    // the free bytes of the game's own heap (what its loader's error screen calls MAIN)
    root(replayKey, SEG001, 0x1036);                         // handle_key: R plays the last seconds again (ovr137:0000)
    root(cockpitPointer, SEG005, 0x3600);                    // the frame's last touches (seg005:3384): call far [DED6] draws the mouse pointer
    // root image: "wc2 Origin l s<series> m<mission>" flies one mission with
    // no story around it (main, seg001:0301)
    root(missionStartingDirect, SEG001, 0x0330);             // about to load mission [D0] of series [D2]
    root(missionEndedDirect, SEG001, 0x0349);                // the flight loop returned
}

// The other builds: SM2.EXE, SO1.EXE, SO2.EXE (load_sm2_code, ...).
#include "wcnet_ports.h"

// RV.EXE, the new game (retrovega/): none of WC.EXE's places.  Its hook places
// are the RVHOOK thunks (wcnet_rv.h), watched by the dispatcher's own branch.
static void load_rv_code() {}

// ---------------------------------------------------------------------------
// Recognising the program

struct KnownGame {
    GameId id;
    BuildId build;
    const char *exe;         // file name, upper case
    Bit16u dgroupPara;       // DGROUP's paragraph in the image
    Bit16u signatureOff;     // a string in DGROUP that tells this build from another
    const char *signature;
    const char *title;
    void (*loadCode)();
};

// (The paragraphs and the signatures' places: scripts/wcexe.py -e <exe> segs
// and str.)
static const KnownGame kGames[] = {
    { GAME_WC1, BUILD_WC1, "WC.EXE", 0x1231, 0x0187, "Loading WING COMMANDER", "Wing Commander", load_wc1_code },
    { GAME_WC1, BUILD_SM2, "SM2.EXE", 0x11E8, 0x0181, "Loading WING COMMANDER", "Wing Commander: The Secret Missions 2", load_sm2_code },
    { GAME_WC2, BUILD_WC2, "WC2.EXE", 0x1976, 0x8D65, "Origin", "Wing Commander II", load_wc2_code },
    { GAME_WC2, BUILD_SO1, "SO1.EXE", 0x1989, 0x0258, "Loading WC2 - SPECIAL OPERATIONS 1", "Wing Commander II: Special Operations 1", load_so1_code },
    { GAME_WC2, BUILD_SO2, "SO2.EXE", 0x1924, 0x025A, "Loading WC2 - SPECIAL OPERATIONS 2", "Wing Commander II: Special Operations 2", load_so2_code },
    // SO3.EXE is SO2.EXE with its data-file number set to 3 (so3/build.py):
    // the same code, the same tables, the Culloden campaign's files.
    { GAME_WC2, BUILD_SO2, "SO3.EXE", 0x1924, 0x025A, "Loading WC2 - SPECIAL OPERATIONS 2", "Wing Commander II: Special Operations 3 (Culloden)", load_so2_code },
    // RV.EXE: DGROUP's paragraph moves with its code size, so dgroupPara is
    // read from the image (RVHOOK's relocated word; game_program_loaded).
    { GAME_RV, BUILD_RV, "RV.EXE", 0, 0x0400, "Loading RETROVEGA", "RetroVega", load_rv_code },
};
static const KnownGame *g_program = NULL;   // the entry of the program that is running

const char *game_name() {
    if (g_program) {
        return g_program->title;
    }
    for (size_t i = 0; i < sizeof(kGames) / sizeof(kGames[0]); i++) {
        if (kGames[i].build == g_build) {
            return kGames[i].title;
        }
    }
    return "";
}

static void set_game(const KnownGame *game, Bit16u loadSeg, Bit16u psp, Bit16u dgroupPara) {
    g_program = game;
    g_game = game ? game->id : GAME_NONE;
    g_build = game ? game->build : BUILD_NONE;
    g_loadSeg = game ? loadSeg : 0;
    g_gamePsp = game ? psp : 0;
    DS = game ? (Bit16u)(loadSeg + dgroupPara) : 0;
    DS_OFF = (PhysPt)DS * 0x10;
    load_ds(g_build);
    g_params = g_game == GAME_WC2 ? kWc2Params : g_build == BUILD_SM2 ? kSm2Params : kWc1Params;
    clear_code();
    if (game) {
        game->loadCode();
    }
    hooks_game_changed();
}

void game_program_loaded(const char *path, Bit16u loadSeg, Bit16u psp) {
    std::string name = path ? path : "";
    size_t cut = name.find_last_of("\\/:");
    if (cut != std::string::npos) {
        name = name.substr(cut + 1);
    }
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] >= 'a' && name[i] <= 'z') {
            name[i] = (char)(name[i] - 'a' + 'A');
        }
    }
    for (size_t i = 0; i < sizeof(kGames) / sizeof(kGames[0]); i++) {
        const KnownGame &g = kGames[i];
        if (name != g.exe) {
            continue;
        }
        Bit16u para = g.dgroupPara;
        if (g.build == BUILD_RV) {
            // "RVHOOK" at the image's first paragraph, and DGROUP's segment after it
            PhysPt hook = (PhysPt)loadSeg * 0x10;
            static const char kMagic[] = "RVHOOK";
            bool isRv = true;
            for (size_t k = 0; k < 6 && isRv; k++) {
                isRv = mem_readb(hook + (PhysPt)k) == (Bit8u)kMagic[k];
            }
            if (!isRv) {
                wclog(0, "%s has no RVHOOK header: no hooks", name.c_str());
                continue;
            }
            para = (Bit16u)(mem_readw(hook + 8) - loadSeg);
        }
        PhysPt at = ((PhysPt)(loadSeg + para)) * 0x10 + g.signatureOff;
        size_t len = strlen(g.signature);
        bool same = true;
        for (size_t k = 0; k < len && same; k++) {
            same = mem_readb(at + (PhysPt)k) == (Bit8u)g.signature[k];
        }
        if (!same) {
            wclog(0, "%s is not the %s build the hooks know: no multiplayer", name.c_str(), g.title);
            continue;
        }
        set_game(&g, loadSeg, psp, para);
        wclog(1, "%s loaded at %04X (data segment %04X)", g.title, loadSeg, DS);
        return;
    }
    // Anything else: a child of the game (it stays), or another program.
}

// What a game that gave up left on the text screen ("Sorry, an error has
// occurred..."): the only place it says why.
static void log_text_screen() {
    if (mem_readb(0x449) > 3) {
        return;  // not a text mode
    }
    for (int row = 0; row < 25; row++) {
        char line[81];
        int len = 0;
        for (int col = 0; col < 80; col++) {
            Bit8u c = mem_readb(0xB8000 + 2 * (80 * row + col));
            line[col] = c >= 32 && c < 127 ? (char)c : ' ';
            if (line[col] != ' ') {
                len = col + 1;
            }
        }
        line[len] = 0;
        if (len) {
            wclog(1, "  screen: %s", line);
        }
    }
}

void game_program_ended(Bit16u psp) {
    if (g_game != GAME_NONE && psp == g_gamePsp) {
        wclog(1, "%s ended", game_name());
        log_text_screen();
        if (getenv("WCNET_EXIT_STACK")) {
            // Debug aid: the stack at exit, as offsets from the load segment
            // where a word could be a segment of the image (scripts/wcexe.py
            // segs names them), to find who called the error exit.
            std::string line;
            for (int i = 0; i < 160; i++) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%04x ", mem_readw(SegPhys(ss) + ((reg_esp + 2 * i) & 0xffff)));
                line += buf;
                if (i % 16 == 15) {
                    wclog(1, "  stack: %s", line.c_str());
                    line.clear();
                }
            }
            wclog(1, "  load segment %04x cs:ip %04x:%04x", g_loadSeg, SegValue(cs), reg_eip);
        }
        if (g_session) {
            g_session->on_program_ended();
        }
        set_game(NULL, 0, 0, 0);
    }
}

}  // namespace wc

void wc_net_program_loaded(const char *path, Bit16u loadSeg, Bit16u psp) {
    wc::game_program_loaded(path, loadSeg, psp);
}

void wc_net_program_ended(Bit16u psp) {
    wc::game_program_ended(psp);
}
