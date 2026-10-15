#include <cctype>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <deque>
#include <string>
#include <vector>
#include "wcnet_events.h"
#include "wcnet_session.h"
#include "wcnet_memory.h"
#include "wcnet_code.h"
#include "wcnet_game.h"
#include "wcnet_log.h"
#include "cpu.h"
#include "regs.h"

namespace wc {

// Scratch vector inside the data segment for replayed damage positions.
#define kTmpVectorOff (ds::aLoadingWingCom)

// The named pilot (dseg:D1A2: 3 is Iceman) the leader's ship wears on a
// wingman's machine: see "The leader on a wingman's machine" below.
enum { kLeaderFace = 3 };

// Soft rocks (RocksMode): what a rock does to a player's own ship.
enum { kSoftRockDivisor = 16, kSoftRockMost = 80 };

static std::deque<PendingFire> g_pendingFires;
static Bit32u g_nextFireSeq = 1;

// True while a client runs the game's nav point setup for a spawn the server
// sent (NavSetupJob): the spawns and despawns nested in it are the client's
// own and are answered as outside a replay (the server decides).
static bool g_navSetup = false;

// True while the server runs do_damage for a hit a client reported (one of
// that client's own rocks or mines against a ship the server owns).  The call
// comes from the trampoline, but it is the server's own decision and is
// broadcast like any other.
static bool g_applyingReport = false;

static bool g_cinematic = false;  // the autopilot's fly-by is being replayed (see AutopilotJob)

void reset_pending_fires() {
    g_cinematic = false;  // (a mission that ended inside a fly-by)
    g_pendingFires.clear();
}

// ---------------------------------------------------------------------------
// Helpers

static void seed_rng(Bit32u seed) {
    wr32(ds::randomSeed, seed);
}

// Snapshot of which permanent slots are in use, to detect what a game call
// spawned (fireGunFromShip returns the bolt slot in AX for bolts but missiles
// take a permanent slot that we must map).
struct SlotSnapshot {
    Bit16u types[kMaxShipSlot + 1];
    void take() {
        for (int i = 0; i <= kMaxShipSlot; i++) {
            types[i] = entity_type(i);
        }
    }
    // First permanent slot that became used since take(); -1 if none.
    int newly_used() const {
        for (int i = kMinShipSlot; i <= kMaxShipSlot; i++) {
            if (entity_type(i) && !types[i]) {
                return i;
            }
        }
        return -1;
    }
};

// Which ships and missiles are on their way out: a ship the game has blown up
// keeps its slot, in AI state 9, until the explosion is over, and a missile's
// slot becomes its explosion.
struct DoomedSnapshot {
    bool doomed[kMaxShipSlot + 1];
    bool alive[kMaxShipSlot + 1];
    static bool is_doomed(int slot) {
        Bit16u type = entity_type(slot);
        return type < ET_MISSILE || (type >= ET_SHIP && rd16((Bit16u)(ds::shipAiState + 2 * slot)) == 9);
    }
    void take() {
        for (int i = 0; i <= kMaxShipSlot; i++) {
            alive[i] = entity_type(i) >= ET_MISSILE;
            doomed[i] = is_doomed(i);
        }
    }
    // Destroyed since take(): it was a live ship or missile then.
    bool destroyed_since(int slot) const {
        return alive[slot] && !doomed[slot] && slot_in_use(slot) && is_doomed(slot);
    }
};

static GameCall call_of(const Loc &fn) {
    return GameCall(fn);
}

// The shooter's locked target (dseg:C284[shooter], 0xff = none) decides
// whether a missile homes; it is read when the shot is fired.
static void capture_lock(WeaponFire *fire, int shooter) {
    Bit8u target = rd8((Bit16u)(ds::missileTarget + shooter));
    if (target != 0xff) {
        NetworkShipId t = NetworkShipId::from_local(target);
        if (!t.is_invalid()) {
            fire->set_target(t.to_net());
        }
    }
}

static void apply_lock(const WeaponFire &fire, int shooter) {
    Bit8u target = 0xff;
    if (fire.has_target()) {
        if (!g_entityMap || g_entityMap->is_mapped(fire.target())) {
            int local = NetworkShipId::from_net(fire.target()).to_local();
            if (local != kInvalidSlot && slot_in_use(local)) {
                target = (Bit8u)local;
            }
        }
    }
    wr8((Bit16u)(ds::missileTarget + shooter), target);
}

// A hit can knock a gun off a ship (WC.EXE ovr143:10C6: a random one, the
// rest move down and the count drops), and a launched missile leaves the
// table the same way.  The machines do not always lose the same gun, and
// from then on a gun number means a laser here and a missile there: a
// wingman's machine launched missiles nobody else had, which no one ever
// removed.  So a shot carries the shooter's record for its gun, and the
// machine replaying it puts that under the number first.
static Bit16u gun_table(int ship) {
    return (Bit16u)(ds::gunTable + g_params.gunsSize * ship);
}

static bool gun_in_table(int ship, int gun) {
    return ds::known(ds::gunTable) && ship >= 0 && ship <= kMaxShipSlot && gun >= 0 &&
           1 + g_params.gunSize * (gun + 1) <= g_params.gunsSize;
}

static void capture_gun(WeaponFire *fire, int ship, int gun) {
    if (!gun_in_table(ship, gun)) {
        return;
    }
    std::string record;
    for (int i = 0; i < g_params.gunSize; i++) {
        record.push_back((char)rd8((Bit16u)(gun_table(ship) + 1 + g_params.gunSize * gun + i)));
    }
    fire->set_gun(record);
    fire->set_guns(rd8(gun_table(ship)));
}

static void apply_gun(const WeaponFire &fire, int ship, int gun) {
    if (!fire.has_gun() || (int)fire.gun().size() != g_params.gunSize || !gun_in_table(ship, gun)) {
        return;
    }
    for (int i = 0; i < g_params.gunSize; i++) {
        wr8((Bit16u)(gun_table(ship) + 1 + g_params.gunSize * gun + i), (Bit8u)fire.gun()[i]);
    }
    if (fire.has_guns()) {
        wr8(gun_table(ship), (Bit8u)fire.guns());
    }
}

// ---------------------------------------------------------------------------
// Jobs

// Runs fireGunFromShip(ship, gun); afterwards records which permanent slot
// (missile) it used and either broadcasts the event or resolves a predicted
// local fire.
class FireJob : public VmJob {
public:
    enum Mode { BROADCAST, PREDICT, REPLAY };
    FireJob(Mode mode, const WeaponFire &fire, int localShip, int gun, bool replayingClientFire = false)
        : mode_(mode), fire_(fire), localShip_(localShip), gun_(gun), replayingClientFire_(replayingClientFire) {}

    virtual bool start() {
        // A shot of this machine's own runs in place of the game's call, and
        // the caller reads the bolt's slot from AX: none, when it is dropped.
        bool remote = mode_ == REPLAY || replayingClientFire_;
        if (localShip_ == kInvalidSlot || !slot_in_use(localShip_)) {
            wclog(2, "fire: shooter slot %d not usable", localShip_);
            if (!remote) {
                reg_eax = 0xffff;
            }
            return false;
        }
        if (free_temp_slots() < 5) {
            wclog(2, "fire: too few free slots, dropping shot");
            if (!remote) {
                reg_eax = 0xffff;
            }
            return false;
        }
        before_.take();
        started_ = true;
        if (remote) {
            apply_lock(fire_, localShip_);
            apply_gun(fire_, localShip_, gun_);
        }
        wclog(2, "%s fire: ship %d (net %d) gun %d target %s",
              mode_ == PREDICT ? "predict" : (mode_ == REPLAY ? "replay" : "local"),
              localShip_, fire_.shooter(), gun_,
              fire_.has_target() ? std::to_string(fire_.target()).c_str() : "none");
        call_of(code::fireGunFromShip).arg((Bit16u)localShip_).arg((Bit16u)gun_).invoke();
        return true;
    }

    virtual void finish() {
        if (!started_) {
            return;
        }
        int spawned = before_.newly_used();
        if (spawned != -1 && entity_type(spawned) != ET_MISSILE) {
            wclog(2, "fire spawned permanent slot %d of type %d", spawned, entity_type(spawned));
        }
        switch (mode_) {
        case BROADCAST: {
            Event ev;
            *ev.mutable_fire() = fire_;
            if (spawned != -1) {
                ev.mutable_fire()->set_ship_id(NetworkShipId::from_local(spawned).to_net());
            }
            g_session->queue_outgoing_event(ev);
            break;
        }
        case PREDICT: {
            PendingFire pf;
            pf.seq = fire_.client_seq();
            pf.gun = gun_;
            pf.spawnedSlot = spawned;
            g_pendingFires.push_back(pf);
            Event ev;
            *ev.mutable_fire() = fire_;
            g_session->queue_outgoing_event(ev);
            break;
        }
        case REPLAY:
            if (fire_.has_ship_id() && spawned != -1 && g_entityMap) {
                g_entityMap->record_spawn(fire_.ship_id(), spawned);
            }
            break;
        }
    }
    virtual const char *describe() const { return "fire"; }

private:
    Mode mode_;
    WeaponFire fire_;
    int localShip_, gun_;
    bool replayingClientFire_;  // server replaying a client's shot: use the client's lock
    SlotSnapshot before_;
    bool started_ = false;
};

class DamageJob : public VmJob {
public:
    // REPLAY: damage the server decided, replayed on a client.
    // REPORTED: on the server, a hit a client saw one of its own rocks or
    // mines do to a ship the server owns; the server applies it and its
    // do_damage hook broadcasts it (see g_applyingReport).
    enum Mode { REPLAY, REPORTED };
    DamageJob(Mode mode, const Damage &dam) : mode_(mode), dam_(dam) {}
    virtual bool start() {
        if (!dam_.has_ship_id()) {
            return false;
        }
        NetworkShipId dst = NetworkShipId::from_net(dam_.ship_id());
        int dstLocal = dst.to_local();
        if (g_entityMap && !g_entityMap->is_mapped(dam_.ship_id())) {
            wclog(2, "damage: target net %d unmapped, ignored", dam_.ship_id());
            return false;
        }
        if (dstLocal == kInvalidSlot || !slot_in_use(dstLocal)) {
            return false;
        }
        if (g_session->is_player_slot(dstLocal)) {
            // Damage to a human's ship is computed on that human's machine.
            return false;
        }
        if (mode_ == REPORTED && (!is_ship_slot(dstLocal) || entity_type(dstLocal) < ET_SHIP)) {
            return false;  // only ships take collision or blast damage
        }
        int srcLocal = kInvalidSlot;
        if (mode_ == REPLAY && dam_.has_shooter()) {
            int shooterLocal = NetworkShipId::from_net(dam_.shooter()).to_local();
            if (shooterLocal != kInvalidSlot) {
                // The game plays hit sounds/effects only when the source is a
                // bolt, so hand it one of the shooter's bolts when we have one.
                int bolt = find_bolt_of(shooterLocal);
                srcLocal = bolt != -1 ? bolt : shooterLocal;
            }
        }
        Vec3 pos = { 0, 0, 256 };
        if (dam_.has_pos()) {
            pos.x = dam_.pos().x();
            pos.y = dam_.pos().y();
            pos.z = dam_.pos().z();
        }
        write_vec(kTmpVectorOff, pos);
        if (mode_ == REPLAY && dam_.has_seed()) {
            seed_rng(dam_.seed());
        }
        if (mode_ == REPORTED) {
            wclog(2, "reported damage to %d qty %d", dstLocal, dam_.quantity());
            g_applyingReport = true;
            before_.take();
        } else {
            wclog(2, "replay damage %d -> %d (local %d -> %d) qty %d", dam_.has_shooter() ? (int)dam_.shooter() : -1,
                  dam_.ship_id(), srcLocal, dstLocal, dam_.quantity());
        }
        call_of(code::do_damage)
            .arg((Bit16u)srcLocal).arg((Bit16u)dstLocal)
            .arg((Bit16u)dam_.quantity()).arg((Bit16u)kTmpVectorOff)
            .invoke();
        return true;
    }
    virtual void finish() {
        if (mode_ != REPORTED || !g_applyingReport) {
            return;
        }
        g_applyingReport = false;
        // A kill inside that damage (the ship itself, or one caught in its
        // blast) ran natively, nested in our call: the clients hear of it
        // here, after the damage.
        for (int slot = kMinShipSlot; slot <= kMaxShipSlot; slot++) {
            if (!before_.destroyed_since(slot) || g_session->is_player_slot(slot)) {
                continue;
            }
            wclog(2, "slot %d destroyed by reported damage", slot);
            Event ev;
            Despawn *d = ev.mutable_despawn();
            d->set_ship_id(NetworkShipId::from_local(slot).to_net());
            d->set_explode(1);
            g_session->queue_outgoing_event(ev);
        }
    }
    virtual const char *describe() const { return "damage"; }

private:
    Mode mode_;
    Damage dam_;
    DoomedSnapshot before_;
};

class SpawnJob : public VmJob {
public:
    // mode BROADCAST: the server's own spawn (args from the intercepted call).
    // mode REPLAY: a spawn received from the server.
    enum Mode { BROADCAST, REPLAY };
    SpawnJob(Mode mode, const Spawn &spawn) : mode_(mode), spawn_(spawn) {}
    virtual bool start() {
        if (!spawn_.has_mission_ship_id() || !spawn_.has_situation_id()) {
            wclog(0, "invalid spawn event");
            return false;
        }
        if (mode_ == REPLAY && spawn_.has_seed()) {
            seed_rng(spawn_.seed());
        }
        wclog(2, "spawn mission ship %d situation %d", spawn_.mission_ship_id(), spawn_.situation_id());
        call_of(code::outerSpawnShipEntity)
            .arg((Bit16u)spawn_.mission_ship_id()).arg((Bit16u)spawn_.situation_id())
            .invoke();
        return true;
    }
    virtual void finish() {
        int slot = (int)(reg_eax & 0xffff);
        wclog(2, "spawn produced slot %d", slot);
        if (slot != kInvalidSlot && is_ship_slot(slot) && !slot_in_use(slot)) {
            wclog(0, "spawn of mission ship %d left slot %d empty", spawn_.mission_ship_id(), slot);
            slot = kInvalidSlot;
        }
        if (mode_ == BROADCAST) {
            if (slot != kInvalidSlot) {
                spawn_.set_ship_id(NetworkShipId::from_local(slot).to_net());
            }
            Event ev;
            *ev.mutable_spawn() = spawn_;
            g_session->queue_outgoing_event(ev);
            g_session->on_spawned(spawn_);
        } else if (spawn_.has_ship_id() && g_entityMap && slot != kInvalidSlot) {
            g_entityMap->record_spawn(spawn_.ship_id(), slot);
            // The mission setup gives a ship its pilot (dseg:D1A2) after the
            // spawn returns; on a client that setup never runs, this replayed
            // spawn only builds the ship, so the server sends the byte along.
            // The comms code names and draws the speaker from it and the
            // talk-to-wingman menu indexes by it, so it must be one of the
            // eight named pilots (0-7) for the body that stands in for the
            // host; 8, the player's own value, shows "(null)".
            int pilot = spawn_.has_pilot() ? (int)spawn_.pilot() : -1;
            if ((int)spawn_.ship_id() == g_entityMap->own_ship()) {
                wclog(2, "net %d is us (slot 0); slot %d becomes the server player's ship", spawn_.ship_id(), slot);
                const char *env = getenv("WCHOSTPILOT");  // 0-7: another named pilot for the host
                if (env && env[0] && atoi(env) >= 0 && atoi(env) <= 7) {
                    pilot = atoi(env);
                }
                if (is_wc1() && !(env && env[0] && atoi(env) >= 0 && atoi(env) <= 7)) {
                    // Not the mission's wingman, who is this player: see
                    // "The leader on a wingman's machine" above.
                    pilot = kLeaderFace;
                }
                wr8((Bit16u)(ds::shipStateByte + slot), (Bit8u)pilot);
                wclog(2, "slot %d wears pilot %d for the server player (spawn said %d)", slot, pilot,
                      spawn_.has_pilot() ? (int)spawn_.pilot() : -1);
            } else {
                wclog(2, "net %d -> slot %d", spawn_.ship_id(), slot);
                if (pilot >= 0) {
                    wr8((Bit16u)(ds::shipStateByte + slot), (Bit8u)pilot);
                }
            }
        }
    }
    virtual const char *describe() const { return "spawn"; }

private:
    Mode mode_;
    Spawn spawn_;
};

class DespawnJob : public VmJob {
public:
    enum Mode { BROADCAST, REPLAY, WINGMAN_LOST };
    DespawnJob(Mode mode, const Despawn &d, int localShip, int localSrc)
        : mode_(mode), d_(d), localShip_(localShip), localSrc_(localSrc) {}
    virtual bool start() {
        if (localShip_ == kInvalidSlot || !slot_in_use(localShip_)) {
            wclog(2, "despawn: slot %d already free", localShip_);
            return false;
        }
        if (mode_ == REPLAY && g_session->is_player_slot(localShip_)) {
            return false;
        }
        bool explode = d_.has_explode() && d_.explode();
        wclog(2, "despawn slot %d explode=%d", localShip_, (int)explode);
        if (explode) {
            call_of(code::delayedDespawn).arg((Bit16u)localSrc_).arg((Bit16u)localShip_).invoke();
        } else {
            call_of(code::despawn).arg((Bit16u)localShip_).invoke();
        }
        started_ = true;
        return true;
    }
    virtual void finish() {
        if (!started_) {
            return;  // nothing was there; nobody needs to hear about it
        }
        bool explode = d_.has_explode() && d_.explode();
        if (mode_ == BROADCAST || mode_ == WINGMAN_LOST) {
            Event ev;
            *ev.mutable_despawn() = d_;
            g_session->queue_outgoing_event(ev);
            if (!explode) {
                g_session->on_despawned(d_.ship_id());
            }
        } else if (!explode && g_entityMap) {
            g_entityMap->record_despawn(d_.ship_id());
        }
    }
    virtual const char *describe() const { return "despawn"; }

private:
    Mode mode_;
    Despawn d_;
    int localShip_, localSrc_;
    bool started_ = false;
};

// The autopilot's camera is replayed by calling the game's own animation,
// and that call is the game flying on for some hundred frames: ships move,
// the AI thinks, a nav point can come into reach and be set up.  None of
// that is the replay of anything the server did.  While it runs the hooks
// treat what the game does as they do in an ordinary frame (g_cinematic,
// replaying()).

// A gunner sits in a turret: the rear one when the mission begins (the game
// itself starts everybody in the pilot's seat), and after an autopilot,
// which puts every view back to the pilot's, the one it sat in when the
// autopilot began.  Both by the call the game's own F2..F4 keys make.
static int g_viewBeforeAutopilot = -1;

class ViewJob : public VmJob {
public:
    ViewJob(int view, const char *why) : view_(view), why_(why) {}
    virtual bool start() {
        if (!code::setView.known() || !ds::known(ds::cockpitView)) {
            return false;
        }
        wclog(2, "view %d (%s)", view_, why_);
        wr16(ds::viewKeyState, 0);
        call_of(code::setView).arg((Bit16u)view_).arg(0).invoke();
        return true;
    }
    virtual const char *describe() const { return "view"; }

private:
    int view_;
    const char *why_;
};

void enqueue_gunner_seat() {
    enum { kRearView = 3 };
    g_trampoline.enqueue(new ViewJob(kRearView, "the gunner's seat at the start"));
}

class AutopilotJob : public VmJob {
public:
    explicit AutopilotJob(const AutoPilotEvent &ape) : ape_(ape) {}
    virtual void finish() {
        g_cinematic = false;
        bool finish = ape_.has_finish_camera() && ape_.finish_camera();
        if (finish && g_viewBeforeAutopilot > 0) {
            g_trampoline.enqueue(new ViewJob(g_viewBeforeAutopilot, "back in the turret after the autopilot"));
            g_viewBeforeAutopilot = -1;
        }
    }
    virtual bool start() {
        if (!(ape_.has_finish_camera() && ape_.finish_camera()) && g_session && g_session->is_gunner() &&
            ds::known(ds::cockpitView) && g_viewBeforeAutopilot < 0) {
            enum { kInTurret = 4 };
            g_viewBeforeAutopilot = rd16(ds::cameraMode) == kInTurret ? rd8(ds::cockpitView) : 0;
        }
        // The server sends one event when its autopilot camera starts and one
        // when it finishes.  Both are replayed by calling the game's
        // autoAnimation: the finish event carries no camera parameters, and
        // the resulting (0, 0, 0) call is what takes the client out of the
        // cinematic and back to the cockpit (as the original wc_net.cpp did);
        // skipping it leaves the client in the letterboxed camera view.
        bool finish = ape_.has_finish_camera() && ape_.finish_camera();
        wclog(2, "replay autopilot %s (%d, %d, %d)", finish ? "finish" : "camera",
              ape_.cam_ship_type(), ape_.cam_mode(), ape_.duration());
        g_cinematic = true;
        call_of(code::autoAnimation)
            .arg((Bit16u)ape_.cam_ship_type()).arg((Bit16u)ape_.cam_mode()).arg((Bit16u)ape_.duration())
            .invoke();
        return true;
    }
    virtual const char *describe() const { return "autopilot"; }

private:
    AutoPilotEvent ape_;
};

// The leader on a wingman's machine.  The leader's own ship carries "the
// player" there, which is nobody to the comms code of another machine, so
// the body that stands in for it wears one of the eight named pilots (see
// SpawnJob): his face on the comm display, his name in the targeting
// computer, and, because the game takes any such ship for its own wingman,
// his lines whenever the game thinks a wingman would speak.  It used to be
// the mission's wingman, the very pilot the second player is: Angel read
// "ANGEL: It is our duty to engage." from a ship called ANGEL.  Now:
//  * the body wears Iceman unless WCHOSTPILOT says otherwise (kLeaderFace);
//  * the game's own lines from that ship are not shown (on_comm_message_entry):
//    a person flies it, and says what he types;
//  * the pilot's name is the leader's callsign for the flight
//    (leader_name_tick; WC2: the name of the ship's mission record), which
//    is what the targeting computer and the comm menu print;
//  * the name painted on the helmet of the comm picture is painted out by
//    the renderer (wc_net_helmet_name, BlankHelmetName in sdlmain.cpp).
static bool g_chatLine = false;
static std::string g_leaderCallsign;
static std::string g_savedPilotName[8];
static bool g_pilotNameSaved[8] = { false, false, false, false, false, false, false, false };

void note_leader_callsign(const std::string &callsign) {
    if (callsign.length() > 1 && callsign != "BLUEHAIR") {
        g_leaderCallsign = callsign;
    }
}

static std::string leader_callsign() {
    const char *env = getenv("WCHOSTCALLSIGN");  // (the page knows it from the room's roster)
    return env && env[0] ? std::string(env) : g_leaderCallsign;
}

void on_comm_message_entry() {
    Session *s = g_session;
    if (!s || !s->is_client() || g_chatLine) {
        return;
    }
    int ship = call_arg16(code::showCommMessage, 0);
    if (s->is_remote_player_slot(ship)) {
        wclog(2, "the game's own line %d for the leader's ship (slot %d) is not shown", call_arg16(code::showCommMessage, 1), ship);
        return_from_call(code::showCommMessage, 0);
    }
}

// Once in a while in flight, on a client: the leader's ship is called by the
// leader's callsign.
void leader_name_tick() {
    static int frames = 0;
    Session *s = g_session;
    if (!s || !s->is_client() || !g_entityMap || (frames++ % 20) != 0 || !g_entityMap->is_mapped(0)) {
        return;
    }
    std::string want = leader_callsign();
    int slot = g_entityMap->net_to_local(0);
    if (want.empty() || slot <= 0 || slot > kMaxShipSlot || !slot_in_use(slot)) {
        return;
    }
    if (is_wc1() && ds::known(ds::pilotRecords)) {
        int pilot = rd8((Bit16u)(ds::shipStateByte + slot));
        if (pilot > 7) {
            return;
        }
        if (want.size() > 13) {
            want.resize(13);
        }
        for (size_t i = 0; i < want.size(); i++) {
            want[i] = (char)toupper((unsigned char)want[i]);  // (as the game writes its pilots)
        }
        Bit16u name = (Bit16u)(rd16((Bit16u)(ds::pilotRecords + 2 * pilot)) + 0xE);
        std::string now = read_cstring(name, 14);
        if (now != want) {
            if (!g_pilotNameSaved[pilot]) {
                g_pilotNameSaved[pilot] = true;
                g_savedPilotName[pilot] = now;
            }
            write_cstring(name, 14, want);
            wclog(1, "the leader's ship (slot %d, pilot %d: \"%s\") is called \"%s\" here", slot, pilot, now.c_str(), want.c_str());
        }
    } else if (is_wc2() && ds::known(ds::slotMissionShip) && ds::known(ds::missionShipTable)) {
        int ms = (Bit16s)rd16((Bit16u)(ds::slotMissionShip + 2 * slot));
        if (ms < 0 || ms >= 64) {
            return;
        }
        if (want.size() > 19) {
            want.resize(19);
        }
        Bit16u name = (Bit16u)(ds::missionShipTable + g_params.missionShipSize * ms);
        std::string now = read_cstring(name, 20);
        if (now != want) {
            write_cstring(name, 20, want);  // (the mission's own data: loaded again with the next mission)
            wclog(1, "the leader's ship (slot %d, \"%s\") is called \"%s\" here", slot, now.c_str(), want.c_str());
        }
    }
}

// The pilots' own names again when the flight is over (the kill board, the
// funerals and the saved game are theirs).
void leader_name_restore() {
    if (!is_wc1() || !ds::known(ds::pilotRecords)) {
        return;
    }
    for (int pilot = 0; pilot < 8; pilot++) {
        if (g_pilotNameSaved[pilot]) {
            g_pilotNameSaved[pilot] = false;
            write_cstring((Bit16u)(rd16((Bit16u)(ds::pilotRecords + 2 * pilot)) + 0xE), 14, g_savedPilotName[pilot]);
        }
    }
}

class ChatJob : public VmJob {
public:
    ChatJob(int netShip, const std::string &text) : netShip_(netShip), text_(text) {}
    virtual bool start() {
        g_chatLine = true;  // (this call of the comm display is ours: on_comm_message_entry)
        int local = NetworkShipId::from_net(netShip_).to_local();
        if (local == kInvalidSlot) {
            local = kPlayerSlot;
        }
        // (ship, messageId).  The call sets up the comm display (face,
        // timer) and copies message 0 -- an empty string, shown as
        // "(null)" -- into commGlobalTxt; the VDU renders that buffer on
        // the following frames, so our text has to go in after the call
        // returns (finish), exactly as the original wc_net.cpp did.  With
        // the arguments the other way round the game shows message #slot
        // from ship 0 instead.
        call_of(code::showCommMessage).arg((Bit16u)local).arg(0).invoke();
        return true;
    }
    virtual void finish() {
        g_chatLine = false;
        if (ds::known(ds::commGlobalTxt)) {
            write_cstring(ds::commGlobalTxt, 80, text_);
        }
    }
    virtual const char *describe() const { return "chat"; }

private:
    int netShip_;
    std::string text_;
};

// WC2 loads a nav point's ship types when the player gets there
// (ovr116:1511), and a ship of a type that is not loaded cannot be spawned.
// The server's spawns for a nav point can reach a client before its own
// player is near enough (the autopilot's arrival, most of all), so the
// client sets the nav point up first; its own later check finds it current.
class NavSetupJob : public VmJob {
public:
    explicit NavSetupJob(int nav) : nav_(nav) {}
    virtual bool start() {
        if (rd16(ds::currentNavPoint) == nav_) {
            return false;
        }
        wclog(2, "setting up nav point %d for the server's spawns (ours was %d)", nav_, rd16(ds::currentNavPoint));
        g_navSetup = true;
        call_of(code::enterNavPoint).arg((Bit16u)nav_).invoke();
        return true;
    }
    virtual void finish() {
        g_navSetup = false;
    }
    virtual const char *describe() const { return "nav setup"; }

private:
    int nav_;
};

// A drone's copy of the server's player.  A wingman gets that body from the
// spawn of the ship he flies himself (EntityMap::record_spawn); a drone
// flies nothing the mission knows, so it spawns the mission's player ship a
// second time.  The game refuses to spawn a mission ship that is already in
// a slot (ours, slot 0), so the slot's mission ship is hidden for the call;
// and the record says "flown by the player", which is an error
// (ovr116:200D) for any slot but 0, so for the call it is an ordinary pilot.
class HostBodyJob : public VmJob {
public:
    virtual bool start() {
        if (!ds::known(ds::slotMissionShip) || !ds::known(ds::playerMissionShip) || !g_params.missionShipAiOff || !g_entityMap) {
            return false;
        }
        enum { kOrdinaryPilot = 4 };
        own_ = rd16(ds::slotMissionShip);
        wr16(ds::slotMissionShip, 0xffff);
        aiOff_ = (Bit16u)(ds::missionShipTable + g_params.missionShipSize * rd16(ds::playerMissionShip) + g_params.missionShipAiOff);
        ai_ = rd16(aiOff_);
        wr16(aiOff_, kOrdinaryPilot);
        started_ = true;
        call_of(code::outerSpawnShipEntity).arg(rd16(ds::playerMissionShip)).arg(rd16(ds::currentNavPoint)).invoke();
        return true;
    }
    virtual void finish() {
        if (!started_) {
            wclog(0, "this game has no ship for the leader on a drone's machine");
            return;
        }
        wr16(ds::slotMissionShip, own_);
        wr16(aiOff_, ai_);
        int slot = (int)(reg_eax & 0xffff);
        if (slot == kInvalidSlot || !is_ship_slot(slot) || slot == kPlayerSlot || !slot_in_use(slot)) {
            wclog(0, "no ship for the leader on this machine (slot %d)", slot);
            return;
        }
        g_entityMap->map(kPlayerSlot, slot);
        wclog(1, "the leader's ship is slot %d here", slot);
    }
    virtual const char *describe() const { return "leader's ship"; }

private:
    bool started_ = false;
    Bit16u own_ = 0xffff;
    Bit16u aiOff_ = 0, ai_ = 0;
};

void enqueue_host_body() {
    g_trampoline.enqueue(new HostBodyJob());
}

// ---------------------------------------------------------------------------
// Cloaking (WC2)
//
// A stealth fighter's AI cloaks it (ovr133:0034) and uncloaks it to shoot
// (ovr133:0085); a word per ship says which (ds::cloakState).  Every machine
// runs the AI, so every machine would cloak its copies on its own: the
// server's calls are sent as events and a client's own are dropped.

enum { kCloaked = 1 };

class CloakJob : public VmJob {
public:
    CloakJob(int slot, bool on) : slot_(slot), on_(on) {}
    virtual bool start() {
        const Loc &fn = on_ ? code::cloak : code::uncloak;
        if (!fn.known() || !is_ship_slot(slot_) || !slot_in_use(slot_)) {
            return false;
        }
        wclog(2, "slot %d %s", slot_, on_ ? "cloaks" : "uncloaks");
        call_of(fn).arg((Bit16u)slot_).invoke();
        return true;
    }
    virtual const char *describe() const { return "cloak"; }

private:
    int slot_;
    bool on_;
};

void on_cloak_entry(bool on) {
    Session *s = g_session;
    if (!s || (g_trampoline.is_running() && !g_cinematic)) {
        return;
    }
    const Loc &fn = on ? code::cloak : code::uncloak;
    int ship = call_arg16(fn, 0);
    if (s->is_client()) {
        return_from_call(fn, 0);  // the server's AI decides
        return;
    }
    bool cloaked = is_ship_slot(ship) && rd16((Bit16u)(ds::cloakState + 2 * ship)) == kCloaked;
    if (!is_ship_slot(ship) || on == cloaked) {
        return;  // nothing changes (the AI asks again and again)
    }
    Event ev;
    ev.mutable_cloak()->set_ship_id(NetworkShipId::from_local(ship).to_net());
    ev.mutable_cloak()->set_on(on);
    s->queue_outgoing_event(ev);
    wclog(2, "slot %d %s", ship, on ? "cloaks" : "uncloaks");
}

void add_cloak_events(Frame *frame) {
    if (!ds::known(ds::cloakState)) {
        return;
    }
    for (int slot = kPlayerSlot + 1; slot <= kMaxShipSlot; slot++) {
        if (slot_in_use(slot) && rd16((Bit16u)(ds::cloakState + 2 * slot)) == kCloaked) {
            Cloak *c = frame->add_event()->mutable_cloak();
            c->set_ship_id(NetworkShipId::from_local(slot).to_net());
            c->set_on(true);
        }
    }
}

// ---------------------------------------------------------------------------
// Turrets (WC2)
//
// The fire key in a turret (ovr136:0510) makes a pair of bolts from ship 0
// along the camera entity, which is what the turret's sight moves.  A
// gunner's "own ship" is the leader's, mirrored, so its shots have to happen
// on the server: the gunner's machine sends the camera's orientation and its
// offset from the ship, and the server runs the same function with its own
// camera standing there for the moment.  Energy and cooldown were the
// gunner's to check; the server's own are put back.

static void put_vec(Vector *out, const Vec3 &v) {
    out->set_x(v.x);
    out->set_y(v.y);
    out->set_z(v.z);
}

static Vec3 get_vec(const Vector &in) {
    Vec3 v = { in.x(), in.y(), in.z() };
    return v;
}

class TurretFireJob : public VmJob {
public:
    explicit TurretFireJob(const TurretFire &shot) : shot_(shot) {}
    virtual bool start() {
        if (!code::turretFire.known() || !slot_in_use(kPlayerSlot) || !shot_.has_right() || !shot_.has_up() || !shot_.has_fore()) {
            return false;
        }
        const int cam = kCameraSlot;
        for (int i = 0; i < 4; i++) {
            saved_[i] = read_vec(vec_slot(base(i), cam));
        }
        barrels_ = rd16(ds::turretBarrels);
        energy_ = rd16(ds::turretEnergy);
        cooldown_ = rd16(ds::turretCooldown);
        Vec3 pos = read_vec(vec_slot(ds::gPositionVector, kPlayerSlot));
        if (shot_.has_offset()) {
            Vec3 off = get_vec(shot_.offset());
            pos.x += off.x;
            pos.y += off.y;
            pos.z += off.z;
        }
        write_vec(vec_slot(ds::gOrientationRightVector, cam), get_vec(shot_.right()));
        write_vec(vec_slot(ds::gOrientationUpVector, cam), get_vec(shot_.up()));
        write_vec(vec_slot(ds::gOrientationFrontVector, cam), get_vec(shot_.fore()));
        write_vec(vec_slot(ds::gPositionVector, cam), pos);
        wr16(ds::turretBarrels, (Bit16u)shot_.barrels());
        wr16(ds::turretEnergy, 100);
        wr16(ds::turretCooldown, 0);
        started_ = true;
        call_of(code::turretFire).invoke();
        return true;
    }
    virtual void finish() {
        if (!started_) {
            return;
        }
        for (int i = 0; i < 4; i++) {
            write_vec(vec_slot(base(i), kCameraSlot), saved_[i]);
        }
        wr16(ds::turretBarrels, barrels_);
        wr16(ds::turretEnergy, energy_);
        wr16(ds::turretCooldown, cooldown_);
    }
    virtual const char *describe() const { return "turret fire"; }

private:
    static Bit16u base(int i) {
        return i == 0 ? ds::gOrientationRightVector : i == 1 ? ds::gOrientationUpVector
             : i == 2 ? ds::gOrientationFrontVector : ds::gPositionVector;
    }
    TurretFire shot_;
    Vec3 saved_[4];
    Bit16u barrels_ = 0, energy_ = 0, cooldown_ = 0;
    bool started_ = false;
};

void enqueue_turret_fire(const TurretFire &shot) {
    wclog(2, "the gunner fires a turret");
    g_trampoline.enqueue(new TurretFireJob(shot));
}

void on_turret_fire_shot() {
    Session *s = g_session;
    if (!s || !s->is_client() || !s->is_gunner() || g_trampoline.is_running()) {
        return;
    }
    const int cam = kCameraSlot;
    Event ev;
    TurretFire *shot = ev.mutable_turret();
    put_vec(shot->mutable_right(), read_vec(vec_slot(ds::gOrientationRightVector, cam)));
    put_vec(shot->mutable_up(), read_vec(vec_slot(ds::gOrientationUpVector, cam)));
    put_vec(shot->mutable_fore(), read_vec(vec_slot(ds::gOrientationFrontVector, cam)));
    Vec3 at = read_vec(vec_slot(ds::gPositionVector, cam)), ship = read_vec(vec_slot(ds::gPositionVector, kPlayerSlot));
    Vec3 off = { at.x - ship.x, at.y - ship.y, at.z - ship.z };
    put_vec(shot->mutable_offset(), off);
    shot->set_barrels(rd16(ds::turretBarrels));
    s->queue_outgoing_event(ev);
    wclog(2, "turret shot reported to the server");
}

void on_turret_auto_next() {
    Session *s = g_session;
    if (!s || !s->is_server() || (reg_esi & 0xffff) != (Bit32u)kPlayerSlot) {
        return;
    }
    int turret = (Bit16s)mem_readw(SegPhys(ss) + ((reg_ebp - 0x1c) & 0xffff));
    if (turret == s->gunner_turret()) {
        wclog(4, "automatic fire leaves turret %d to the gunner", turret);
        reg_eip = code::turretAutoSkip.off;
    }
}

// Removes this machine's rocks or mines and forgets the nav point's fields.
class ClearFieldsJob : public VmJob {
public:
    virtual bool start() {
        call_of(code::clearFields).invoke();
        return true;
    }
    virtual void finish() {
        wr16(ds::fieldCount, 0);
        wr16(ds::fieldRockTally, 0);
    }
    virtual const char *describe() const { return "clear fields"; }
};

// Registers one field of the current nav point again (the game's own call;
// the spawn hook lets it through because it comes from the trampoline).
class RegisterFieldJob : public VmJob {
public:
    RegisterFieldJob(int missionShip, int navPoint) : missionShip_(missionShip), navPoint_(navPoint) {}
    virtual bool start() {
        wclog(2, "registering field %d of nav point %d again", missionShip_, navPoint_);
        call_of(code::outerSpawnShipEntity).arg((Bit16u)missionShip_).arg((Bit16u)navPoint_).invoke();
        return true;
    }
    virtual const char *describe() const { return "register field"; }

private:
    int missionShip_, navPoint_;
};

// ---------------------------------------------------------------------------
// Rocks on or off

static int g_rocksMode = -1;  // -1: not yet read from WCROCKS

int rocks_mode() {
    if (g_rocksMode < 0) {
        const char *env = getenv("WCROCKS");
        g_rocksMode = !env || !env[0] ? ROCKS_ON
            : (env[0] == '0' || env[0] == 'n' || env[0] == 'N' || !strcasecmp(env, "off")) ? ROCKS_OFF
            : (env[0] == '2' || !strcasecmp(env, "soft")) ? ROCKS_SOFT : ROCKS_ON;
    }
    return g_rocksMode;
}

void set_rocks_mode(int mode) {
    mode = mode == ROCKS_OFF ? ROCKS_OFF : mode == ROCKS_SOFT ? ROCKS_SOFT : ROCKS_ON;
    if (rocks_mode() != mode) {
        wclog(1, "asteroid and mine fields are %s", mode == ROCKS_OFF ? "off" : mode == ROCKS_SOFT ? "on, rocks soft" : "on");
    }
    g_rocksMode = mode;
}

void enqueue_rocks_change(int mode) {
    set_rocks_mode(mode);
    if (!rocks_enabled()) {
        g_trampoline.enqueue(new ClearFieldsJob());
        return;
    }
    if (rd16(ds::fieldCount) != 0) {
        return;  // still registered
    }
    int nav = rd16(ds::currentNavPoint);
    for (int i = 0; i < 10 && g_params.navPointShipsOff; i++) {
        int ship = rd16((Bit16u)(ds::navPointTable + g_params.navPointSize * nav + g_params.navPointShipsOff + 2 * i));
        if (is_field_mission_ship(ship)) {
            g_trampoline.enqueue(new RegisterFieldJob(ship, nav));
        }
    }
}

// ---------------------------------------------------------------------------
// Interception

// A call the game makes while a trampoline job runs is part of what that job
// replays (the bolt a replayed shot makes, the despawns inside a replayed
// kill) and runs as the game wrote it, unless the job is the autopilot's
// fly-by (g_cinematic).  That used to count as well: a client whose own ship
// came into reach of the next nav point during the fly-by set the nav point
// up itself, with ships of its own that no network id ever named; the
// server's spawns for them then found them "already there", and every shot,
// hit and kill of theirs was ignored on that client.
static bool replaying() {
    return g_trampoline.is_running() && !g_cinematic;
}

// The network id of the ship behind a hit or a kill, when a ship is behind
// it: a rock or a mine belongs to nobody and exists on one machine only, so
// its slot number means nothing anywhere else.
static bool ship_behind(int src, int *net) {
    NetworkShipId shooter = NetworkShipId::from_top_level_local(src);
    if (shooter.is_invalid() || !is_ship_slot(top_level_parent(src))) {
        return false;
    }
    *net = shooter.to_net();
    return true;
}

// Client: one of this machine's own rocks or mines hit a ship the server
// owns.  The server cannot see that rock, so tell it; the damage comes back
// with the server's next frame like any other.
static void report_local_hit(Session *s, int dst, Bit16u quantity, Bit16u vecOff) {
    if (g_entityMap && !g_entityMap->is_local_mapped(dst)) {
        return;
    }
    Event ev;
    Damage *dam = ev.mutable_damage();
    dam->set_ship_id(NetworkShipId::from_local(dst).to_net());
    dam->set_quantity(quantity);
    Vec3 v = read_vec(vecOff);
    dam->mutable_pos()->set_x(v.x);
    dam->mutable_pos()->set_y(v.y);
    dam->mutable_pos()->set_z(v.z);
    wclog(2, "reporting a hit by our own rock or mine on slot %d (net %d) qty %d", dst, dam->ship_id(), quantity);
    s->queue_outgoing_event(ev);
}

void on_do_damage_entry() {
    Session *s = g_session;
    int src = call_arg16(code::do_damage, 0);
    int dst = call_arg16(code::do_damage, 1);
    Bit16u quantity = call_arg16(code::do_damage, 2);
    Bit16u vecOff = call_arg16(code::do_damage, 3);
    if (dst == kPlayerSlot && src >= 0 && src < kNumSlots && entity_type(src) == ET_ASTEROID && rocks_mode() == ROCKS_SOFT) {
        // Soft rocks: the game's (closing speed)^2 / 2 runs to 1000-3000 for
        // a head-on rock, against a Hornet's 85 of front shield and armor
        // (70 on a side).  A sixteenth of it, and never more than 80, takes
        // the shield and most of the armor but leaves any fresh ship alive
        // after one rock, wherever it lands.
        Bit16u soft = (Bit16u)(quantity / kSoftRockDivisor);
        soft = soft < 1 ? 1 : soft > kSoftRockMost ? (Bit16u)kSoftRockMost : soft;
        wclog(2, "soft rock: %d becomes %d", quantity, soft);
        quantity = soft;
        set_call_arg16(code::do_damage, 2, quantity);
    }
    if (dst == kPlayerSlot && src >= 0 && src < kNumSlots && entity_type(src) != ET_BOLT) {
        // What hit our own ship, other than gunfire (also when flying alone).
        wclog(2, "our ship is hit by slot %d (type %d, ship type %d) qty %d", src, entity_type(src),
              rd16((Bit16u)(ds::gInstanceShipTypes + 2 * src)), quantity);
    }
    if (!s) {
        return;
    }

    if (s->is_remote_player_slot(dst) || (dst == kPlayerSlot && s->is_drone())) {
        // Only the owner's machine may damage a human's ship (see session.h),
        // and nothing touches a drone.
        return_from_call(code::do_damage, 0);
        return;
    }
    if (replaying() && !g_applyingReport) {
        return;  // a replayed event: run natively
    }
    if (s->is_client()) {
        if (dst == kPlayerSlot) {
            return;  // our own ship: the player model runs here and only here
        }
        if (!is_ship_slot(dst)) {
            return;  // rocks, mines, bolts: every machine has its own
        }
        if (quantity != 0 && s->seat() == SEAT_WINGMAN && is_local_hazard(src) && slot_in_use(dst) && entity_type(dst) >= ET_SHIP) {
            report_local_hit(s, dst, quantity, vecOff);
        }
        return_from_call(code::do_damage, 0);  // NPC damage is decided by the server
        return;
    }
    if (dst == kPlayerSlot) {
        return;  // server's own ship: native, nobody else needs it
    }
    if (!is_ship_slot(dst)) {
        return;  // rocks, mines, bolts: not replicated
    }
    Event ev;
    Damage *dam = ev.mutable_damage();
    dam->set_ship_id(NetworkShipId::from_local(dst).to_net());
    int shooter;
    if (ship_behind(src, &shooter)) {
        dam->set_shooter(shooter);
    }
    dam->set_quantity(quantity);
    Vec3 v = read_vec(vecOff);
    dam->mutable_pos()->set_x(v.x);
    dam->mutable_pos()->set_y(v.y);
    dam->mutable_pos()->set_z(v.z);
    dam->set_seed(rd32(ds::randomSeed));
    s->queue_outgoing_event(ev);
    // ...and let the game apply it here with its original arguments.
}

void on_fire_entry() {
    Session *s = g_session;
    if (!s) {
        return;
    }
    int ship = call_arg16(code::fireGunFromShip, 0);
    int gun = call_arg16(code::fireGunFromShip, 1);
    if (replaying()) {
        wclog(3, "fire inside the %s job: ship %d gun %d", g_trampoline.current_name(), ship, gun);
        return;
    }
    if (s->is_client()) {
        if (ship != kPlayerSlot || s->seat() != SEAT_WINGMAN || g_trampoline.is_running()) {
            // Other ships fire through server events; a drone has no guns,
            // and a gunner has the turrets, not the pilot's guns.  (And
            // nobody's guns fire during the autopilot's fly-by.)
            return_from_call(code::fireGunFromShip, 0xffff);
            return;
        }
        WeaponFire fire;
        fire.set_shooter(NetworkShipId::from_local(ship).to_net());
        fire.set_gun_id(gun);
        fire.set_client_seq(g_nextFireSeq++);
        capture_lock(&fire, ship);
        capture_gun(&fire, ship, gun);
        g_trampoline.enqueue(new FireJob(FireJob::PREDICT, fire, ship, gun));
        g_trampoline.run_instead_of_current_call(code::fireGunFromShip);
        return;
    }
    if (s->is_remote_player_slot(ship)) {
        return_from_call(code::fireGunFromShip, 0xffff);  // the AI must not fire a human's ship
        return;
    }
    WeaponFire fire;
    fire.set_shooter(NetworkShipId::from_local(ship).to_net());
    fire.set_gun_id(gun);
    capture_lock(&fire, ship);
    capture_gun(&fire, ship, gun);
    g_trampoline.enqueue(new FireJob(FireJob::BROADCAST, fire, ship, gun));
    g_trampoline.run_instead_of_current_call(code::fireGunFromShip);
}

void on_spawn_entry() {
    Session *s = g_session;
    if (is_field_mission_ship(call_arg16(code::outerSpawnShipEntity, 0)) && !rocks_enabled()) {
        return_from_call(code::outerSpawnShipEntity, 0xffff);  // rocks are switched off: the nav point has no field
        return;
    }
    if (!s || (replaying() && !g_navSetup)) {
        return;
    }
    if (is_field_mission_ship(call_arg16(code::outerSpawnShipEntity, 0))) {
        // Not a ship: the nav point's asteroid or mine field (ovr145:1183
        // only adds it to the field table).  Every machine sets up a nav
        // point for itself when its own player gets there, clearing that
        // table first, so each registers the field here and then keeps its
        // own rocks around its own ship.
        wclog(2, "mission ship %d is a field: registered locally", call_arg16(code::outerSpawnShipEntity, 0));
        return;
    }
    if (s->is_client()) {
        return_from_call(code::outerSpawnShipEntity, 0xffff);  // the server decides what exists
        return;
    }
    Spawn spawn;
    spawn.set_mission_ship_id(call_arg16(code::outerSpawnShipEntity, 0));
    spawn.set_situation_id(call_arg16(code::outerSpawnShipEntity, 1));
    spawn.set_seed(rd32(ds::randomSeed));
    g_trampoline.enqueue(new SpawnJob(SpawnJob::BROADCAST, spawn));
    g_trampoline.run_instead_of_current_call(code::outerSpawnShipEntity);
}

static void intercept_despawn(bool explode) {
    Session *s = g_session;
    if (!s) {
        return;
    }
    const Loc &fn = explode ? code::delayedDespawn : code::despawn;
    int ship = explode ? call_arg16(fn, 1) : call_arg16(fn, 0);
    int src = explode ? call_arg16(fn, 0) : kInvalidSlot;
    if (!is_ship_slot(ship)) {
        if (explode && (entity_type(ship) == ET_ASTEROID || entity_type(ship) == ET_MINE)) {
            wclog(2, "%s in slot %d destroyed", entity_type(ship) == ET_ASTEROID ? "rock" : "mine", ship);
        }
        return;  // temporary entities are local on every machine
    }
    if (!slot_in_use(ship)) {
        // Mission setup despawns every slot to reset its bookkeeping (pilot
        // 0xff, comm flags, cull status, ...).  Nothing to tell anyone, and
        // skipping it leaves the unused slots as whatever the last mission
        // left there -- pilot 0 made them look like extra copies of the
        // wingman to the comms code.
        return;
    }
    if (s->is_remote_player_slot(ship)) {
        return_from_call(fn, 0);  // a human's ship only leaves when its owner says so
        return;
    }
    if (replaying() && !g_navSetup) {
        return;
    }
    if (ship == kPlayerSlot) {
        return;  // our own death/landing: the game handles it, mission status tells the others
    }
    if (s->is_client()) {
        return_from_call(fn, 0);
        return;
    }
    Despawn d;
    d.set_ship_id(NetworkShipId::from_local(ship).to_net());
    if (explode) {
        d.set_explode(1);
        int shooter;
        if (ship_behind(src, &shooter)) {
            d.set_shooter(shooter);
        }
    }
    g_trampoline.enqueue(new DespawnJob(DespawnJob::BROADCAST, d, ship, src));
    g_trampoline.run_instead_of_current_call(fn);
}

void on_despawn_entry() { intercept_despawn(false); }
void on_delayed_despawn_entry() { intercept_despawn(true); }

void on_ai_think_entry() {
    Session *s = g_session;
    // WC.EXE clients let the AI run (the server's positions overwrite what
    // it does); in WC2 the wingman AI would pull the host's ship into
    // formation on our own every frame, so there it is off for that ship.
    if (!s || !(s->is_server() || is_wc2())) {
        return;
    }
    if (s->is_remote_player_slot(call_arg16(code::aiShipThink, 0))) {
        return_from_call(code::aiShipThink, 0);
    }
}

void on_ai_set_speed_entry() {
    Session *s = g_session;
    if (!s) {
        return;
    }
    int ship = call_arg16(code::aiSetSpeed, 0);
    if (s->is_client() || s->is_remote_player_slot(ship)) {
        // The AI must not change the set speed of a human-flown ship.
        if (code::aiSetSpeedReturn.known()) {
            reg_eip = code::aiSetSpeedReturn.off;  // WC.EXE: the function's early-return label (ovr143:0918)
        } else {
            return_from_call(code::aiSetSpeed, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Replay

static void enqueue_fire(const WeaponFire &fire) {
    if (!fire.has_shooter()) {
        return;
    }
    if (g_entityMap) {
        if (!g_entityMap->is_mapped(fire.shooter())) {
            wclog(2, "fire from unmapped net %d ignored", fire.shooter());
            return;
        }
        if ((int)fire.shooter() == g_entityMap->own_ship()) {
            // Echo of a shot we already fired locally.
            for (std::deque<PendingFire>::iterator it = g_pendingFires.begin(); it != g_pendingFires.end(); ++it) {
                if (fire.has_client_seq() && it->seq == fire.client_seq()) {
                    if (fire.has_ship_id() && it->spawnedSlot != -1) {
                        g_entityMap->record_spawn(fire.ship_id(), it->spawnedSlot);
                    }
                    g_pendingFires.erase(g_pendingFires.begin(), it + 1);
                    return;
                }
            }
            wclog(2, "echo of our fire seq %u not pending; ignoring", fire.client_seq());
            return;
        }
    }
    int local = NetworkShipId::from_net(fire.shooter()).to_local();
    FireJob::Mode mode = g_session->is_server() ? FireJob::BROADCAST : FireJob::REPLAY;
    g_trampoline.enqueue(new FireJob(mode, fire, local, fire.gun_id(), g_session->is_server()));
}

void enqueue_remote_event(const Event &ev) {
    if (ev.has_fire()) {
        enqueue_fire(ev.fire());
    }
    if (ev.has_damage()) {
        g_trampoline.enqueue(new DamageJob(DamageJob::REPLAY, ev.damage()));
    }
    if (ev.has_spawn()) {
        if (is_wc2() && g_session->is_client() && ev.spawn().has_situation_id() && ev.spawn().situation_id() != 0xffff) {
            g_trampoline.enqueue(new NavSetupJob((int)ev.spawn().situation_id()));  // nothing to do when it is current
        }
        g_trampoline.enqueue(new SpawnJob(SpawnJob::REPLAY, ev.spawn()));
    }
    if (ev.has_despawn() && ev.despawn().has_ship_id()) {
        const Despawn &d = ev.despawn();
        if (g_entityMap && !g_entityMap->is_mapped(d.ship_id())) {
            wclog(2, "despawn of unmapped net %d ignored", d.ship_id());
        } else {
            int local = NetworkShipId::from_net(d.ship_id()).to_local();
            int src = kInvalidSlot;
            if (d.has_shooter()) {
                src = NetworkShipId::from_net(d.shooter()).to_local();
            }
            g_trampoline.enqueue(new DespawnJob(DespawnJob::REPLAY, d, local, src));
        }
    }
    if (ev.has_autopiloting()) {
        g_trampoline.enqueue(new AutopilotJob(ev.autopiloting()));
    }
    if (ev.has_cloak() && ev.cloak().has_ship_id() && g_session->is_client()) {
        const Cloak &c = ev.cloak();
        if (g_entityMap && !g_entityMap->is_mapped(c.ship_id())) {
            wclog(2, "cloak of unmapped net %d ignored", c.ship_id());
        } else {
            // (A drone saw through cloaks once; with the leader's energy in
            // its hands that was too much, and it sees what everybody sees.)
            g_trampoline.enqueue(new CloakJob(NetworkShipId::from_net(c.ship_id()).to_local(), c.on()));
        }
    }
}

void enqueue_reported_damage(const Damage &reported) {
    // Only what a collision report needs: no source, no seed of the client's.
    Damage dam;
    dam.set_ship_id(reported.ship_id());
    dam.set_quantity(reported.quantity() & 0xffff);
    if (reported.has_pos()) {
        *dam.mutable_pos() = reported.pos();
    }
    g_trampoline.enqueue(new DamageJob(DamageJob::REPORTED, dam));
}

void enqueue_chat_display(int netShipId, const std::string &callsign, const std::string &text) {
    std::string formatted = text;
    if (callsign.length() > 1 && callsign != "BLUEHAIR") {
        formatted = callsign + ": " + text;
    }
    g_trampoline.enqueue(new ChatJob(netShipId, formatted));
}

void enqueue_wingman_lost(int slot, bool explode) {
    Despawn d;
    d.set_ship_id(NetworkShipId::from_local(slot).to_net());
    if (explode) {
        d.set_explode(1);
    }
    g_trampoline.enqueue(new DespawnJob(DespawnJob::WINGMAN_LOST, d, slot, kInvalidSlot));
}

// Test aid (the key script's "!kill=<slot>"): the server destroys a ship as
// if the player had shot it, through the same broadcast path as a real kill.
void enqueue_test_kill(int slot) {
    Despawn d;
    d.set_ship_id(NetworkShipId::from_local(slot).to_net());
    d.set_explode(1);
    d.set_shooter(NetworkShipId::from_local(kPlayerSlot).to_net());
    g_trampoline.enqueue(new DespawnJob(DespawnJob::BROADCAST, d, slot, kPlayerSlot));
}

// Test aid: damage to our own ship as the game would apply it (shield, armor,
// then the player's own damage model with its cockpit damage).
class OwnHitJob : public VmJob {
public:
    OwnHitJob(int quantity, bool fromBehind) : quantity_(quantity), fromBehind_(fromBehind) {}
    virtual bool start() {
        // The vector is the way the blow travels: against our nose for a
        // hit from ahead (front shield and armor), along it from behind.
        Vec3 v = read_vec(vec_slot(ds::gOrientationFrontVector, kPlayerSlot));
        if (!fromBehind_) {
            v.x = -v.x; v.y = -v.y; v.z = -v.z;
        }
        write_vec(kTmpVectorOff, v);
        call_of(code::do_damage)
            .arg((Bit16u)kInvalidSlot).arg((Bit16u)kPlayerSlot)
            .arg((Bit16u)quantity_).arg((Bit16u)kTmpVectorOff)
            .invoke();
        return true;
    }
    virtual const char *describe() const { return "own hit"; }

private:
    int quantity_;
    bool fromBehind_;
};

// Test aid (the key script's "!mem"): how much of its own heap the game has
// free, by the count its loader's error screen prints (WC2: "MAIN").  A
// flight that left less than the one before it lost something on the way.
class MemJob : public VmJob {
public:
    virtual bool start() {
        if (!code::freeMainMemory.known()) {
            return false;
        }
        called_ = true;
        call_of(code::freeMainMemory).invoke();
        return true;
    }
    virtual void finish() {
        if (called_) {
            wclog(1, "free main memory: %ld bytes", (long)(((Bit32u)(reg_edx & 0xffff) << 16) | (reg_eax & 0xffff)));
        }
    }
    virtual const char *describe() const { return "mem"; }

private:
    bool called_ = false;
};

void enqueue_test_mem() {
    g_trampoline.enqueue(new MemJob());
}

void enqueue_test_hit(int quantity, bool fromBehind) {
    g_trampoline.enqueue(new OwnHitJob(quantity, fromBehind));
}

// ---------------------------------------------------------------------------
// despawn_all_ships: C++ translation of ovr140:1C16 (despawn) for the
// permanent slots, skipping the capital-ship death animation, used when a
// mission ends to make sure every machine starts the next one empty.

static void set_word_array(Bit16u base, int slot, Bit16u v) {
    wr16((Bit16u)(base + 2 * slot), v);
}

static void despawn_slot_quietly(int slot) {
    if (slot == kInvalidSlot) {
        return;
    }
    set_word_array(ds::entityCullStatus, slot, 0x8001);
    set_word_array(0xb862, slot, 0);
    if (rd16(ds::unknown07A6) == 0) {
        wr16(ds::unknown07A6, 0xffff);
    }
    if (rd16(ds::playerTarget) == 0) {
        wr16(ds::playerTarget, 0xffff);
    }
    for (int i = 0; i < 0x14; i++) {
        if (rd8((Bit16u)(ds::entitiesToDespawn + i)) == slot) {
            wr8((Bit16u)(ds::entitiesToDespawn + i), 0xff);
        }
    }
    if (slot < kMinTempSlot) {
        set_word_array(ds::commAnimInfo, slot, 2);
        wr8((Bit16u)(0xd1a2 + slot), 0xff);
        set_word_array(ds::vduStatus10WhenPlayerHitsSmth, slot, 0xff);
        set_word_array(0xc69e, slot, 0xffff);
        set_word_array(0xc38c, slot, 0);
        wr8((Bit16u)(0xc854 + slot), 0xff);
        set_word_array(0xbe26, slot, 0);
        set_word_array(0xbfe0, slot, 0xffff);
    }
    set_word_array(ds::maybeEntityType, slot, 0);
    wr32((Bit16u)(0xc754 + 4 * slot), 0);
}

void despawn_all_ships() {
    // (WC.EXE's arrays; WC2's own mission setup clears its slots.)
    for (int i = 0; i < kMinTempSlot && is_wc1(); i++) {
        despawn_slot_quietly(i);
    }
}

}  // namespace wc
