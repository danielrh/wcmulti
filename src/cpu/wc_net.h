/*
 *  Public interface of the Wing Commander multiplayer layer.
 *
 *  The implementation lives in src/cpu/wcnet_*.cpp:
 *    wcnet_game       which executable is running, and its address tables
 *    wcnet_memory     data-segment map and typed accessors
 *    wcnet_code       code addresses (stubs, overlays, hook points)
 *    wcnet_vm         running game code (interception, trampoline)
 *    wcnet_transport  TCP framing and per-category message queues
 *    wcnet_entities   network id <-> local slot mapping
 *    wcnet_events     interception and replay of game events
 *    wcnet_session    server/client sessions and the per-frame exchange
 *    wcnet_hooks      the per-instruction dispatcher
 */
#ifndef WC_NET_H_
#define WC_NET_H_

#include <string>
#include "dosbox.h"
#include "net_config.h"

// The hooks watch every instruction the CPU cores run, and nearly every one
// is none of their business: the cores make this test in line and only call
// out for an instruction at a watched address, and once in a thousand for
// the periodic work (wc_net_countdown; 0 forces the call for the next one).
// A call per instruction costs the browser build about a seventh of its
// speed, more than anything else the hooks do.
extern bool wc_net_watch[0x10000];
extern Bit32s wc_net_countdown;
void wc_net_check_cpu_hooks();
static inline void wc_net_cpu_hook(Bit32u ip) {
    if (GCC_UNLIKELY(--wc_net_countdown < 0) || (ip < 0x10000 && GCC_UNLIKELY(wc_net_watch[ip]))) {
        wc_net_check_cpu_hooks();
    }
}

// Called by DOS when it has loaded a program (its path, the first paragraph
// of the image, its PSP) and when a process ends: the hooks only apply to
// the executables they know, wherever DOS put them.
void wc_net_program_loaded(const char *path, Bit16u loadSeg, Bit16u psp);
void wc_net_program_ended(Bit16u psp);

// Start a server (WCHOST unset) or connect to one (WCHOST set); with WCROOM
// set, meet the others in a lobbylink room instead.  See NetConfig.
bool init_network();
void uninit_network();

// GUI queries (src/gui/sdlmain.cpp) and the WCNET command.
bool is_wc_connected();
// "server", "client" or "" when not connected.
const char *wc_net_role();
// Something worth telling the player about the connection, or "".
const char *wc_net_status_note();
bool in_space();
// Chat lines and notices go on the emulator's overlay: outside flight, and in
// flight when the game has no comms display the hooks can write to (WC2).
bool wc_net_overlay_chat();
extern bool within_briefed_mission;
void wcnetSendChatMessage(const std::string &msg);
// Asteroid and mine fields: 1 on (the default), 0 off (WCROCKS=0), 2 soft
// (WCROCKS=soft: a rock that hits a player's own ship does a small fraction
// of the game's damage, so one collision is survivable).  The host's choice
// goes to every player; "/rocks on", "/rocks soft" and "/rocks off" in the
// comms prompt do the same.
// Called by the keyboard before a key reaches the game: true takes the key
// (a drone's copilot keys go to the server instead; wcnet_hooks.cpp).
bool wc_net_key_filter(int kbdKey, bool pressed);
// A drone riding behind the leader: its dashboard, from that line of the
// game's 200 down, is to go dark (src/gui/sdlmain.cpp).
bool wc_net_cockpit_dim(int *dashboardTop);
// True while the comm display shows a line from the leader's ship on a
// wingman's machine; vduOrigin: the left and top of that display's picture.
bool wc_net_helmet_name(int *vduOrigin);
void wc_net_set_rocks(int mode);
int wc_net_rocks();
// Collisions between the humans' ships (WingBumps in wcnet_events.h: 1 harmless, 0 the game's).
void wc_net_set_wing_bumps(int mode);
int wc_net_wing_bumps();
// The exchange mode (ExchangeMode in wcnet_session.h): the setting, 0 auto
// (the default: the host's game decides from the link it measures), 1 low
// latency, 2 high latency (WCNET_MODE=auto|low|high; the host's lobby option;
// "/latency auto", "/latency low", "/latency high" in the comms prompt), the
// mode in force, and the round trip to the other side in ms (-1: unknown).
void wc_net_set_mode(int setting);
int wc_net_mode_setting();
int wc_net_mode();
double wc_net_rtt_ms();

#endif
