/*
 *  Which game is running, and where.
 *
 *  DOS_Execute reports every program it loads (wc_net_program_loaded); when
 *  it is one of the executables the hooks know, the data-segment map
 *  (wcnet_ds.def -> ds::...) and the code addresses (code::...) are loaded
 *  for it, relative to the segment DOS put it at.  While no such program
 *  runs, DS is 0, every code::Loc is unknown and the per-instruction hook
 *  does nothing game specific.
 *
 *  There are two games and five executables: The Secret Missions 2 is
 *  Wing Commander's program built again (SM2.EXE), the two Special
 *  Operations are Wing Commander II's (SO1.EXE, SO2.EXE).  What the code
 *  asks is which game it is (is_wc1(), is_wc2()); the build only picks the
 *  addresses.
 */
#ifndef WCNET_GAME_H_
#define WCNET_GAME_H_

#include "dosbox.h"

namespace wc {

enum GameId {
    GAME_NONE = 0,
    GAME_WC1,   // Wing Commander: WC.EXE (with The Secret Missions) and SM2.EXE
    GAME_WC2,   // Wing Commander II: WC2.EXE, SO1.EXE and SO2.EXE
    GAME_RV,    // RetroVega (retrovega/): RV.EXE, the new game, hooked through its RVHOOK thunks
};

// The executable: its column of wcnet_ds.def, its table of code places.
enum BuildId {
    BUILD_NONE = 0,
    BUILD_WC1,  // WC.EXE
    BUILD_SM2,  // SM2.EXE: The Secret Missions 2
    BUILD_WC2,  // WC2.EXE
    BUILD_SO1,  // SO1.EXE: Special Operations 1
    BUILD_SO2,  // SO2.EXE: Special Operations 2
    BUILD_RV,   // RV.EXE: no column of wcnet_ds.def yet; its DS comes from RVHOOK (wcnet_rv.h)
};

extern GameId g_game;
extern BuildId g_build;
extern Bit16u g_loadSeg;  // first paragraph of the program image

// Layout facts that differ between the games.
struct GameParams {
    int slots;               // entity slots (every per-entity array has this many elements)
    int missionShipSize;     // bytes per mission ship record (ds::missionShipTable)
    int missionShipClassOff; // where in it the type/class that marks a field is
    int missionShipClassSize;
    int asteroidField, mineField;  // those two values
    int navPointSize;        // bytes per nav point record (ds::navPointTable)
    int navPointShipsOff;    // ten mission-ship words in it (0: not located)
    int missionShipAiOff;    // WC2: the word in a mission ship record that picks its AI (6: the player; 0: not located)
    int gunsSize, gunSize;   // bytes per slot in ds::gunTable, and per gun in it
    int campaign;            // WC1: the campaign whose table the program loads when it starts (-1: none)
    int shipStatsSize;       // bytes per ship type in the game's table of ship figures (ds::shipStatsHull)
};
extern GameParams g_params;

inline bool is_wc1() { return g_game == GAME_WC1; }
inline bool is_wc2() { return g_game == GAME_WC2; }
const char *game_name();

// From DOS: a program was loaded at loadSeg for the process `psp`, and the
// process `psp` ended.
void game_program_loaded(const char *path, Bit16u loadSeg, Bit16u psp);
void game_program_ended(Bit16u psp);

// The hook dispatcher's part (wcnet_hooks.cpp): the tables changed.
void hooks_game_changed();

}  // namespace wc

#endif
