/*
 *  Game events: interception of the game's own calls and replay of remote
 *  events through the trampoline.
 *
 *  Interception entry points are invoked by the hook dispatcher when CS:IP is
 *  at the entry of the corresponding game function, at any nesting depth
 *  (including from inside a replayed call).  Each decides between letting the
 *  call run, skipping it (return_from_call), or running it through the
 *  trampoline so that the result can be inspected and the event broadcast.
 */
#ifndef WCNET_EVENTS_H_
#define WCNET_EVENTS_H_

#include "dosbox.h"
#include "wcnet_vm.h"
#include "wc.pb.h"

namespace wc {

// --- interception ---------------------------------------------------------
void on_do_damage_entry();        // do_damage(src, dst, quantity, vec*)
void on_fire_entry();             // fireGunFromShip(ship, gun)
void on_spawn_entry();            // outerSpawnShipEntity(missionShip, situation)
void on_despawn_entry();          // despawn(ship)
void on_delayed_despawn_entry();  // delayedDespawn(src, ship)
void on_ai_think_entry(const Loc &fn);  // per-frame ship AI (WC.EXE: ovr163:160E the capital ships', 13CF the fighters')
void on_ai_set_speed_entry();     // ovr143:0874

// --- replay ---------------------------------------------------------------
// Queue trampoline jobs for every event in a received frame.
void enqueue_remote_event(const Event &ev);
void enqueue_chat_display(int netShipId, const std::string &callsign, const std::string &text);
// The leader on a wingman's machine (wcnet_events.cpp): the game's own lines
// from its ship are not shown, and the ship goes by the leader's callsign.
void on_comm_message_entry();     // outerSomeCommThing(ship, message)
void note_leader_callsign(const std::string &callsign);
void leader_name_tick();
void leader_name_restore();
// A drone client: make a ship for the server's player (network id 0).
void enqueue_host_body();
void enqueue_gunner_seat();  // WC2: the gunner's view goes to the rear turret
// Turrets (WC2).  A gunner's machine reports its shots (on_turret_fire_shot)
// and the server fires them from its own ship (enqueue_turret_fire); the
// server's automatic fire leaves the gunner's turret alone
// (on_turret_auto_next).
// Cloaking (WC2): the server's AI switches a ship's cloak and tells the
// clients; theirs does not.  A drone keeps seeing cloaked ships: finding
// them is what it is for.
void on_cloak_entry(bool on);
void add_cloak_events(Frame *frame);  // server: the ships cloaked right now, for a start state
void on_turret_fire_shot();
void on_turret_auto_next();
void enqueue_turret_fire(const TurretFire &shot);
// Server: a client saw one of its own rocks or mines hit a ship of ours.
void enqueue_reported_damage(const Damage &reported);
// Server: a client left the mission; remove its copy here (with an explosion
// when it died, quietly when it landed).
void enqueue_wingman_lost(int slot, bool explode);
void enqueue_test_kill(int slot);
bool before_first_frame();   // a mission has started but its first flight frame has not run: the game is setting it up
// Test aid (the key script's "!hit=<qty>"): our own ship takes that much
// damage from dead ahead (or from behind), through the game's do_damage.
void enqueue_test_hit(int quantity, bool fromBehind);
void enqueue_test_mem();  // logs the game's free heap (WC2)
// A key pressed and released on this machine's game, as a player would
// (wcnet_hooks.cpp; the release follows from the periodic call).
void tap_key(int kbdKey);

// Asteroid and mine fields can be switched off (the host decides, see
// Session::request_rocks; WCROCKS=0 starts without them).  The flag is what
// the spawn hook consults; enqueue_rocks_change also queues the game calls
// that remove this machine's rocks now, or register the current nav point's
// fields again, and must be followed by a run of the trampoline.
enum RocksMode { ROCKS_OFF = 0, ROCKS_ON = 1, ROCKS_SOFT = 2 };
// Two humans' ships touching: the game's damage (0) or none (1, the
// default; the lobby's "Collisions" menu, WCWINGBUMPS=game, /bumps game).
enum WingBumps { WING_BUMPS_GAME = 0, WING_BUMPS_HARMLESS = 1 };
int wing_bumps_mode();
void set_wing_bumps_mode(int mode);
int rocks_mode();
inline bool rocks_enabled() { return rocks_mode() != ROCKS_OFF; }
void set_rocks_mode(int mode);
void enqueue_rocks_change(int mode);

// Client-side prediction of the local player's own shots.
struct PendingFire {
    Bit32u seq;
    int gun;
    int spawnedSlot;  // permanent slot the local fire used (missiles), or -1
};
void reset_pending_fires();

// Bookkeeping helpers shared with the session.
void despawn_all_ships();  // C++ translation of ovr140:1C16 for slots 0..9, no animations

}  // namespace wc

#endif
