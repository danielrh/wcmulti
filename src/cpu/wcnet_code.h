/*
 *  Code addresses used by the multiplayer hooks, for the game that is running.
 *
 *  The Wing Commander executables are Borland overlay programs.  Overlay
 *  functions are reached through a stub segment holding `jmp far` thunks (one
 *  every 5 bytes from offset 0x20); the overlay code itself is loaded at a
 *  run-time dependent segment, so a hook on an overlay function compares CS
 *  against the segment currently stored in the thunk and IP against the
 *  function's fixed offset.  Root-image code sits at a fixed distance from
 *  the program's load segment.
 *
 *  Address forms (all resolvable with scripts/wcexe.py):
 *     stub143:0084  -> thunk; its segment is load segment + the stub's paragraph
 *     ovr143:0A99   -> offset inside the overlay
 *     seg001:20E3   -> root image
 *
 *  The values are loaded by wcnet_game.cpp when DOS starts a known program.
 */
#ifndef WCNET_CODE_H_
#define WCNET_CODE_H_

#include "dosbox.h"

namespace wc {

// A function entry or an instruction we hook.  seg == 0: the running game has
// no such place (or it has not been found yet), and nothing matches it.
struct Loc {
    Bit16u seg;       // run-time segment: of the root code, or of the overlay's stub
    Bit16u stubOff;   // overlay: a thunk in the stub (the function's own when it is exported)
    Bit16u off;       // offset of the place in its code segment (overlay or root)
    Bit8u overlay;    // lives in an overlay
    Bit8u nargs;      // function: words of arguments
    Bit8u pascal;     // function: arguments pushed first to last, the callee pops them
    const char *name;
    bool known() const { return seg != 0; }
};

namespace code {

// The older names for the same thing.
typedef Loc OverlayFn;
typedef Loc OverlayLoc;
typedef Loc RootLoc;

#define WC_CODE_LIST(X) \
    /* ships, damage, weapons */ \
    X(do_damage) X(delayedDespawn) X(fireGunFromShip) X(maybe_fire_all_guns) X(aiSetSpeed) X(aiSetSpeedReturn) \
    /* mission spawning, entities, AI, comms */ \
    X(outerSpawnShipEntity) X(enterNavPoint) X(navStepAltIntel) X(despawn) X(aiShipThink) X(showCommMessage) \
    /* autopilot */ \
    X(autoAnimation) X(autoAnimationBody) X(autopilotFinished) X(autopilotKey) \
    /* briefing, barracks, mission lifecycle, simulator */ \
    X(briefingStarted) X(enterBarracks) X(enterBarracksReturn) X(missionStarting) X(missionVictoryCalc) X(missionEnded) \
    X(missionStartingDirect) X(missionEndedDirect) X(flyMission) \
    X(runHangarMission) X(simulatorStart) X(simulatorEnd) \
    /* asteroid and mine fields */ \
    X(clearFields) \
    /* turrets (WC2) */ \
    X(turretFire) X(turretFireShot) X(turretAutoNext) X(turretAutoSkip) X(setView) X(replayKey) X(freeMainMemory) \
    /* cloaking (WC2) */ \
    X(cloak) X(uncloak) \
    /* the cockpit's mouse pointer */ \
    X(cockpitPointer) \
    /* root image */ \
    X(mainLoopTop) X(statusCheckAfterKeys) X(statusCheckAfterFrame) X(statusSetByExitKey) \
    X(skipOrchestra) X(afterStartup) X(afterHangarMission)

#define X(name) extern Loc name;
WC_CODE_LIST(X)
#undef X

}  // namespace code
}  // namespace wc

#endif
