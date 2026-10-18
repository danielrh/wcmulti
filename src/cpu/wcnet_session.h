/*
 *  Multiplayer session: who we are, who we talk to, and the per-frame
 *  exchange.
 *
 *  Authority model
 *  ---------------
 *  * The server owns the mission: it spawns and despawns entities, runs the
 *    AI, and computes damage to NPC ships.  Clients replay those events.
 *  * Every human's own ship is authoritative on that human's machine.
 *    Wing Commander applies damage to slot 0 with a different (player) model
 *    than to NPC slots, so damage to a player's ship is only ever computed on
 *    the machine where that ship is slot 0.  The owner replicates the
 *    resulting health snapshot (ShipHealth) to everyone else, and no machine
 *    ever runs do_damage/despawn against a remote player's ship.
 *  * Positions of a player's ship come from its owner every frame.
 *  * Shared fate: any player's ending (landed, died, ejected, quit) ends the
 *    mission for everyone with that same status.  A client reports its own
 *    ending with PlayerEnd; the server adopts it, and its MissionEnd frame
 *    carries it to the other clients.
 *
 *  Frame exchange
 *  --------------
 *  The server sets the pace (wcnet_perf.h): at the top of every in-flight
 *  frame it takes in whatever the clients have sent, runs its frame and sends
 *  one frame to each client.  It does not wait for a client's frame: only
 *  when a client is more than a few frames behind in what it says it has
 *  applied (Frame.ack) does the server wait for it.  A client sends its
 *  ShipUpdate plus any events at the top of its frame, then waits for the
 *  server's next frame (all that have arrived are applied, in order) before
 *  it simulates.  Events are replayed through the trampoline before the
 *  game simulates the frame.  Both waits pass in emulated time (the CPU
 *  idles, the game's interrupts and the clock run on); nothing blocks the
 *  emulator for a round trip.
 */
#ifndef WCNET_SESSION_H_
#define WCNET_SESSION_H_

#include <string>
#include "dosbox.h"
#include "wcnet_entities.h"
#include "wc.pb.h"

namespace wc {

enum Role { ROLE_SERVER, ROLE_CLIENT };

// How the frames are exchanged with a wingman (docs section 3b).  The
// server's setting (WCNET_MODE, the page's option, "/latency" in the comms
// prompt) is low latency unless told otherwise; in the automatic mode the
// server goes by what it measures of the link.  Its frames carry the mode
// in force to the clients.
enum ExchangeMode {
    MODE_AUTO = 0,  // a setting only, had by asking: the server goes by the link it measures
    MODE_LOW = 1,   // low latency: the client waits for every server frame; the server runs at most six frames ahead
    MODE_HIGH = 2,  // high latency: the client paces itself and takes the server's frames as they come,
                    // the server runs as far ahead as the round trip needs; everybody sees the others later
};

// What a client is in a mission (Game.seat).
enum Seat {
    SEAT_WINGMAN = 0,  // flies the ship the mission has in the wingman's slot
    SEAT_DRONE = 1,    // a mission the story flies alone: a ship of its own that exists on its
                       // machine only; nothing sees or hits it and it cannot shoot
    SEAT_GUNNER = 2,   // such a mission in a ship with turrets: its "own ship" is the leader's,
                       // mirrored from the server; it mans the turrets
};

class Session {
public:
    virtual ~Session() {}
    virtual Role role() const = 0;
    bool is_server() const { return role() == ROLE_SERVER; }
    bool is_client() const { return role() == ROLE_CLIENT; }

    // --- ownership --------------------------------------------------------
    // Slot flown by another human on this machine.
    virtual bool is_remote_player_slot(int slot) const = 0;
    bool is_player_slot(int slot) const { return slot == kPlayerSlot || is_remote_player_slot(slot); }
    // This machine's own seat (a server is the leader, never a drone).
    virtual Seat seat() const { return SEAT_WINGMAN; }
    bool is_drone() const { return seat() == SEAT_DRONE; }
    bool is_gunner() const { return seat() == SEAT_GUNNER; }
    // Server: the turret of our ship a gunner mans, or -1.
    virtual int gunner_turret() const { return -1; }
    // A drone can ride behind the leader instead of flying itself.
    virtual void toggle_chase() {}
    // A drone riding behind the leader is the leader's copilot: its keys go
    // to the server as Copilot actions (wcnet_hooks.cpp, wc_net_key_filter).
    virtual bool is_copilot() const { return false; }
    virtual void copilot(Copilot::Action action) { (void)action; }
    // (Which network ids humans fly is the server's word, Frame.humans: the
    // slot a client flies is decided with each mission's start state.)

    // --- events -------------------------------------------------------------
    // Add an event to the frame that will be sent at the end of this frame's
    // exchange (server: to all clients; client: to the server).
    virtual void queue_outgoing_event(const Event &ev) = 0;
    // Spawn bookkeeping for late joiners (server only).
    virtual void on_spawned(const Spawn &spawn) { (void)spawn; }
    virtual void on_despawned(int net) { (void)net; }

    // --- lifecycle hooks (see wcnet_hooks.cpp for where each fires) ---------
    virtual void on_mission_starting(int mission, int series) = 0;
    virtual void on_mission_reset() = 0;         // entities cleared (mission over)
    virtual void on_mission_victory_calc() = 0;  // score being computed
    virtual void on_mission_ended() = 0;         // back to the hangar flow
    virtual void on_frame_top() = 0;             // top of the in-flight loop
    virtual void on_async_tick() = 0;            // every ~1000 instructions
    virtual void on_barracks() = 0;              // entering the barracks
    virtual void check_mission_status() = 0;     // before the game tests dseg:00AE
    virtual void on_autopilot_begin(Bit16u camShipType, Bit16u camMode, Bit16u duration) {
        (void)camShipType; (void)camMode; (void)duration;
    }
    virtual void on_autopilot_finished() {}
    virtual void on_trampoline_idle() {}
    // The game's program has ended (its own exit, or an error that dumps it
    // to the DOS prompt) while the session is alive.
    virtual void on_program_ended() {}
    // WC2's scene player starts a scene from the flight loop on this machine
    // (a comm answer, or a cutscene of the mission's script).
    virtual void on_in_flight_scene(int scene) { (void)scene; }
    virtual bool in_space() const = 0;

    virtual void send_chat(const std::string &text) = 0;

    // Switch the asteroid and mine fields on, off or soft (RocksMode) for
    // everybody.  Only the server decides; a client's request just says so.
    // In flight it takes effect at the top of the next frame.
    virtual void request_rocks(int mode) = 0;
    virtual void request_wing_bumps(int mode) = 0;   // WingBumps: the host's choice

    // --- the exchange mode (ExchangeMode) ------------------------------------
    // Whether this machine paces its own flight frames (wcnet_perf.h): a
    // server always; a client only in the high-latency mode, where it does
    // not wait for the server's frames.
    virtual bool paces() const { return true; }
    // The setting (MODE_AUTO, MODE_LOW, MODE_HIGH): the server's to make; a
    // client's request only says so.
    virtual void request_mode(int setting) { (void)setting; }
    virtual int mode_setting() const { return MODE_AUTO; }
    // The mode in force.
    virtual int mode() const { return MODE_LOW; }
    // The round trip to the other side in ms (a server: the longest of its
    // clients'), or -1 before one is known.
    virtual double rtt_ms() const { return -1; }
};

// The active session, or NULL when not connected.
extern Session *g_session;

extern int in_simulation;
extern bool has_started_up;

// Jump into the campaign at a mission (MIS/SERIES environment variables).
void run_campaign(int missionId, int seriesId);
// Pilot identity (env WCCALLSIGN / WCLASTNAME override the save game).
std::string get_callsign();
std::string get_last_name();
void apply_pilot_names();  // WC2: the environment's names into the game (periodic)

}  // namespace wc

#endif
