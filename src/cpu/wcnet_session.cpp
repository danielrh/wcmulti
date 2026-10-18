#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "wc_net.h"
#include "wcnet_session.h"
#include "wcnet_perf.h"
#include "pic.h"
#include "wcnet_events.h"
#include "wcnet_vm.h"
#include "wcnet_memory.h"
#include "wcnet_code.h"
#include "wcnet_game.h"
#include "wcnet_transport.h"
#include "wcnet_lobby.h"
#include "wcnet_log.h"
#include "keyboard.h"
#include "cpu.h"
#include "regs.h"

#ifdef C_LOBBYLINK
// In a lobbylink room player 0 is the server; clients wait this long for the
// WebRTC connection to it.
enum { kHostPlayer = 0, kHostWaitMs = 20000 };
#endif

// Text shown by the GUI overlay while not in space (src/gui/sdlmain.cpp).
extern std::string incoming_text;

bool within_briefed_mission = false;
NetConfig net_config;

namespace wc {

Session *g_session = NULL;
int in_simulation = 0;
bool has_started_up = false;

// ---------------------------------------------------------------------------
// NetConfig

}  // namespace wc

// Pause while retrying a connection.  In the browser the emulator has to
// yield to the event loop (Asyncify) instead of blocking the page.
static void pause_ms(int ms) {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

const char *NetConfig::kDefaultLobbyUrl = "https://pqrstuvw.xyz/lobbylink";
const char *NetConfig::kDefaultPort = "13255";

NetConfig::NetConfig() {
    reset_from_env();
}

void NetConfig::reset_from_env() {
    reset(getenv("WCHOST"), getenv("WCPORT"));
    room = getenv("WCROOM");
    const char *url = getenv("WCLOBBY");
    lobby_url = (url && url[0]) ? url : kDefaultLobbyUrl;
    lobby_origin = getenv("WCLOBBY_ORIGIN");
    const char *relay = getenv("WCLOBBY_RELAY");
    lobby_relay = relay && relay[0] && relay[0] != '0';
    const char *players = getenv("WCPLAYERS");
    lobby_players = players ? atoi(players) : 0;
    if (lobby_players < 2) {
        lobby_players = 2;  // a leader and a wingman; WCPLAYERS=3 adds a seat
    }
}

void NetConfig::reset(const char *h, const char *p) {
    host = h;
    portstr = p ? p : kDefaultPort;
    port = (uint16_t)atoi(portstr);
    if (port < 1024) {
        fprintf(stderr, "You must set the WCPORT and (optionally) WCHOST env variables! Not %s\n", portstr);
        abort();
    }
}

namespace wc {

// ---------------------------------------------------------------------------
// Pilot identity and small game helpers

std::string get_last_name() {
    if (!ds::known(ds::pilotLastName)) {
        const char *env = getenv("WCLASTNAME");
        return env ? env : "";
    }
    std::string saved = read_cstring(ds::pilotLastName, 14);
    const char *env = getenv("WCLASTNAME");
    if (env && env[0] && saved != env) {
        saved = env;
        write_cstring(ds::pilotLastName, 14, saved);
    }
    return saved;
}

std::string get_callsign() {
    if (!ds::known(ds::pilotCallsign)) {
        const char *env = getenv("WCCALLSIGN");
        return env ? env : "";
    }
    std::string saved = read_cstring(ds::pilotCallsign, 14);
    const char *env = getenv("WCCALLSIGN");
    if (env && env[0] && saved != env) {
        saved = env;
        write_cstring(ds::pilotCallsign, 14, saved);
    }
    return saved;
}

// WC2 asks a new pilot for a first name as well, and its conversations print
// all three from dseg:9640.  A mission chosen on the command line never
// asks, and the placeholders "CALLSIGN", "PCNAME" and "FIRSTNAME" were
// what people said.  WCCALLSIGN / WCLASTNAME / WCFIRSTNAME go there and into
// the saved record's copy (a loaded game brings its own names back, hence
// the periodic call).
void apply_pilot_names() {
    if (!ds::known(ds::shownNames) || !ds::known(ds::savedNames)) {
        return;
    }
    enum { kMost = 12 };  // what the game's own entry screen takes
    static const struct {
        const char *env;
        int shown, saved;
    } kNames[] = { { "WCCALLSIGN", 0x00, 0x32 }, { "WCLASTNAME", 0x18, 0x19 }, { "WCFIRSTNAME", 0x30, 0x00 } };
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        const char *env = getenv(kNames[i].env);
        if (!env || !env[0]) {
            continue;
        }
        std::string name = std::string(env).substr(0, kMost);
        Bit16u places[2] = { (Bit16u)(ds::shownNames + kNames[i].shown), (Bit16u)(ds::savedNames + kNames[i].saved) };
        for (int p = 0; p < 2; p++) {
            std::string had = read_cstring(places[p], kMost + 1);
            if (had != name) {
                if (p == 0) {
                    wclog(2, "%s: the game's \"%s\" is now \"%s\"", kNames[i].env, had.c_str(), name.c_str());
                }
                write_cstring(places[p], kMost + 1, name);
            }
        }
    }
}

// Clear the KIA flags so the debriefing never tells a sob story about a
// wingman who is in fact alive on another machine.  One word per named pilot
// (the mission he died in, series * 4 + mission; 0 = alive): the scripts hold
// a funeral for every pilot whose word names the mission just flown.
static void liven_everyone() {
    for (int i = 0; i < 8 && ds::known(ds::statusPilots); i++) {
        wr16((Bit16u)(ds::statusPilots + 2 * i), 0);
    }
}

enum { kMissionTreeSize = 8 * 19 };

// (The nav-point progress table is WC.EXE's; WC2's is not mapped yet.)
static void load_mission_tree_progress(const std::string &data) {
    if (!is_wc1()) {
        return;
    }
    if (data.length() != kMissionTreeSize) {
        wclog(0, "unexpected mission tree length %d", (int)data.length());
        return;
    }
    for (int i = 0; i < kMissionTreeSize; i++) {
        wr8((Bit16u)(ds::navPointState + i), (Bit8u)data[i]);
    }
}

// WC2's campaign record: a far-heap block, its length in words first.  The
// barracks holds it between flights (it is saved and freed before one), so
// it is there when "Fly mission" is chosen and absent in the mission test.
static PhysPt campaign_record(int *bytes) {
    *bytes = 0;
    if (!ds::known(ds::campaignState)) {
        return 0;
    }
    PhysPt rec = PhysMake(rd16((Bit16u)(ds::campaignState + 2)), rd16(ds::campaignState));
    if (!rec) {
        return 0;
    }
    int n = 2 * (int)mem_readw(rec);
    if (n < 0x20 || n > 0x1000) {
        wclog(0, "campaign record of %d bytes: not what was expected", n);
        return 0;
    }
    *bytes = n;
    return rec;
}

static std::string read_campaign_state() {
    int n;
    PhysPt rec = campaign_record(&n);
    std::string out;
    for (int i = 0; i < n; i++) {
        out.push_back((char)mem_readb(rec + i));
    }
    return out;
}

static void load_campaign_state(const std::string &data) {
    int n;
    PhysPt rec = campaign_record(&n);
    if (data.empty() || !rec) {
        return;
    }
    if ((int)data.length() != n) {
        wclog(0, "the server's campaign record is %d bytes, ours %d: keeping ours", (int)data.length(), n);
        return;
    }
    for (int i = 0; i < n; i++) {
        mem_writeb(rec + i, (Bit8u)data[i]);
    }
    wclog(1, "took over the server's campaign record: series %d mission %d", mem_readw(rec + 4), mem_readw(rec + 6));
}

static void populate_mission_end(MissionEnd *end) {
    end->set_game_update((GameState)rd16(ds::missionStatus));
    if (rd16(ds::missionStatus) == EndLand && ds::known(ds::landedOn)) {
        // WC.EXE: the ship we landed on (ovr141:1841 writes it), for the
        // wingmen's landing sequences, which draw the carrier from it.
        int slot = (Bit16s)rd16(ds::landedOn);
        if (is_ship_slot(slot) && slot_in_use(slot)) {
            end->set_landed_on_plus_one(1 + (Bit32u)NetworkShipId::from_local(slot).to_net());
        }
    }
    std::string *tree = end->mutable_mission_tree_progress();
    tree->clear();
    for (int i = 0; i < kMissionTreeSize && is_wc1(); i++) {
        tree->push_back((char)rd8((Bit16u)(ds::navPointState + i)));
    }
    if (is_wc2()) {
        // (The record as the flight loop returned: what the campaign loop
        // branches on.  A wingman's game takes it at its own return.)
        end->set_campaign_state(read_campaign_state());
    }
}

static void populate_mission_addendum(MissionEnd *end) {
    if (!ds::known(ds::victoryPoints)) {
        return;
    }
    end->set_victory_points_plus_one(1 + (Bit32u)rd16(ds::victoryPoints));
}

void run_campaign(int missionId, int seriesId) {
    wr8(ds::currentMission, (Bit8u)missionId);
    wr8(ds::currentSeries, (Bit8u)seriesId);
    wr8(ds::savedGameLoaded, 1);
    // Called at the title call of the game's main loop (afterStartup): run
    // the hangar mission as that loop's own call would, so its result is
    // tested the same way every time -- nonzero goes on to the next mission
    // through the barracks, zero (death, carrier lost, quit) comes back to
    // afterStartup, which flies the mission again.
    CPU_Push16((Bit16u)SegValue(cs));
    CPU_Push16(code::afterHangarMission.off);
    SegSet16(cs, code::runHangarMission.seg);
    reg_eip = code::runHangarMission.stubOff;
}

template <class V>
static void fill_vector(V *out, Bit16u off) {
    Vec3 v = read_vec(off);
    out->set_x(v.x);
    out->set_y(v.y);
    out->set_z(v.z);
}

template <class V>
static Vec3 to_vec(const V &in) {
    Vec3 v = { in.x(), in.y(), in.z() };
    return v;
}

static void populate_location(Location *loc, int slot) {
    fill_vector(loc->mutable_pos(), vec_slot(ds::gPositionVector, slot));
    fill_vector(loc->mutable_vel(), vec_slot(ds::gVelocityVector, slot));
    fill_vector(loc->mutable_right(), vec_slot(ds::gOrientationRightVector, slot));
    fill_vector(loc->mutable_up(), vec_slot(ds::gOrientationUpVector, slot));
    fill_vector(loc->mutable_fore(), vec_slot(ds::gOrientationFrontVector, slot));
    loc->set_set_speed(rd32((Bit16u)(ds::setSpeed + 4 * slot)));
}

static void apply_location(const Location &loc, int slot) {
    if (loc.has_pos()) {
        write_vec(vec_slot(ds::gPositionVector, slot), to_vec(loc.pos()));
    }
    if (loc.has_vel()) {
        write_vec(vec_slot(ds::gVelocityVector, slot), to_vec(loc.vel()));
    }
    if (loc.has_right() && loc.has_up() && loc.has_fore()) {
        write_vec(vec_slot(ds::gOrientationRightVector, slot), to_vec(loc.right()));
        write_vec(vec_slot(ds::gOrientationUpVector, slot), to_vec(loc.up()));
        write_vec(vec_slot(ds::gOrientationFrontVector, slot), to_vec(loc.fore()));
    }
    if (loc.has_set_speed()) {
        wr32((Bit16u)(ds::setSpeed + 4 * slot), loc.set_speed());
    }
}

static void fill_health(ShipHealth *out, const ShipHealthState &h) {
    out->Clear();
    for (int i = 0; i < 2; i++) {
        out->add_shield(h.shield[i]);
        out->add_shield_max(h.shieldMax[i]);
    }
    for (int i = 0; i < 4; i++) {
        out->add_armor(h.armor[i]);
    }
    out->set_damage_points(h.damagePoints);
    out->set_core_hp(h.coreHp);
    out->set_hull_counter(h.hullCounter);
    out->set_state_byte(h.stateByte);
    out->set_gun_damage(h.gunDamage);
    out->set_engine_flag(h.engineFlag);
    out->set_gun_energy(h.gunEnergy);
}

static bool parse_health(const ShipHealth &in, ShipHealthState *h) {
    if (in.shield_size() != 2 || in.shield_max_size() != 2 || in.armor_size() != 4) {
        return false;
    }
    memset(h, 0, sizeof(*h));
    for (int i = 0; i < 2; i++) {
        h->shield[i] = (Bit16s)in.shield(i);
        h->shieldMax[i] = (Bit16s)in.shield_max(i);
    }
    for (int i = 0; i < 4; i++) {
        h->armor[i] = (Bit16s)in.armor(i);
    }
    h->damagePoints = (Bit16s)in.damage_points();
    h->coreHp = (Bit8u)in.core_hp();
    h->hullCounter = (Bit8u)in.hull_counter();
    h->stateByte = (Bit8u)in.state_byte();
    h->gunDamage = (Bit8u)in.gun_damage();
    h->engineFlag = (Bit8u)in.engine_flag();
    h->gunEnergy = (Bit16s)in.gun_energy();
    return true;
}

// Sends a ship's health when it changed, and periodically anyway so a lost
// update cannot leave a peer stale forever.
class HealthPublisher {
public:
    HealthPublisher() : framesSince_(0) {}
    void reset() { last_.clear(); framesSince_ = 0; }
    void next_frame() { framesSince_++; }
    bool should_send(int slot, ShipHealthState *out) {
        *out = read_health(slot);
        std::map<int, ShipHealthState>::iterator it = last_.find(slot);
        bool changed = it == last_.end() || it->second != *out;
        if (!changed && framesSince_ < kPeriod) {
            return false;
        }
        last_[slot] = *out;
        return true;
    }
    void end_frame() {
        if (framesSince_ >= kPeriod) {
            framesSince_ = 0;
        }
    }

private:
    enum { kPeriod = 30 };
    std::map<int, ShipHealthState> last_;
    int framesSince_;
};

static std::string describe_health(int slot) {
    ShipHealthState h = read_health(slot);
    char buf[128];
    snprintf(buf, sizeof(buf), "shield %d/%d armor %d/%d/%d/%d core %d dmg %d",
             h.shield[0], h.shield[1], h.armor[0], h.armor[1], h.armor[2], h.armor[3], h.coreHp, h.damagePoints);
    return buf;
}

static ShipUpdate *update_for(Frame *frame, int net) {
    for (int i = 0; i < frame->update_size(); i++) {
        if (frame->update(i).has_ship_id() && (int)frame->update(i).ship_id() == net) {
            return frame->mutable_update(i);
        }
    }
    ShipUpdate *su = frame->add_update();
    su->set_ship_id(net);
    return su;
}

static const char *status_name(int status) {
    switch (status) {
    case Proceed: return "proceed";
    case EndLand: return "landed";
    case EndEject: return "ejected";
    case EndCarrier: return "carrier";
    case EndDeath: return "died";
    case EndExit: return "exited";
    }
    return "?";
}

static void handle_incoming_chat(const Chat &chat) {
    std::string formatted = chat.message();
    if (chat.callsign().length() > 1 && chat.callsign() != "BLUEHAIR") {
        formatted = chat.callsign() + ": " + chat.message();
    }
    incoming_text = formatted;
    if (chat.ship_id() == 0 && g_session && g_session->is_client()) {
        note_leader_callsign(chat.callsign());
    }
    enqueue_chat_display(chat.ship_id(), chat.callsign(), chat.message());
}

// A line of our own: on the comms display in flight (it runs with the next
// frame's jobs), on the overlay otherwise.
static void show_notice(const std::string &text) {
    if (!within_briefed_mission) {
        incoming_text = text;
        return;
    }
    Chat chat;
    chat.set_ship_id(0);
    chat.set_message(text);
    handle_incoming_chat(chat);
}

static const char *wing_bumps_notice(int mode) {
    return mode == WING_BUMPS_GAME ? "Bumps between wingmen do damage, as the game has it" : "Bumps between wingmen are harmless";
}

static const char *rocks_notice(int mode) {
    return mode == ROCKS_OFF ? "Asteroids and mines are off"
         : mode == ROCKS_SOFT ? "Asteroids are soft: one will not kill you" : "Asteroids and mines are on";
}

// ---------------------------------------------------------------------------
// The link to the other machine, measured (docs section 3b): the round trip
// (a frame goes out, the other side's ack of it comes back, less the time
// the other side held it, Frame.ack_delay), its spikes (a round trip well
// above the best of the last minute: a lost message sent again, a queue
// somewhere), and the frame rates of both games, a sample a second.  All
// in real time (perf_now_ms); the server keeps one per client.

static double frame_ms() {
    int fps = pace_fps();
    return 1000.0 / (fps > 0 ? fps : 15);  // (WC2 paces itself at 15)
}

class LinkMonitor {
public:
    enum { kRing = 512, kSpikeMs = 150, kMinuteMs = 60000, kGraceMs = 5000 };
    LinkMonitor() { reset(); }
    void reset() {
        for (int i = 0; i < kRing; i++) {
            sentNo_[i] = ~(Bit64u)0;
            sentAt_[i] = 0;
        }
        samples_.clear();
        spikes_.clear();
        seconds_.clear();
        rtt_ = -1;
        spikeOpen_ = false;
        quietUntil_ = -1;
        aliveAt_ = -1;
    }
    // No frame rate samples for a while: the start of a flight and the
    // frames after a cinematic are slow by nature.
    void quiet(double now, double forMs) {
        if (now + forMs > quietUntil_) {
            quietUntil_ = now + forMs;
        }
    }
    void sent(Bit64u frame, double now) {
        sentNo_[frame % kRing] = frame;
        sentAt_[frame % kRing] = now;
    }
    // The other side acks `frame`, which it had held for `heldMs` by then.
    // Frames that went out before the other side's first word are no
    // measure of the link: a wingman's first ack comes when its game has
    // launched, seconds after the start state (the server runs ahead and
    // waits), and would read as a round trip of seconds.
    void acked(Bit64u frame, double heldMs, double now) {
        if (aliveAt_ < 0) {
            aliveAt_ = now;
        }
        if (sentNo_[frame % kRing] != frame || sentAt_[frame % kRing] < aliveAt_ || now < quietUntil_) {
            return;  // (quiet: the acks that come after a cinematic are of frames sent before it)
        }
        double rtt = now - sentAt_[frame % kRing] - heldMs;
        if (rtt < 0) {
            rtt = 0;
        }
        samples_.push_back(std::make_pair(now, rtt));
        prune(now);
        rtt_ = rtt_ < 0 ? rtt : rtt_ + (rtt - rtt_) * 0.1;
        bool high = rtt > floor() + kSpikeMs;
        if (high && !spikeOpen_) {
            spikes_.push_back(now);
        }
        spikeOpen_ = high;
    }
    // Once a second: the other side's frames in its last second (-1: not
    // said) and ours.
    void second(double now, int theirFps, int ourFps) {
        if (quietUntil_ < 0) {
            quiet(now, kGraceMs);  // the first second seen: the flight has just begun
        }
        Second sec = { now, now < quietUntil_ ? -1 : theirFps, now < quietUntil_ ? -1 : ourFps };
        seconds_.push_back(sec);
        prune(now);
    }
    bool known() const { return rtt_ >= 0; }
    double rtt() const { return rtt_; }  // smoothed, ms
    // The best round trip of the last minute.
    double floor() const {
        double best = -1;
        for (size_t i = 0; i < samples_.size(); i++) {
            if (best < 0 || samples_[i].second < best) {
                best = samples_[i].second;
            }
        }
        return best < 0 ? 0 : best;
    }
    // The worst round trip of the last `sinceMs`.
    double ceiling(double now, double sinceMs) const {
        double worst = 0;
        for (size_t i = samples_.size(); i-- > 0 && samples_[i].first >= now - sinceMs;) {
            if (samples_[i].second > worst) {
                worst = samples_[i].second;
            }
        }
        return worst;
    }
    int spikes_per_min() const { return (int)spikes_.size(); }
    // The share of the round trips of the last `sinceMs` that were spikes:
    // about the share of its time the other side spends waiting beyond the
    // link's best, whatever the spikes' cause.
    double spike_share(double now, double sinceMs) const {
        int n = 0, high = 0;
        double best = floor();
        for (size_t i = samples_.size(); i-- > 0 && samples_[i].first >= now - sinceMs;) {
            n++;
            if (samples_[i].second > best + kSpikeMs) {
                high++;
            }
        }
        return n ? (double)high / n : 0;
    }
    // The average of the frame rate samples of the last `sinceMs` (theirs or
    // ours), or -1 with none.
    double fps_avg(double now, bool theirs, double sinceMs) const {
        double sum = 0;
        int n = 0;
        for (size_t i = seconds_.size(); i-- > 0 && seconds_[i].at >= now - sinceMs;) {
            int fps = theirs ? seconds_[i].theirs : seconds_[i].ours;
            if (fps >= 0) {
                sum += fps;
                n++;
            }
        }
        return n ? sum / n : -1;
    }
    // Seconds of the last `sinceMs` in which a frame rate was under `fps`.
    int seconds_under(double now, bool theirs, int fps, double sinceMs) const {
        int n = 0;
        for (size_t i = seconds_.size(); i-- > 0 && seconds_[i].at >= now - sinceMs;) {
            int got = theirs ? seconds_[i].theirs : seconds_[i].ours;
            if (got >= 0 && got < fps) {
                n++;
            }
        }
        return n;
    }
    int seconds_known() const { return (int)seconds_.size(); }

private:
    void prune(double now) {
        while (!samples_.empty() && samples_.front().first < now - kMinuteMs) {
            samples_.pop_front();
        }
        while (!spikes_.empty() && spikes_.front() < now - kMinuteMs) {
            spikes_.pop_front();
        }
        while (!seconds_.empty() && seconds_.front().at < now - kMinuteMs) {
            seconds_.pop_front();
        }
    }
    struct Second {
        double at;
        int theirs, ours;
    };
    Bit64u sentNo_[kRing];
    double sentAt_[kRing];
    std::deque<std::pair<double, double> > samples_;  // (when, round trip ms), the last minute
    std::deque<double> spikes_;                        // when each spike began, the last minute
    std::deque<Second> seconds_;
    double rtt_;
    bool spikeOpen_;
    double quietUntil_;
    double aliveAt_;  // when the other side first acked anything (-1: not yet)
};

static const char *mode_name(int mode) {
    return mode == MODE_HIGH ? "high latency" : mode == MODE_LOW ? "low latency" : "auto";
}

// The setting by name: low latency unless told otherwise.  The automatic
// mode, which goes by the link the server measures, was the default until
// 2026-10-15; its flips to the high-latency mode in the middle of a fight
// were a disturbance of their own in play, so it is only had by asking.
static int mode_from_name(const char *name) {
    if (!name || !name[0]) return MODE_LOW;
    if (!strcasecmp(name, "auto") || name[0] == '0') return MODE_AUTO;
    if (!strcasecmp(name, "low") || name[0] == '1') return MODE_LOW;
    if (!strcasecmp(name, "high") || name[0] == '2') return MODE_HIGH;
    return MODE_LOW;
}

// ---------------------------------------------------------------------------
// Server

struct RemoteClient {
    int net;                     // network id == server slot this client flies (as a wingman)
    Seat seat;                   // decided with the mission start state
    int mannedTurret;            // a gunner: the turret it sits in, or -1
    Bit64u acked;                // the last frame of ours it had applied, by its own word
    Bit64u lastFrame;            // the last frame of its we took in, and when (real ms): our ack
    double lastFrameAt;
    int fps;                     // its frames in its last second, by its own word (-1: not said)
    LinkMonitor link;
    Connection conn;
    std::string callsign;
    std::string missionTreeProgress;  // reported with a shared mission end
    bool requestedBriefingStart;
    bool needsMissionStartState;
    bool inMission;              // takes part in the frame exchange
    bool leftThisMission;        // died/ejected/exited this mission
    int skipPendingEvents;       // events already covered by the start state
    // The last frame of its whose position we took (by either channel), and when (emulated ms).
    Bit64u lastPosition = 0;
    double lastPositionEmu = 0;
    // Debugging: how far our copy of its ship turns away from the heading it
    // last reported, between its updates (the AI must not fly a human's ship).
    bool hasLastLoc = false;
    Location lastLoc;
    double driftWorst = 0, driftWindowAt = 0;
    int driftUpdates = 0;

    explicit RemoteClient(int n)
        : net(n), seat(SEAT_WINGMAN), mannedTurret(-1), acked(0), lastFrame(0), lastFrameAt(0), fps(-1),
          requestedBriefingStart(false), needsMissionStartState(false), inMission(false), leftThisMission(false),
          skipPendingEvents(0) {}
    bool connected() const { return conn.is_open(); }
    void disconnect() {
        conn.close();
        inMission = false;
        needsMissionStartState = false;
        requestedBriefingStart = false;
    }
};

class ServerSession;

// WCNET_BESTEFFORT=0: no positions by the best-effort channel in the
// high-latency mode (to measure what it is worth).
static bool best_effort_wanted() {
    static int wanted = -1;
    if (wanted < 0) {
        const char *env = getenv("WCNET_BESTEFFORT");
        wanted = env && env[0] == '0' ? 0 : 1;
    }
    return wanted == 1;
}

// The server's wait for a client that has fallen too far behind, in emulated
// time (as ServerFrameJob is for a client).
class ClientLagJob : public VmJob {
public:
    explicit ClientLagJob(ServerSession *server, double began = -1)
        : server_(server), began_(began < 0 ? PIC_FullIndex() : began), done_(false) {}
    virtual bool start();
    virtual void finish() {
        if (!done_) {
            g_trampoline.enqueue_front(new ClientLagJob(server_, began_));
        }
    }
    virtual const char *describe() const { return "client lag"; }

private:
    ServerSession *server_;
    double began_;
    bool done_;
};

class ServerSession : public Session {
public:
    // Takes ownership of the listener.
    explicit ServerSession(Listener *listener)
        : listener_(listener), epoch_(1), frameNumber_(0), sendFrameAtIdle_(false), ignoreNextFrameTop_(false),
          hadClientInMission_(false),
          wantRocks_(rocks_mode()), wantWingBumps_(wing_bumps_mode()), modeSetting_(mode_from_name(getenv("WCNET_MODE"))), autoMode_(MODE_LOW),
          highWindow_(kHighWindowMin), modeSince_(0), modeTickAt_(0), ownFrames_(0), rtt_(-1) {
        allowedIds_.push_back(1);
        allowedIds_.push_back(3);
        reset_pending_frame();
        const char *env = getenv("WCNET_MODE");
        wclog(1, "exchange mode setting: %s (%s)", mode_name(modeSetting_), env && env[0] ? "WCNET_MODE" : "the default");
    }
    ~ServerSession() {
        for (size_t i = 0; i < clients_.size(); i++) {
            delete clients_[i];
        }
        delete listener_;
    }

    virtual Role role() const { return ROLE_SERVER; }

    virtual bool is_remote_player_slot(int slot) const {
        RemoteClient *c = client_for_slot(slot);
        return c != NULL && c->inMission;
    }

    virtual void queue_outgoing_event(const Event &ev) {
        *pendingFrame_.mutable_frame()->add_event() = ev;
    }
    virtual int gunner_turret() const {
        for (size_t i = 0; i < clients_.size(); i++) {
            const RemoteClient *c = clients_[i];
            if (c->connected() && c->inMission && c->seat == SEAT_GUNNER && c->mannedTurret >= 0) {
                return c->mannedTurret;
            }
        }
        return -1;
    }
    virtual void on_spawned(const Spawn &spawn) { spawns_.add(spawn); }
    virtual void on_despawned(int net) { spawns_.remove(net); }

    virtual int mode_setting() const { return modeSetting_; }
    virtual int mode() const { return modeSetting_ != MODE_AUTO ? modeSetting_ : autoMode_; }
    virtual double rtt_ms() const { return rtt_; }

    virtual void request_mode(int setting) {
        setting = setting == MODE_LOW ? MODE_LOW : setting == MODE_HIGH ? MODE_HIGH : MODE_AUTO;
        if (setting == modeSetting_) {
            return;
        }
        int before = mode();
        modeSetting_ = setting;
        wclog(1, "exchange mode setting: %s (in force: %s)", mode_name(setting), mode_name(mode()));
        if (mode() != before) {
            announce_mode();
        } else {
            show_notice(mode_notice());
        }
    }

    virtual void request_wing_bumps(int mode) {
        mode = mode == WING_BUMPS_GAME ? WING_BUMPS_GAME : WING_BUMPS_HARMLESS;
        if (mode == wantWingBumps_) {
            return;
        }
        wantWingBumps_ = mode;
        if (!within_briefed_mission) {
            set_wing_bumps_mode(mode);
            show_notice(wing_bumps_notice(mode));
        }
        // In a mission: exchange() applies it at the top of the next frame.
    }

    virtual void request_rocks(int mode) {
        mode = mode == ROCKS_OFF ? ROCKS_OFF : mode == ROCKS_SOFT ? ROCKS_SOFT : ROCKS_ON;
        if (mode == wantRocks_) {
            return;
        }
        wantRocks_ = mode;
        if (!within_briefed_mission) {
            set_rocks_mode(mode);  // the next mission starts that way
            show_notice(rocks_notice(mode));
        }
        // In a mission: exchange() applies it at the top of the next frame.
    }

    virtual void on_mission_starting(int mission, int series) {
        liven_everyone();
        set_rocks_mode(wantRocks_);
        set_wing_bumps_mode(wantWingBumps_);
        within_briefed_mission = true;
        MissionEnd end;
        populate_mission_end(&end);
        lastBriefing_.set_mission_tree_progress(end.mission_tree_progress());
        lastBriefing_.set_campaign_state(read_campaign_state());
        lastBriefing_.set_mission_id(mission);
        lastBriefing_.set_series_id(series);
        accept_clients(true);
        wclog(1, "mission %d/%d starting with %d client slot(s)", mission, series, (int)clients_.size());
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            // A new mission: nobody has left it yet.  The flag was cleared
            // only with the score after a landing or a death (WC.EXE's
            // compute_victory), so a wingman that had ejected or quit the
            // mission before stayed "gone" through the next one: its seat
            // not counted (client_for_slot), the AI flying the host's copy of
            // its ship and its shots leaving at the AI's heading.
            c->leftThisMission = false;
            if (c->connected() && c->requestedBriefingStart) {
                send_briefing_state(c);
            }
        }
    }

    virtual void on_mission_reset() {
        despawn_all_ships();
        spawns_.clear();
        health_.reset();
        epoch_ += 1;
        frameNumber_ = 0;
        hadClientInMission_ = false;
        extraWingTried_ = false;
        for (size_t i = 0; i < clients_.size(); i++) {
            clients_[i]->leftThisMission = false;
            clients_[i]->missionTreeProgress.clear();
        }
        // inMission is cleared when the mission-end frame goes out (flush).
    }

    virtual void on_mission_victory_calc() {
        // The score, computed after the landing animation.  The mission-end
        // frame went out when the flight loop returned and took the wingmen
        // out of the mission, so this goes to every connected wingman on
        // its own; a wingman reads it at its own victory computation
        // (ClientSession::on_mission_victory_calc).
        MissionEnd end;
        populate_mission_addendum(&end);
        if (!end.has_victory_points_plus_one()) {
            return;
        }
        wclog(1, "victory points %d", (int)end.victory_points_plus_one() - 1);
        NetworkMessage msg;
        msg.set_epoch(epoch_);
        msg.set_frame_number(0);
        *msg.mutable_frame()->mutable_mission_end() = end;
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (c->connected() && !c->conn.send(msg)) {
                wclog(0, "failed to send the score to player %d", c->net);
            }
        }
    }

    virtual void on_mission_ended() {
        liven_everyone();
        within_briefed_mission = false;
        int status = rd16(ds::missionStatus);
        wclog(1, "mission ended: %s", status_name(status));
        if (status > EndExit) {
            return;
        }
        for (size_t i = 0; i < clients_.size(); i++) {
            if (!clients_[i]->missionTreeProgress.empty()) {
                load_mission_tree_progress(clients_[i]->missionTreeProgress);
            }
        }
        MissionEnd *end = pendingFrame_.mutable_frame()->mutable_mission_end();
        populate_mission_end(end);
        // Out at once, for every ending.  A landing's used to wait for the
        // victory computation, to carry the score in the same message; the
        // computation runs after the landing animation and the debrief, so
        // the wingmen sat in their cockpits for those twelve seconds and
        // landed while the host was already being debriefed (2026-10-15).
        // The score follows on its own (on_mission_victory_calc).
        sendFrameAtIdle_ = true;
        exchange(true);
        if (status == EndDeath || status == EndCarrier) {
            // These endings skip the victory computation, so do the reset
            // that normally runs there: otherwise the epoch, the spawn
            // registry and the clients' left-this-mission flags survive
            // into the next mission, the next briefing request is answered
            // with the old briefing and start state, and the client waits
            // forever for frames it is no longer part of.
            on_mission_reset();
        }
    }

    virtual void on_frame_top() {
        if (ignoreNextFrameTop_) {
            ignoreNextFrameTop_ = false;
            return;
        }
        if (spawn_extra_wing()) {
            return;  // its spawn runs before this frame; the exchange (and the start states) wait a frame for it
        }
        exchange(false);
    }

    // Wing Commander gives a mission one wingman.  In a room of three (or
    // with WCNET_EXTRAWING=1) the mission's wingman record is spawned a
    // second time at the first frame of every mission: a second ship of the
    // same type and pilot, the game's own AI flying it until a human takes
    // it (reseat).  Returns true the once it enqueues the spawn.
    bool want_extra_wing() const {
        const char *env = getenv("WCNET_EXTRAWING");
        return env && env[0] ? atoi(env) > 0 : net_config.lobby_players >= 3;
    }
    bool spawn_extra_wing() {
        if (extraWingTried_ || !is_wc1() || !within_briefed_mission || !want_extra_wing()) {
            return false;
        }
        extraWingTried_ = true;
        const std::map<int, Spawn> &all = spawns_.all();
        std::map<int, Spawn>::const_iterator wing = all.find(kPlayerSlot + 1);
        if (wing == all.end() || !slot_in_use(kPlayerSlot + 1) || entity_type(kPlayerSlot + 1) != ET_SHIP) {
            wclog(1, "a third seat was wanted, but slot 1 holds no wingman to copy");
            return false;
        }
        // A mission with more than the one wingman in its records (the
        // Ralari escort), at launch or joining later, is left as it is: the
        // third seat is one of them.
        int wingmen = wing_records_wc1();
        if (wingmen >= 2) {
            wclog(1, "a third seat: the mission has %d wingmen of its own, one of them is it", wingmen);
            return false;
        }
        wclog(1, "a third seat: the wingman's mission ship %d is spawned again", wing->second.mission_ship_id());
        enqueue_server_spawn((int)wing->second.mission_ship_id(), (int)wing->second.situation_id(), kPlayerSlot + 1);
        return true;
    }
    // The wingmen in flight, by slot, the mission's own first: the seats a
    // human can take in Wing Commander.  A wingman is a fighter of our side
    // flying formation on the player (or on another wingman).  The mission
    // ship record (0x2a bytes), as read from Enyo 1's at launch: word 0 the
    // ship type, word 1 the side (0 ours, 1 Kilrathi), word 2 the mission
    // ship it flies formation on (Spirit's says the player's; the carrier's
    // patrol pair, 5,000 km out, says the carrier and each other), word 3
    // the behaviour (4 in formation).  The server's spawn registry says which
    // mission ship each slot flies.
    enum { kMsSide = 2, kMsLeader = 4, kMsBehaviour = 6, kMsInFormation = 4, kMsRecords = 16 };
    // Is mission ship `ms` a wingman's record: our side, in formation on one of `leaders`?
    bool wing_record_wc1(int ms, const std::set<int> &leaders) const {
        Bit16u rec = (Bit16u)(ds::missionShipTable + g_params.missionShipSize * ms);
        return rd16((Bit16u)(rec + kMsSide)) == 0 && rd16((Bit16u)(rec + kMsBehaviour)) == kMsInFormation &&
               leaders.count((Bit16s)rd16((Bit16u)(rec + kMsLeader))) > 0;
    }
    // How many wingmen the mission's records hold, spawned or not (they may
    // join at a later nav point).
    int wing_records_wc1() const {
        if (!ds::known(ds::missionShipTable) || !ds::known(ds::playerMissionShip)) {
            return 0;
        }
        int player = (Bit16s)rd16(ds::playerMissionShip);
        std::set<int> leaders;
        leaders.insert(player);
        int count = 0;
        for (bool grew = true; grew;) {
            grew = false;
            for (int ms = 0; ms < kMsRecords; ms++) {
                if (ms != player && !leaders.count(ms) && wing_record_wc1(ms, leaders)) {
                    leaders.insert(ms);
                    count++;
                    grew = true;
                }
            }
        }
        return count;
    }
    std::vector<int> wing_slots_wc1() const {
        std::vector<int> out;
        if (!ds::known(ds::missionShipTable) || !ds::known(ds::playerMissionShip)) {
            return out;
        }
        std::set<int> leaders;
        leaders.insert((Bit16s)rd16(ds::playerMissionShip));
        const std::map<int, Spawn> &all = spawns_.all();
        for (bool grew = true; grew;) {
            grew = false;
            for (std::map<int, Spawn>::const_iterator it = all.begin(); it != all.end(); ++it) {
                int slot = it->first, ms = (int)it->second.mission_ship_id();
                if (slot == kPlayerSlot || !is_ship_slot(slot) || !slot_in_use(slot) || entity_type(slot) != ET_SHIP ||
                    std::find(out.begin(), out.end(), slot) != out.end()) {
                    continue;
                }
                if (!wing_record_wc1(ms, leaders)) {
                    continue;
                }
                out.push_back(slot);
                leaders.insert(ms);
                grew = true;
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    // The slot of that second ship, or -1: the registry's other spawn of the
    // wingman's mission ship.
    int extra_wing_slot() const {
        const std::map<int, Spawn> &all = spawns_.all();
        std::map<int, Spawn>::const_iterator wing = all.find(kPlayerSlot + 1);
        if (wing == all.end()) {
            return -1;
        }
        for (std::map<int, Spawn>::const_iterator it = all.begin(); it != all.end(); ++it) {
            if (it->first != kPlayerSlot + 1 && it->second.mission_ship_id() == wing->second.mission_ship_id() && slot_in_use(it->first)) {
                return it->first;
            }
        }
        return -1;
    }

    virtual void on_async_tick() {
        accept_clients(false);
        NetworkMessage msg;
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (!c->connected()) {
                continue;
            }
            if (c->conn.poll(CAT_CHAT, msg).ok() && msg.has_chat()) {
                handle_incoming_chat(msg.chat());
                for (size_t j = 0; j < clients_.size(); j++) {
                    if (j != i && clients_[j]->connected()) {
                        clients_[j]->conn.send(msg);
                    }
                }
            }
            // Requests and stale frames that arrived while we were not in
            // the frame loop.
            const NetworkMessage *next = c->conn.peek(CAT_GAME);
            if (next && next->has_frame() && next->epoch() != epoch_ && !in_simulation) {
                c->conn.recv(CAT_GAME, msg);
                wclog(2, "discarding frame from epoch %u (now %u)", msg.epoch(), epoch_);
            } else if (next && next->has_start_briefing_req()) {
                c->conn.recv(CAT_GAME, msg);
                handle_briefing_request(c);
            }
        }
    }

    virtual void on_barracks() {
        accept_clients(true);
        wclog(1, "server has %d client slot(s)", (int)clients_.size());
    }

    virtual void check_mission_status() {}

    virtual void on_autopilot_begin(Bit16u camShipType, Bit16u camMode, Bit16u duration) {
        AutoPilotEvent *ape = pendingFrame_.mutable_frame()->add_event()->mutable_autopiloting();
        ape->set_cam_ship_type(camShipType);
        ape->set_cam_mode(camMode);
        ape->set_duration(duration);
        exchange(true);
        quiet_links();
    }

    virtual void on_autopilot_finished() {
        AutoPilotEvent *ape = pendingFrame_.mutable_frame()->add_event()->mutable_autopiloting();
        ape->set_finish_camera(true);
        // Wing Commander brings its one wingman along on an autopilot; the
        // second copy, while the AI flies it, is brought along here.
        int extra = is_wc1() ? extra_wing_slot() : -1;
        if (extra >= 0 && !client_for_slot(extra)) {
            park_beside(extra);
            wclog(2, "the second wingman (slot %d, the AI's) comes along after the autopilot", extra);
        }
        exchange(true);
        quiet_links();
    }

    // A cinematic: the frames around it are slow on both sides, and the
    // acks of the frames sent before it come after it, seconds later; none
    // of that is the link's doing.
    void quiet_links() {
        double now = perf_now_ms();
        for (size_t i = 0; i < clients_.size(); i++) {
            clients_[i]->link.quiet(now, LinkMonitor::kGraceMs);
        }
        modeTickAt_ = 0;
    }

    virtual void on_trampoline_idle() {
        flush_outgoing_frame();
    }

    virtual bool in_space() const {
        if (!within_briefed_mission) {
            return false;
        }
        for (size_t i = 0; i < clients_.size(); i++) {
            if (clients_[i]->connected() && clients_[i]->needsMissionStartState) {
                return false;
            }
        }
        return true;
    }

    // Our game starts an in-flight scene of the mission's script and sends
    // no frames for its length.  Said at once, out of band, so that a
    // wingman whose game has the same scene pending plays it now rather
    // than after ours (ClientSession::host_scene).  Comm answers are scenes
    // too: a wingman has none of those pending and lets them pass.
    virtual void on_in_flight_scene(int scene) {
        if (!within_briefed_mission) {
            return;
        }
        NetworkMessage msg;
        msg.mutable_in_flight_scene()->set_scene((Bit32u)scene);
        int told = 0;
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (c->connected() && c->inMission && !c->leftThisMission && c->conn.send(msg)) {
                told++;
            }
        }
        wclog(1, "in-flight scene %d starts here: %d wingm%s told", scene, told, told == 1 ? "an" : "en");
    }

    virtual void send_chat(const std::string &text) {
        NetworkMessage msg;
        Chat *chat = msg.mutable_chat();
        chat->set_ship_id(0);
        chat->set_message(text);
        chat->set_callsign(get_callsign());
        for (size_t i = 0; i < clients_.size(); i++) {
            if (clients_[i]->connected() && !clients_[i]->conn.send(msg)) {
                wclog(0, "failed to send chat to client %d", clients_[i]->net);
            }
        }
    }

private:
    RemoteClient *client_for_slot(int slot) const {
        if (slot < 0 || slot >= (int)clients_.size()) {
            return NULL;
        }
        RemoteClient *c = clients_[slot];
        // (A drone has no ship here; nor, any more, has a wingman that left
        // this mission: landed, its ship is despawned like any other.)
        return c->connected() && c->seat == SEAT_WINGMAN && !c->leftThisMission ? c : NULL;
    }

    // What a client is in this mission.  WC.EXE gives every mission a
    // wingman, and his ship is the client's.  WC2 has missions the story
    // flies alone: the slot is then empty or something else entirely (the
    // carrier, a transport to escort).  A wingman there is a fighter in the
    // player's wing list; without one the client is the gunner when the
    // leader's ship has turrets, and a drone when it has none.  WCSEAT
    // (drone, gunner) on the server overrides, for tests.
    Seat seat_for(const RemoteClient *c) const {
        if (!is_wc2() || !ds::known(ds::playerWing)) {
            return SEAT_WINGMAN;
        }
        const char *env = getenv("WCSEAT");
        const bool turrets = turret_count(kPlayerSlot) > 0;
        if (env && !strcasecmp(env, "drone")) {
            return SEAT_DRONE;
        }
        if (env && !strcasecmp(env, "gunner") && turrets) {
            return SEAT_GUNNER;
        }
        if (slot_in_use(c->net) && entity_type(c->net) == ET_SHIP) {
            Bit16u missionShip = rd16((Bit16u)(ds::slotMissionShip + 2 * c->net));
            for (int i = 0; i < 8; i++) {
                if (rd16((Bit16u)(ds::playerWing + 2 * i)) == missionShip) {
                    return SEAT_WINGMAN;
                }
            }
        }
        return turrets ? SEAT_GUNNER : SEAT_DRONE;
    }

    // Is the ship in a slot one a human can fly: a fighter of the player's
    // wing (seat_for's test)?
    bool wing_fighter_slot(int slot) const {
        if (!is_wc2() || !ds::known(ds::playerWing) || !slot_in_use(slot) || entity_type(slot) != ET_SHIP) {
            return false;
        }
        Bit16u missionShip = rd16((Bit16u)(ds::slotMissionShip + 2 * slot));
        for (int i = 0; i < 8; i++) {
            if (rd16((Bit16u)(ds::playerWing + 2 * i)) == missionShip) {
                return true;
            }
        }
        return false;
    }

    // The slot a client flies is its network id, given when it connected (1,
    // then 3).  In Wing Commander II the mission decides what is in a slot:
    // the carrier is slot 1 in most of Special Operations' missions and the
    // wingman slot 2, and a mission with a second wing fighter (SO3P,
    // tools/wc2wing) may have it in any slot.  So, with each mission's start
    // state: a client whose own slot holds no fighter of the wing takes the
    // first one no human flies, and keeps its seat as a gunner or a drone
    // only when there is none.  The id goes to the client with the start
    // state; the other machines know the ship by the server's slot number,
    // which is what it always was.
    void reseat(RemoteClient *c) {
        if (is_wc1()) {
            // The mission's wingman (slot 1), then the other wingmen in
            // flight (the mission's own, or the second copy of the wingman
            // that spawn_extra_wing made), whichever no human flies.
            std::vector<int> seats = wing_slots_wc1();
            for (size_t i = 0; i < seats.size(); i++) {
                int slot = seats[i];
                if (slot == c->net) {
                    return;
                }
                if (slot < (int)clients_.size() && clients_[slot]->connected()) {
                    continue;
                }
                wclog(1, "player %d takes slot %d, %s", c->net, slot, slot == kPlayerSlot + 1 ? "the mission's wingman"
                      : slot == extra_wing_slot() ? "the second wingman" : "another wingman the mission launched");
                move_client(c, slot);
                return;
            }
            return;
        }
        const char *env = getenv("WCSEAT");
        if (!is_wc2() || !ds::known(ds::playerWing) || (env && env[0]) || wing_fighter_slot(c->net)) {
            return;
        }
        for (int slot = kPlayerSlot + 1; slot <= kMaxShipSlot; slot++) {
            if (!wing_fighter_slot(slot) || (slot < (int)clients_.size() && clients_[slot]->connected())) {
                continue;
            }
            wclog(1, "player %d takes slot %d, the wing's fighter there (its slot %d holds %s)", c->net, slot, c->net,
                  !slot_in_use(c->net) ? "nothing" : entity_type(c->net) == ET_SHIP ? "a fighter not of the wing" : "a ship that is not a fighter");
            move_client(c, slot);
            return;
        }
    }

    void move_client(RemoteClient *c, int slot) {
        int from = c->net;
        while ((int)clients_.size() <= slot) {
            clients_.push_back(new RemoteClient((int)clients_.size()));
        }
        std::swap(clients_[from], clients_[slot]);
        clients_[from]->net = from;  // the unconnected one takes the old index
        c->net = slot;
    }

    // Frame.humans: the ships humans fly, by network id (the host's 0 first),
    // for the clients' own rules; `also` is a client about to be in the
    // mission (its start state).
    void list_humans(Frame *frame, const RemoteClient *also) const {
        frame->clear_humans();
        frame->add_humans(0);
        for (size_t i = 0; i < clients_.size(); i++) {
            const RemoteClient *c = clients_[i];
            if (c->connected() && c->seat == SEAT_WINGMAN && (c->inMission || c == also)) {
                frame->add_humans((Bit32u)c->net);
            }
        }
    }

    RemoteClient *allocate_client() {
        for (size_t i = 0; i < allowedIds_.size(); i++) {
            int net = allowedIds_[i];
            while (net >= (int)clients_.size()) {
                clients_.push_back(new RemoteClient((int)clients_.size()));
            }
            if (!clients_[net]->connected()) {
                return clients_[net];
            }
        }
        return NULL;
    }

    bool any_client_connected() const {
        for (size_t i = 0; i < clients_.size(); i++) {
            if (clients_[i]->connected()) {
                return true;
            }
        }
        return false;
    }

    // Accept pending connections.  With blockIfNone the call waits until at
    // least one client is connected: a server alone cannot fly the mission.
    void accept_clients(bool blockIfNone) {
        while (true) {
            bool block = blockIfNone && !any_client_connected();
            if (block) {
                wclog(1, "waiting for a client to connect (%s)", listener_->describe().c_str());
            }
            Stream *s = listener_->accept(block);
            if (!s) {
                return;
            }
            RemoteClient *c = allocate_client();
            if (!c) {
                wclog(1, "no free player slot; refusing %s", s->describe().c_str());
                s->close();
                delete s;
                return;
            }
            c->conn.adopt(s);
            NetworkMessage msg;
            if (!c->conn.recv(CAT_GAME, msg).ok() || !msg.has_connect()) {
                wclog(0, "client did not introduce itself");
                c->disconnect();
                continue;
            }
            c->callsign = msg.connect().callsign();
            c->leftThisMission = false;
            c->missionTreeProgress.clear();
            wclog(1, "%s connected as player %d (%s)", c->callsign.c_str(), c->net, c->conn.describe().c_str());
            if (within_briefed_mission) {
                // Joining a mission in progress: bring them in next frame.
                c->needsMissionStartState = true;
            }
        }
    }

    void handle_briefing_request(RemoteClient *c) {
        if (within_briefed_mission && !c->leftThisMission) {
            wclog(1, "player %d joins the mission in progress", c->net);
            send_briefing_state(c);
        } else {
            wclog(1, "player %d will get the next briefing", c->net);
            c->requestedBriefingStart = true;
        }
    }

    // A drone's hand on our ship: energy between the shields and the guns in
    // lumps of kLump (the Ferret's shields are 115 a side, the guns go to
    // 100), and the cruising speed through our own + and - keys, so that
    // the game does what it would for us.  The next health snapshot takes
    // the result to everyone.
    void apply_copilot(RemoteClient *c, Copilot::Action action) {
        if (c->seat != SEAT_DRONE || !ds::known(ds::curShield) || !ds::known(ds::gunEnergy) || !in_space()) {
            return;
        }
        const int kGunsFull = 100;
        Bit16u shieldAt = (Bit16u)(ds::curShield + 4 * kPlayerSlot), maxAt = (Bit16u)(ds::shieldMax + 4 * kPlayerSlot);
        int shield[2] = { (Bit16s)rd16(shieldAt), (Bit16s)rd16((Bit16u)(shieldAt + 2)) };
        int most[2] = { (Bit16s)rd16(maxAt), (Bit16s)rd16((Bit16u)(maxAt + 2)) };
        int guns = (Bit16s)rd16((Bit16u)(ds::gunEnergy + 2 * kPlayerSlot));
        // A fifth of a shield's full charge a press (an Epee's shields are 60
        // a side, a Ferret's 115; the guns go to 100).
        const int lump = std::max(10, (most[0] + most[1]) / 10);
        char line[96];
        switch (action) {
        case Copilot::SHIELDS_TO_REAR:
        case Copilot::SHIELDS_TO_FRONT: {
            int from = action == Copilot::SHIELDS_TO_REAR ? 0 : 1, to = 1 - from;
            int moved = std::max(0, std::min(lump, std::min(shield[from], most[to] - shield[to])));
            shield[from] -= moved;
            shield[to] += moved;
            snprintf(line, sizeof(line), "shields to the %s: %d/%d", to ? "rear" : "front", shield[0], shield[1]);
            break;
        }
        case Copilot::SHIELDS_TO_GUNS: {
            int moved = std::max(0, std::min(lump, std::min(kGunsFull - guns, shield[0] + shield[1])));
            // Half from each side; what one side cannot give, the other does.
            int fromFront = std::min(shield[0], moved / 2 + (moved & 1));
            int fromRear = std::min(shield[1], moved - fromFront);
            fromFront = moved - fromRear;
            shield[0] -= fromFront;
            shield[1] -= fromRear;
            guns += moved;
            snprintf(line, sizeof(line), "shields to guns: guns %d, shields %d/%d", guns, shield[0], shield[1]);
            break;
        }
        case Copilot::GUNS_TO_WEAKEST: {
            int to = shield[1] < shield[0] ? 1 : 0;
            int moved = std::max(0, std::min(lump, std::min(guns, most[to] - shield[to])));
            guns -= moved;
            shield[to] += moved;
            snprintf(line, sizeof(line), "guns to the %s shield: guns %d, shields %d/%d", to ? "rear" : "front", guns, shield[0], shield[1]);
            break;
        }
        case Copilot::SPEED_UP:
        case Copilot::SPEED_DOWN:
            tap_key(action == Copilot::SPEED_UP ? KBD_equals : KBD_minus);
            snprintf(line, sizeof(line), "cruising speed %s", action == Copilot::SPEED_UP ? "up" : "down");
            break;
        default:
            return;
        }
        wr16(shieldAt, (Bit16u)shield[0]);
        wr16((Bit16u)(shieldAt + 2), (Bit16u)shield[1]);
        wr16((Bit16u)(ds::gunEnergy + 2 * kPlayerSlot), (Bit16u)guns);
        wclog(2, "copilot %s: %s", c->callsign.c_str(), line);  // (the gauges show it; nothing on the screen)
    }

    bool send_briefing_state(RemoteClient *c) {
        NetworkMessage msg;
        *msg.mutable_briefing_start() = lastBriefing_;
        msg.mutable_briefing_start()->set_rocks(rocks_mode());
        msg.mutable_briefing_start()->set_wing_bumps(wing_bumps_mode());
        msg.set_epoch(epoch_);
        msg.set_frame_number(frameNumber_);
        if (!c->conn.send(msg)) {
            c->disconnect();
            return false;
        }
        c->requestedBriefingStart = false;
        c->needsMissionStartState = true;
        return true;
    }

    bool send_start_state(RemoteClient *c) {
        NetworkMessage msg;
        msg.set_epoch(epoch_);
        msg.set_frame_number(frameNumber_);
        Game *game = msg.mutable_game();
        reseat(c);
        game->set_assigned_player_id(c->net);
        c->seat = seat_for(c);
        c->mannedTurret = -1;
        c->acked = frameNumber_;
        game->set_seat(c->seat);
        if (c->seat == SEAT_WINGMAN) {
            take_station(c->net);
        }
        game->set_fps(pace_fps());
        Frame *frame = game->mutable_starting_state();
        list_humans(frame, c);
        frame->set_rocks(rocks_mode());
        frame->set_wing_bumps(wing_bumps_mode());
        frame->set_mode(mode());
        c->link.reset();
        c->fps = -1;
        for (int slot = kPlayerSlot; slot <= kMaxShipSlot; slot++) {
            if (!slot_in_use(slot)) {
                continue;
            }
            ShipUpdate *su = update_for(frame, slot);
            populate_location(su->mutable_loc(), slot);
            if (!client_for_slot(slot)) {
                fill_health(su->mutable_health(), read_health(slot));
            }
        }
        const std::map<int, Spawn> &spawns = spawns_.all();
        for (std::map<int, Spawn>::const_iterator it = spawns.begin(); it != spawns.end(); ++it) {
            Spawn *sp = frame->add_event()->mutable_spawn();
            *sp = it->second;
            stamp_pilot(sp);
        }
        add_cloak_events(frame);  // after the spawns they belong to
        wclog(1, "sending mission start state to player %d (%s, %d ships)", c->net,
              c->seat == SEAT_DRONE ? "a drone" : c->seat == SEAT_GUNNER ? "the gunner" : "the wingman", (int)spawns.size());
        if (!c->conn.send(msg)) {
            c->disconnect();
            return false;
        }
        c->inMission = true;
        c->leftThisMission = false;  // it has the mission's start state: it is in it
        list_humans(pendingFrame_.mutable_frame(), NULL);  // (this frame's list was made before it joined)
        // Its frame numbers start again at 1 with this mission (its epoch),
        // so what we remember of its last mission's frames is forgotten too.
        // Left as they were (029ec859, 2026-10-09, to 17), every position it sent in
        // the next mission was "behind the times" until its count passed
        // the last mission's, and the host's copy of its ship was the AI's:
        // a wingman whose shots did no damage all through Gimle 3.
        c->lastFrame = 0;
        c->lastFrameAt = 0;
        c->lastPosition = 0;
        c->lastPositionEmu = 0;
        c->hasLastLoc = false;
        c->link.reset();
        hadClientInMission_ = true;
        // (No word of it on the screen: the game looks as it always did, and
        // the page's lobby says what each seat is.)
        // Spawns queued in the pending frame are already in the registry copy.
        c->skipPendingEvents = pendingFrame_.frame().event_size();
        return true;
    }

    // WC2 spawns a wingman on top of his leader and lets the formation AI
    // sort it out, and that AI is off for a human's ship: put a ship that
    // starts inside ours on our left wing instead (where the game's own
    // formation puts the first wingman: 752 lengths of the right vector).
    // Is `slot` within the two hit spheres of `other`?
    bool inside(int slot, int other) const {
        if (!slot_in_use(other) || other == slot) {
            return false;
        }
        Vec3 a = read_vec(vec_slot(ds::gPositionVector, other));
        Vec3 pos = read_vec(vec_slot(ds::gPositionVector, slot));
        double dx = (double)pos.x - a.x, dy = (double)pos.y - a.y, dz = (double)pos.z - a.z;
        double clear = 256.0 * (rd16((Bit16u)(ds::gMaybeShipRadius + 2 * other)) + rd16((Bit16u)(ds::gMaybeShipRadius + 2 * slot)));
        return dx * dx + dy * dy + dz * dz <= clear * clear;
    }

    void take_station(int slot) {
        if (!slot_in_use(slot) || !slot_in_use(kPlayerSlot)) {
            return;
        }
        // inside our ship, or inside another human's (a second copy of the
        // wingman's record spawns where the first one did)
        bool crowded = inside(slot, kPlayerSlot);
        for (size_t i = 0; i < clients_.size() && !crowded; i++) {
            if ((int)i != slot && clients_[i]->connected() && clients_[i]->inMission && clients_[i]->seat == SEAT_WINGMAN) {
                crowded = inside(slot, (int)i);
            }
        }
        if (!crowded) {
            return;
        }
        park_beside(slot);
    }

    // `slot` on our wing, with our heading and speed: the first wingman on
    // the left, the second on the right, a third further out on the left;
    // the AI's copy of the wingman on the right.
    void park_beside(int slot) {
        Vec3 own = read_vec(vec_slot(ds::gPositionVector, kPlayerSlot));
        Vec3 pos = own;
        enum { kAbeam = 752 };
        int others = 0;
        for (size_t i = 0; i < clients_.size(); i++) {
            const RemoteClient *o = clients_[i];
            if ((int)i != slot && o->connected() && o->inMission && o->seat == SEAT_WINGMAN) {
                others++;
            }
        }
        if (!client_for_slot(slot)) {
            others = 1;
        }
        double abeam = (others % 2 == 0 ? -1.0 : 1.0) * kAbeam * (1 + others / 2);
        Vec3 right = read_vec(vec_slot(ds::gOrientationRightVector, kPlayerSlot));
        pos.x = own.x + right.x * abeam;
        pos.y = own.y + right.y * abeam;
        pos.z = own.z + right.z * abeam;
        write_vec(vec_slot(ds::gPositionVector, slot), pos);
        write_vec(vec_slot(ds::gVelocityVector, slot), read_vec(vec_slot(ds::gVelocityVector, kPlayerSlot)));
        write_vec(vec_slot(ds::gOrientationRightVector, slot), right);
        write_vec(vec_slot(ds::gOrientationUpVector, slot), read_vec(vec_slot(ds::gOrientationUpVector, kPlayerSlot)));
        write_vec(vec_slot(ds::gOrientationFrontVector, slot), read_vec(vec_slot(ds::gOrientationFrontVector, kPlayerSlot)));
        wclog(1, "player %d starts on our %s wing", slot, abeam < 0 ? "left" : "right");
    }

    void reset_pending_frame() {
        pendingFrame_.Clear();
        pendingFrame_.set_epoch(epoch_);
        pendingFrame_.set_frame_number(frameNumber_);
    }

    void populate_server_frame() {
        Frame *frame = pendingFrame_.mutable_frame();
        list_humans(frame, NULL);
        health_.next_frame();
        for (int slot = kPlayerSlot; slot <= kMaxShipSlot; slot++) {
            if (!slot_in_use(slot)) {
                continue;
            }
            ShipUpdate *su = update_for(frame, slot);
            populate_location(su->mutable_loc(), slot);
            RemoteClient *rc = client_for_slot(slot);
            if (rc && rc->hasLastLoc) {
                // (debugging) the angle between the heading it reported last and our copy's now
                Vec3 f = read_vec(vec_slot(ds::gOrientationFrontVector, slot));
                Vec3 r = to_vec(rc->lastLoc.fore());
                double dot = (double)f.x * r.x + (double)f.y * r.y + (double)f.z * r.z;
                double n = sqrt((double)f.x * f.x + (double)f.y * f.y + (double)f.z * f.z) * sqrt((double)r.x * r.x + (double)r.y * r.y + (double)r.z * r.z);
                double deg = n > 0 ? acos(std::max(-1.0, std::min(1.0, dot / n))) * 180.0 / 3.14159265 : 0;
                if (deg > rc->driftWorst) rc->driftWorst = deg;
                double now = PIC_FullIndex();
                if (now - rc->driftWindowAt > 5000.0) {
                    if (rc->driftWindowAt > 0 && (rc->driftWorst > 5.0 || rc->driftUpdates == 0)) {
                        wclog(1, "player %d's copy turned up to %.0f deg from its reported heading (%d updates in %.0f s; in mission %d, ai %s)",
                              slot, rc->driftWorst, rc->driftUpdates, (now - rc->driftWindowAt) / 1000.0, (int)rc->inMission,
                              is_remote_player_slot(slot) ? "held off" : "RUNNING");
                    }
                    rc->driftWindowAt = now; rc->driftWorst = 0; rc->driftUpdates = 0;
                }
            }
            if (!is_remote_player_slot(slot)) {
                ShipHealthState h;
                if (health_.should_send(slot, &h)) {
                    fill_health(su->mutable_health(), h);
                }
            }
        }
        health_.end_frame();
    }

    void merge_client_location(RemoteClient *c, Bit64u frameNo, const Location &loc) {
        apply_location(loc, c->net);
        if (loc.has_fore()) {
            c->lastLoc = loc;
            c->hasLastLoc = true;
            c->driftUpdates++;
        }
        *update_for(pendingFrame_.mutable_frame(), c->net)->mutable_loc() = loc;
        c->lastPosition = frameNo;
        c->lastPositionEmu = PIC_FullIndex();
    }

    // In the high-latency mode a client's position comes by the best-effort
    // channel as well, a frame number with it: what is fresher than the
    // last one taken is applied, the rest is behind the times.
    void drain_client_positions(RemoteClient *c) {
        NetworkMessage msg;
        for (int n = 0; n < 64 && c->seat == SEAT_WINGMAN; n++) {
            RecvStatus st = c->conn.poll_best_effort(msg);
            if (!st.ok()) {
                return;
            }
            if (msg.epoch() != epoch_ || !msg.has_frame() || msg.frame_number() <= c->lastPosition || !slot_in_use(c->net)) {
                continue;
            }
            for (int i = 0; i < msg.frame().update_size(); i++) {
                const ShipUpdate &su = msg.frame().update(i);
                if (su.has_ship_id() && (int)su.ship_id() == c->net && su.has_loc()) {
                    merge_client_location(c, msg.frame_number(), su.loc());
                }
            }
        }
    }

    void merge_client_frame(RemoteClient *c, const Frame &frame, Bit64u frameNo) {
        if (c->seat != SEAT_WINGMAN) {
            // A drone or a gunner has no ship of its own here: its ending
            // ends nothing, and all that counts of what it does is a
            // gunner's turret and a drone's hand on our energy.
            if (frame.has_player_end()) {
                wclog(1, "player %d (%s) %s", c->net, c->seat == SEAT_DRONE ? "a drone" : "the gunner",
                      status_name(frame.player_end().state()));
                c->inMission = false;
                c->leftThisMission = true;
                c->needsMissionStartState = false;
                c->mannedTurret = -1;
                return;
            }
            if (c->seat == SEAT_GUNNER) {
                c->mannedTurret = frame.has_manned_turret() ? (int)frame.manned_turret() - 1 : -1;
                for (int i = 0; i < frame.event_size(); i++) {
                    if (frame.event(i).has_turret()) {
                        enqueue_turret_fire(frame.event(i).turret());
                    }
                }
            } else {
                for (int i = 0; i < frame.event_size(); i++) {
                    if (frame.event(i).has_copilot()) {
                        apply_copilot(c, frame.event(i).copilot().action());
                    }
                }
            }
            return;
        }
        if (frame.has_player_end()) {
            int status = frame.player_end().state();
            if (is_wc2() && (status == EndLand || status == EndCarrier)) {
                // In Wing Commander II a landing is the wingman's own
                // (2026-10-14): it has left the mission, its ship leaves our
                // space, and we fly on until we land too; the next briefing
                // is where the games meet again.  (Our own landing still
                // lands every wingman: a wingman's game cannot fly on
                // without ours.  Wing Commander keeps the shared fate below
                // for landings too, as it always had.)
                wclog(1, "player %d landed; flying on until we land too", c->net);
                c->inMission = false;
                c->leftThisMission = true;
                c->needsMissionStartState = false;
                enqueue_wingman_lost(c->net, false);
                return;
            }
            // Shared fate: a wing lives and dies together.  A wingman's
            // death, ejection or exit ends our mission too; our mission-end
            // frame then ends it for every other client.
            wclog(1, "player %d %s; ending the mission for everyone", c->net, status_name(status));
            c->inMission = false;
            c->leftThisMission = true;
            c->needsMissionStartState = false;
            wr16(ds::missionStatus, (Bit16u)status);
            return;
        }
        if (frame.has_mission_end()) {
            // A shared ending (landing): end our mission too, with their
            // nav-point progress folded in when the debriefing runs.
            wr16(ds::missionStatus, (Bit16u)frame.mission_end().game_update());
            c->missionTreeProgress = frame.mission_end().mission_tree_progress();
            wclog(1, "player %d %s; ending the mission", c->net, status_name(frame.mission_end().game_update()));
        }
        for (int i = 0; i < frame.update_size(); i++) {
            const ShipUpdate &su = frame.update(i);
            if (!su.has_ship_id() || (int)su.ship_id() != c->net) {
                continue;  // clients only speak for their own ship
            }
            if (!slot_in_use(c->net)) {
                continue;
            }
            if (su.has_loc() && frameNo > c->lastPosition) {
                // (A fresher one may have come by the best-effort channel.)
                merge_client_location(c, frameNo, su.loc());
            }
            if (su.has_health()) {
                ShipHealthState h;
                if (parse_health(su.health(), &h)) {
                    write_health(c->net, h, true);  // our copy keeps the mission's wingman pilot
                    *update_for(pendingFrame_.mutable_frame(), c->net)->mutable_health() = su.health();
                    wclog(3, "mirrored player %d health: %s", c->net, describe_health(c->net).c_str());
                }
            }
        }
        for (int i = 0; i < frame.event_size(); i++) {
            const Event &ev = frame.event(i);
            if (ev.has_fire()) {
                Event copy;
                *copy.mutable_fire() = ev.fire();
                copy.mutable_fire()->set_shooter(c->net);
                enqueue_remote_event(copy);
            } else if (ev.has_damage()) {
                // One of that player's own rocks or mines hit a ship of ours.
                enqueue_reported_damage(ev.damage());
            } else {
                wclog(2, "ignoring an event from player %d that is neither fire nor a hit report", c->net);
            }
        }
    }

    // Takes in whatever a client in the mission has sent since last time,
    // without waiting: frames are merged (or, when only flushing, looked at
    // for the player's ending), requests answered.  Returns false when the
    // client is gone.
    bool drain_client(RemoteClient *c, bool mergeUpdates) {
        NetworkMessage msg;
        for (int n = 0; n < 64; n++) {
            RecvStatus st = c->conn.poll(CAT_GAME, msg);
            if (st.no_data()) {
                return true;
            }
            if (!st.ok()) {
                return false;
            }
            if (msg.has_start_briefing_req()) {
                handle_briefing_request(c);
            } else if (msg.has_start_mission_req()) {
                if (!send_start_state(c)) {
                    return false;
                }
            } else if (msg.has_frame()) {
                if (msg.epoch() != epoch_) {
                    wclog(2, "player %d sent a frame from epoch %u (now %u)", c->net, msg.epoch(), epoch_);
                    continue;
                }
                double now = perf_now_ms();
                if (msg.frame().has_ack() && msg.frame().ack() > c->acked) {
                    c->acked = msg.frame().ack();
                    c->link.acked(c->acked, msg.frame().ack_delay(), now);
                }
                if (msg.frame_number() > c->lastFrame) {
                    c->lastFrame = msg.frame_number();
                    c->lastFrameAt = now;
                }
                if (msg.frame().has_fps()) {
                    c->fps = (int)msg.frame().fps();
                }
                if (mergeUpdates || msg.frame().has_player_end()) {
                    merge_client_frame(c, msg.frame(), msg.frame_number());
                }
                if (msg.frame().has_flying() && !msg.frame().flying() && c->inMission) {
                    // Its game has left the flight loop (whatever its ending,
                    // seen or not): nothing to wait for, nothing to send.
                    wclog(1, "player %d is out of the flight loop: not waiting for its frames", c->net);
                    c->inMission = false;
                    c->leftThisMission = true;
                    c->needsMissionStartState = false;
                }
            } else {
                wclog(0, "unexpected %s message from player %d", message_type_name(msg), c->net);
                return false;
            }
        }
        return true;
    }

    // The server does not wait for a client's frame before its own: a round
    // trip per frame would tie the frame rate to the connection.  It runs on
    // with what has arrived, up to kWindow frames ahead of what a client says
    // it has applied (six frames cover a round trip of 300 ms at 15 frames a
    // second); beyond that it waits, so a client that has stopped stops the
    // game instead of being left behind.
    // In the high-latency mode the window is what the round trip needs plus
    // a second for its spikes (mode_tick), between a second and five.
    enum { kWindow = 6, kHighWindowMin = 20, kHighWindowMax = 100 };
    int window() const { return mode() == MODE_HIGH ? highWindow_ : (int)kWindow; }
    bool behind(const RemoteClient *c) const {
        return c->connected() && c->inMission && frameNumber_ > c->acked + window();
    }

    // Once a second of real time, in flight: a sample of both frame rates
    // for every client's link monitor, the high-latency window, and the
    // mode in auto.  Thresholds from the simulated link (docs section 3b):
    // the low-latency exchange holds 20 frames a second to a round trip of
    // the window's length, and past that the frame rates themselves say
    // when the link is too much for it: the spikes (a lost message sent
    // again, a queue) are logged, but the frame rates are the measure.
    void mode_tick() {
        double now = perf_now_ms();
        if (modeTickAt_ > 0 && now - modeTickAt_ < 1000.0) {
            return;
        }
        int ownFps = modeTickAt_ > 0 ? (int)(ownFrames_ * 1000.0 / (now - modeTickAt_) + 0.5) : -1;
        // A pause is no measure of the link: our own frame loop stood still
        // (an in-flight scene, the nav screen, a comm answer: no tick for
        // more than a second), or a wingman's did (no frame of its for more
        // than a second: we wait for it, and our own rate falls with it).
        // No samples then, nor for the seconds after, which catch up.
        bool ownPause = modeTickAt_ > 0 && now - modeTickAt_ > 1500.0;
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (!c->connected() || !c->inMission) {
                continue;
            }
            bool theirPause = c->lastFrameAt > 0 && now - c->lastFrameAt > 1000.0;
            if (ownPause || theirPause) {
                c->link.quiet(now, LinkMonitor::kGraceMs);
            }
        }
        modeTickAt_ = now;
        ownFrames_ = 0;
        double rtt = -1, worst = 0, spiky = 0;
        int spikes = 0;
        double theirFps = -1, ourFps = -1;
        int dips = 0, seconds = 0;
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (!c->connected() || !c->inMission) {
                continue;
            }
            c->link.second(now, c->fps, ownFps);
            c->fps = -1;
            if (!c->link.known()) {
                continue;
            }
            rtt = std::max(rtt, c->link.rtt());
            worst = std::max(worst, c->link.ceiling(now, 5000.0));
            spikes = std::max(spikes, c->link.spikes_per_min());
            spiky = std::max(spiky, c->link.spike_share(now, 10000.0));
            theirFps = std::max(theirFps, c->link.fps_avg(now, true, 10000.0));
            ourFps = c->link.fps_avg(now, false, 10000.0);
            dips = std::max(dips, std::max(c->link.seconds_under(now, true, 10, 60000.0), c->link.seconds_under(now, false, 10, 60000.0)));
            seconds = std::max(seconds, c->link.seconds_known());
        }
        rtt_ = rtt;
        if (rtt < 0) {
            return;
        }
        highWindow_ = std::max((int)kHighWindowMin, std::min((int)kHighWindowMax, (int)((worst + 1000.0) / frame_ms() + 0.5)));
        int before = mode();
        if (autoMode_ == MODE_LOW) {
            const char *why = NULL;
            if (rtt > kRttHigh) {
                why = "the round trip";
            } else if (seconds >= 10 && ((theirFps >= 0 && theirFps < 15) || (ourFps >= 0 && ourFps < 15 && perf_last().valid && perf_last().waitMs > 5))) {
                why = "the frame rate";
            } else if (dips > 1) {
                why = "the dips";
            }
            if (why) {
                autoMode_ = MODE_HIGH;
                modeSince_ = now;
                wclog(1, "link: %s calls for the high-latency mode (round trip %.0f ms, worst %.0f, spiky %.0f%% of the last 10 s, %d spikes/min, wingman %.0f fps, ours %.0f, %d dips)",
                      why, rtt, worst, 100 * spiky, spikes, theirFps, ourFps, dips);
            }
        } else if (now - modeSince_ > 30000.0 && worst < kRttLow && spikes == 0) {
            autoMode_ = MODE_LOW;
            modeSince_ = now;
            wclog(1, "link: good again, back to the low-latency mode (round trip %.0f ms, worst %.0f)", rtt, worst);
        }
        if (mode() != before) {
            announce_mode();
        }
        static double lastLog = 0;
        if (now - lastLog > 5000.0) {
            lastLog = now;
            wclog(2, "link: round trip %.0f ms (worst %.0f), spiky %.0f%%, %d spikes/min, wingman %.0f fps, ours %.0f fps, %d dips/min: %s mode%s, window %d",
                  rtt, worst, 100 * spiky, spikes, theirFps, ourFps, dips, mode_name(mode()), modeSetting_ == MODE_AUTO ? " (auto)" : "", window());
        }
    }
    // 300: the window's edge, six frames at 20 a second.  The low-latency
    // exchange held 20 frames a second at a measured 250 and 15.7 at 350.
    enum { kRttHigh = 300, kRttLow = 150 };

    std::string mode_notice() const {
        char buf[96];
        if (rtt_ >= 0) {
            snprintf(buf, sizeof(buf), "%s mode%s, round trip %.0f ms", mode() == MODE_HIGH ? "High-latency" : "Low-latency",
                     modeSetting_ == MODE_AUTO ? " (auto)" : "", rtt_);
        } else {
            snprintf(buf, sizeof(buf), "%s mode%s", mode() == MODE_HIGH ? "High-latency" : "Low-latency", modeSetting_ == MODE_AUTO ? " (auto)" : "");
        }
        return buf;
    }

    // The mode in force changed: the clients hear with the next frame.
    void announce_mode() {
        wclog(1, "exchange mode: %s", mode_name(mode()));
        if (within_briefed_mission) {
            pendingFrame_.mutable_frame()->set_mode(mode());
        }
        show_notice(mode_notice());
    }

    // One round of the frame exchange.  With serverOnlyFlush what the clients
    // have sent is still taken in but only
    // their player_end is honoured, and the frame is sent immediately instead
    // of after the trampoline.
    // The mission setup writes a ship's pilot (dseg:D1A2) after the spawn
    // returns, so the byte is only known when the spawn event leaves.
    void stamp_pilot(Spawn *sp) {
        if (!sp->has_ship_id()) {
            return;
        }
        int slot = NetworkShipId::from_net(sp->ship_id()).to_local();
        if (slot_in_use(slot)) {
            sp->set_pilot(rd8((Bit16u)(ds::shipStateByte + slot)));
        }
    }

    void exchange(bool serverOnlyFlush) {
        frameNumber_ += 1;
        ownFrames_++;
        if (frameNumber_ % 300 == 1) {
            wclog(2, "frame %llu: own health %s", (unsigned long long)frameNumber_, describe_health(kPlayerSlot).c_str());
        }
        populate_server_frame();
        for (int i = 0; i < pendingFrame_.frame().event_size(); i++) {
            Event *ev = pendingFrame_.mutable_frame()->mutable_event(i);
            if (ev->has_spawn()) {
                stamp_pilot(ev->mutable_spawn());
            }
        }
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (!c->connected()) {
                continue;
            }
            if (c->needsMissionStartState) {
                c->needsMissionStartState = false;
                if (!send_start_state(c)) {
                    continue;
                }
            }
            if (!c->inMission) {
                continue;
            }
            if (!drain_client(c, !serverOnlyFlush)) {
                wclog(1, "player %d disconnected", c->net);
                c->disconnect();
                continue;
            }
            if (!serverOnlyFlush) {
                drain_client_positions(c);
                if (c->seat == SEAT_WINGMAN && c->lastPosition > 0) {
                    perf_position_age(PIC_FullIndex() - c->lastPositionEmu);
                }
            }
        }
        sendFrameAtIdle_ = true;
        // A server alone cannot start the mission: until a wingman has had
        // its start state this waits for one.  After that the mission goes
        // on with whoever is left: a wingman whose game is gone (the link
        // closed when its program ended) does not hold the host's.
        accept_clients(!hadClientInMission_);
        if (!serverOnlyFlush) {
            mode_tick();
        }
        if (!serverOnlyFlush && wantWingBumps_ != wing_bumps_mode()) {
            set_wing_bumps_mode(wantWingBumps_);
            pendingFrame_.mutable_frame()->set_wing_bumps(wantWingBumps_);
            show_notice(wing_bumps_notice(wantWingBumps_));
        }
        if (!serverOnlyFlush && wantRocks_ != rocks_mode()) {
            // The host switched the rocks: ours go or come back with this
            // frame's jobs, and every client does the same with its own.
            enqueue_rocks_change(wantRocks_);
            pendingFrame_.mutable_frame()->set_rocks(wantRocks_);
            show_notice(rocks_notice(wantRocks_));
        }
        if (serverOnlyFlush) {
            flush_outgoing_frame();
        } else {
            for (size_t i = 0; i < clients_.size(); i++) {
                if (behind(clients_[i])) {
                    // Before anything else of this frame, and in emulated
                    // time (ClientLagJob).
                    g_trampoline.enqueue_front(new ClientLagJob(this));
                    break;
                }
            }
            g_trampoline.run_before_current_instruction();
            ignoreNextFrameTop_ = true;
        }
    }

public:
    // For ClientLagJob: takes in what has arrived; true while a client is
    // still too far behind for the server to go on.
    bool clients_behind() {
        bool waiting = false;
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (!c->connected() || !c->inMission) {
                continue;
            }
            if (!drain_client(c, true)) {
                wclog(1, "player %d disconnected", c->net);
                c->disconnect();
            } else if (behind(c)) {
                // However long: a wingman in flight that is in a scene of
                // its own comes back, and running ahead would part the
                // games.  One out of the flight loop says so (Frame.flying)
                // and is not waited for; one whose game is gone closes its
                // link (on_program_ended).
                waiting = true;
            }
        }
        return waiting;
    }

private:

    void flush_outgoing_frame() {
        if (!sendFrameAtIdle_) {
            return;
        }
        sendFrameAtIdle_ = false;
        NetworkMessage positions;
        if (mode() == MODE_HIGH && best_effort_wanted()) {
            positions.set_epoch(epoch_);
            positions.set_frame_number(pendingFrame_.frame_number());
            Frame *lite = positions.mutable_frame();
            for (int i = 0; i < pendingFrame_.frame().update_size(); i++) {
                const ShipUpdate &su = pendingFrame_.frame().update(i);
                if (su.has_loc()) {
                    ShipUpdate *out = lite->add_update();
                    out->set_ship_id(su.ship_id());
                    *out->mutable_loc() = su.loc();
                }
            }
            if (lite->update_size() == 0) {
                positions.Clear();
            }
        }
        for (size_t i = 0; i < clients_.size(); i++) {
            RemoteClient *c = clients_[i];
            if (!c->connected() || !c->inMission) {
                continue;
            }
            // Each client gets its own copy: our ack of its last frame, and
            // only the events it has not had with its start state.
            NetworkMessage msg = pendingFrame_;
            Frame *f = msg.mutable_frame();
            if (c->skipPendingEvents > 0) {
                int skip = c->skipPendingEvents < f->event_size() ? c->skipPendingEvents : f->event_size();
                f->mutable_event()->DeleteSubrange(0, skip);
                c->skipPendingEvents = 0;
            }
            double now = perf_now_ms();
            if (c->lastFrame > 0) {
                f->set_ack(c->lastFrame);
                f->set_ack_delay((Bit32u)std::max(0.0, now - c->lastFrameAt));
            }
            c->link.sent(msg.frame_number(), now);
            if (!c->conn.send(msg)) {
                wclog(1, "player %d disconnected while sending", c->net);
                c->disconnect();
            } else if (f->has_mission_end()) {
                c->inMission = false;  // they leave the frame loop on this frame
            } else if (mode() == MODE_HIGH && positions.has_frame()) {
                // The same positions once more by the best-effort channel:
                // they get there when the reliable one is held up by a
                // message being sent again, and the next frame's supersede
                // them anyway (ClientSession::take_positions).
                c->conn.send_best_effort(positions);
            }
        }
        reset_pending_frame();
    }

    Listener *listener_;
    std::vector<RemoteClient *> clients_;
    std::vector<int> allowedIds_;
    Bit32u epoch_;
    Bit64u frameNumber_;
    bool hadClientInMission_;    // a wingman has had this mission's start state
    bool extraWingTried_ = false;  // WC1: the second wingman's spawn was tried this mission
    ServerSendBriefingStart lastBriefing_;
    NetworkMessage pendingFrame_;
    SpawnRegistry spawns_;
    HealthPublisher health_;
    bool sendFrameAtIdle_;
    bool ignoreNextFrameTop_;
    int wantRocks_;  // the host's choice (RocksMode); rocks_mode() follows it at a frame top
    int wantWingBumps_;  // the host's choice (WingBumps), likewise
    int modeSetting_;   // ExchangeMode: what was asked for
    int autoMode_;      // what the link calls for (MODE_LOW or MODE_HIGH)
    int highWindow_;    // frames the server may run ahead in the high-latency mode
    double modeSince_;  // when autoMode_ last changed (real ms)
    double modeTickAt_; // the last mode_tick (real ms)
    int ownFrames_;     // our frames since then
    double rtt_;        // the longest of the clients' round trips, ms (-1: none known)
};

// ---------------------------------------------------------------------------
// Client

extern bool g_pendingUninit;

class ClientSession;

// A client's wait for the server's frame, in emulated time: looks for the
// frame, and while it is not there lets the emulated CPU idle to the next
// timer event and looks again.
class ServerFrameJob : public VmJob {
public:
    explicit ServerFrameJob(ClientSession *client, double began = -1)
        : client_(client), began_(began < 0 ? PIC_FullIndex() : began), done_(false) {}
    virtual bool start();
    virtual void finish() {
        if (!done_) {
            g_trampoline.enqueue_front(new ServerFrameJob(client_, began_));
        }
    }
    virtual const char *describe() const { return "server frame"; }

private:
    ClientSession *client_;
    double began_;
    bool done_;
};

class ClientSession : public Session {
public:
    ClientSession()
        : shipNet_(0), seat_(SEAT_WINGMAN), chase_(true), seatFrames_(-1), lastServerFrame_(0), lastServerFrameAt_(0),
          lastServerFrameEmu_(-1), mode_(MODE_LOW), lastWrittenMissionStatus_(Proceed), lastVictoryPlusOne_(0),
          isFresh_(true), hasRestartedMission_(false), hasSentConnect_(false),
          ignoreNextFrameTop_(false), ownMissionOver_(false), dead_(false), sceneDue_(false), epoch_(0), frameNumber_(0) {
        callsign_ = get_callsign();
        g_entityMap = &entities_;
    }
    ~ClientSession() {
        g_entityMap = NULL;
    }

    bool connect() {
        // Hang up any previous connection before opening a new one, so a
        // lobby stream never attaches to a generation we are about to end.
        conn_.close();
        Stream *s = NULL;
#ifdef C_LOBBYLINK
        if (g_lobby) {
            s = g_lobby->open_peer(kHostPlayer, kHostWaitMs);
        } else
#endif
        {
            s = tcp_connect(net_config.host, net_config.portstr);
        }
        if (!s) {
            return false;
        }
        conn_.adopt(s);
        return true;
    }

    std::string where() const { return conn_.describe(); }

    virtual Role role() const { return ROLE_CLIENT; }

    virtual bool is_remote_player_slot(int slot) const {
        if (entities_.is_mapped(0) && entities_.net_to_local(0) == slot) {
            return true;  // the host's body
        }
        for (std::set<int>::const_iterator it = humans_.begin(); it != humans_.end(); ++it) {
            if (*it != 0 && *it != shipNet_ && entities_.is_mapped(*it) && entities_.net_to_local(*it) == slot) {
                return true;  // another wingman's ship
            }
        }
        return false;
    }
    // A network id a human flies (the server's word, Frame.humans).
    bool is_human_net(int net) const { return net == 0 || humans_.count(net) > 0; }

    virtual void queue_outgoing_event(const Event &ev) {
        *pendingFrame_.mutable_frame()->add_event() = ev;
    }

    virtual void request_rocks(int mode) {
        (void)mode;
        show_notice("Only the host can switch asteroids and mines");
    }

    virtual void request_wing_bumps(int mode) {
        (void)mode;
        show_notice("Only the host can switch collisions between wingmen");
    }

    virtual bool paces() const { return mode_ == MODE_HIGH; }
    virtual int mode() const { return mode_; }
    virtual double rtt_ms() const { return link_.known() ? link_.rtt() : -1; }
    virtual void request_mode(int setting) {
        (void)setting;
        char buf[96];
        if (link_.known()) {
            snprintf(buf, sizeof(buf), "%s mode (the host decides), round trip %.0f ms",
                     mode_ == MODE_HIGH ? "High-latency" : "Low-latency", link_.rtt());
        } else {
            snprintf(buf, sizeof(buf), "%s mode (the host decides)", mode_ == MODE_HIGH ? "High-latency" : "Low-latency");
        }
        show_notice(buf);
    }

    virtual Seat seat() const { return seat_; }

    virtual bool is_copilot() const { return seat_ == SEAT_DRONE && chase_ && !dead_; }

    virtual void copilot(Copilot::Action action) {
        static const char *const kSaid[] = { "", "shields to the rear", "shields to the front", "shields to the guns",
                                             "guns to the weakest shield", "cruising speed up", "cruising speed down" };
        Event ev;
        ev.mutable_copilot()->set_action(action);
        queue_outgoing_event(ev);
        wclog(2, "copilot: %s", action >= 1 && action <= 6 ? kSaid[action] : "?");  // (the gauges show the result within the frame)
    }

    virtual void toggle_chase() {
        if (seat_ != SEAT_DRONE) {
            show_notice("Only a drone can ride behind the leader");
            return;
        }
        chase_ = !chase_;
        wclog(1, "drone: %s", chase_ ? "riding behind the leader as copilot" : "flying free");
    }

    virtual void on_mission_starting(int mission, int series) {
        (void)mission; (void)series;
        liven_everyone();
        within_briefed_mission = true;
        if (!dead_) {
            pre_briefing_handshake();
        }
    }

    virtual void on_mission_reset() {
        despawn_all_ships();
        entities_.reset();
        humans_.clear();
        reset_pending_fires();
        pendingHealth_.clear();
        health_.reset();
        hasRestartedMission_ = true;
        ownMissionOver_ = false;
        sceneDue_ = false;
        wclog(1, "client reset its mission state");
    }

    virtual void on_mission_victory_calc() {
        if (!lastVictoryPlusOne_ && conn_.is_open()) {
            // The host's score comes on its own after its landing animation,
            // which ran beside ours: a moment's wait for it at most.
            double until = perf_now_ms() + 1500.0;
            while (perf_now_ms() < until) {
                NetworkMessage msg;
                RecvStatus st = conn_.poll(CAT_GAME, msg);
                if (st.no_data()) {
                    pause_ms(20);
                    continue;
                }
                if (!st.ok()) {
                    break;
                }
                if (msg.has_frame() && msg.frame().has_mission_end() && msg.frame().mission_end().has_victory_points_plus_one()) {
                    lastVictoryPlusOne_ = msg.frame().mission_end().victory_points_plus_one();
                    break;
                }
            }
        }
        if (lastVictoryPlusOne_ && ds::known(ds::victoryPoints)) {
            wr16(ds::victoryPoints, (Bit16u)(lastVictoryPlusOne_ - 1));
            wclog(1, "victory points from server: %d", (int)lastVictoryPlusOne_ - 1);
        }
        lastVictoryPlusOne_ = 0;
    }

    virtual void on_mission_ended() {
        liven_everyone();
        within_briefed_mission = false;
        // Out of the flight loop: said to the server, which waits for the
        // frames of a wingman in flight and for nobody else.  The loop can
        // end without passing a status check the hooks watch (the game's
        // own scenes write the status and leave): an ending told to nobody
        // goes with it.
        if (!dead_ && conn_.is_open()) {
            int status = rd16(ds::missionStatus);
            NetworkMessage msg;
            msg.set_epoch(epoch_);
            msg.set_frame_number(++frameNumber_);
            msg.mutable_frame()->set_flying(false);
            if (!ownMissionOver_ && lastWrittenMissionStatus_ == Proceed && status > Proceed && status <= EndExit) {
                ownMissionOver_ = true;
                wclog(1, "we %s (the flight loop ended before a status check); leaving the mission", status_name(status));
                msg.mutable_frame()->mutable_player_end()->set_state((GameState)status);
            }
            wclog(2, "out of the flight loop: told the server");
            conn_.send(msg);
        }
        if (!missionTreeProgress_.empty()) {
            load_mission_tree_progress(missionTreeProgress_);
            missionTreeProgress_.clear();
        }
        if (!missionEndCampaignState_.empty()) {
            // The host's record as its flight loop returned, ours as ours
            // does: the campaign loop ahead branches the host's way.
            load_campaign_state(missionEndCampaignState_);
            missionEndCampaignState_.clear();
        }
        if (lastWrittenMissionStatus_ == EndDeath || lastWrittenMissionStatus_ == EndCarrier) {
            // No victory computation after these endings, so reset here
            // (entity map, health mirror, own-mission-over flag) or the next
            // mission starts with stale state.
            on_mission_reset();
        }
        lastWrittenMissionStatus_ = Proceed;
    }

    virtual void on_frame_top() {
        if (ignoreNextFrameTop_) {
            ignoreNextFrameTop_ = false;
            return;
        }
        if (dead_) {
            return;
        }
        NetworkMessage msg;
        if (hasRestartedMission_) {
            hasRestartedMission_ = false;
            wclog(1, "waiting for the mission start state");
            while (true) {
                if (!conn_.recv(CAT_GAME, msg).ok()) {
                    drop("lost the server while waiting for the mission start");
                    return;
                }
                if (msg.has_game()) {
                    break;
                }
            }
            apply_start_state(msg);
        }
        if (isFresh_ && !hasSentConnect_) {
            if (!send_connect()) {
                drop("could not introduce ourselves");
                return;
            }
        }
        pendingFrame_.set_epoch(epoch_);
        pendingFrame_.set_frame_number(++frameNumber_);
        if (frameNumber_ % 300 == 1) {
            wclog(2, "frame %llu: own health %s", (unsigned long long)frameNumber_, describe_health(kPlayerSlot).c_str());
        }
        populate_own_update();
        double now = perf_now_ms(), emu = PIC_FullIndex();
        frameTimes_.push_back(emu);
        while (!frameTimes_.empty() && (frameTimes_.front() < emu - 1000.0 || frameTimes_.front() > emu)) {
            frameTimes_.pop_front();
        }
        Frame *frame = pendingFrame_.mutable_frame();
        frame->set_ack(lastServerFrame_);
        if (lastServerFrame_ > 0) {
            frame->set_ack_delay((Bit32u)std::max(0.0, now - lastServerFrameAt_));
        }
        frame->set_fps((Bit32u)frameTimes_.size());
        link_.sent(frameNumber_, now);
        if (!conn_.send(pendingFrame_)) {
            drop("lost the server while sending a frame");
            return;
        }
        if (mode_ == MODE_HIGH && seat_ == SEAT_WINGMAN && best_effort_wanted()) {
            // Our position once more by the best-effort channel (see the
            // server's flush_outgoing_frame).
            NetworkMessage positions;
            positions.set_epoch(epoch_);
            positions.set_frame_number(frameNumber_);
            ShipUpdate *su = positions.mutable_frame()->add_update();
            su->set_ship_id(shipNet_);
            populate_location(su->mutable_loc(), kPlayerSlot);
            conn_.send_best_effort(positions);
        }
        pendingFrame_.Clear();
        if (lastPositionEmu_ >= 0) {
            perf_position_age(emu - lastPositionEmu_);
        }
        if (mode_ == MODE_HIGH) {
            // High latency: the frames are paced here (pace_frame), and
            // whatever the server has sent by now is applied; its frames
            // are not waited for, unless there has been none for longer
            // than the round trip and its spikes can explain: then the
            // server, or the link, has stopped, and so do we.
            take_positions();
            bool any = take_server_frame();
            if (!any && lastServerFrameEmu_ >= 0 && emu - lastServerFrameEmu_ > silence_ms()) {
                wclog(2, "no frame from the server for %.0f ms: waiting", emu - lastServerFrameEmu_);
                g_trampoline.enqueue(new ServerFrameJob(this));
            }
        } else {
            // Low latency: the server's frame comes when the server's own
            // frame is due.  We wait for it on the trampoline, in emulated
            // time (ServerFrameJob): blocking here would stop the
            // emulator, and with it the music and the clock, for most of
            // every frame.
            g_trampoline.enqueue(new ServerFrameJob(this));
        }
        g_trampoline.run_before_current_instruction();
        ignoreNextFrameTop_ = true;
    }

    // How long the server may be silent in the high-latency mode before we
    // wait for it: the round trip plus a second, at least a second and a half.
    double silence_ms() const {
        return std::max(1500.0, (link_.known() ? link_.ceiling(perf_now_ms(), 5000.0) : 0) + 1000.0);
    }

    // The server's positions by the best-effort channel (the high-latency
    // mode): the freshest of what has come is applied when it is newer than
    // the last positions taken from either channel.
    void take_positions() {
        NetworkMessage best;
        bool any = false;
        for (int n = 0; n < 64; n++) {
            NetworkMessage msg;
            RecvStatus st = conn_.poll_best_effort(msg);
            if (!st.ok()) {
                break;
            }
            if (msg.epoch() == epoch_ && msg.has_frame() && msg.frame_number() > lastPosition_ && (!any || msg.frame_number() > best.frame_number())) {
                best = msg;
                any = true;
            }
        }
        if (any) {
            apply_frame(best.frame(), false, true);
            lastPosition_ = best.frame_number();
            lastPositionEmu_ = PIC_FullIndex();
        }
    }

    // True when the wait for the server is over: at least one frame of its
    // came and was applied (its events are queued behind the waiting job),
    // or the server is gone.  Every frame that has arrived is applied, in
    // order: after a hiccup of ours or the connection's there can be more
    // than one, and the server goes by what we have applied (Frame.ack).
    bool take_server_frame() {
        bool any = false;
        for (int n = 0; n < 16; n++) {
            NetworkMessage msg;
            RecvStatus st = conn_.poll(CAT_GAME, msg);
            if (st.no_data()) {
                break;
            }
            if (!st.ok() || !msg.has_frame()) {
                drop("lost the server while waiting for a frame");
                return true;
            }
            any = true;
            if (msg.epoch() != epoch_) {
                wclog(2, "ignoring frame from epoch %u (ours %u)", msg.epoch(), epoch_);
                continue;
            }
            bool applyOwn = false;
            for (int i = 0; i < msg.frame().event_size(); i++) {
                if (msg.frame().event(i).has_autopiloting()) {
                    applyOwn = true;
                }
            }
            lastServerFrame_ = msg.frame_number();
            lastServerFrameAt_ = perf_now_ms();
            lastServerFrameEmu_ = PIC_FullIndex();
            if (msg.frame().has_ack()) {
                link_.acked(msg.frame().ack(), msg.frame().ack_delay(), lastServerFrameAt_);
            }
            // Its positions, unless fresher ones came by the best-effort
            // channel (a cinematic's are taken whatever came).
            bool applyLoc = applyOwn || msg.frame_number() > lastPosition_;
            apply_frame(msg.frame(), applyOwn, applyLoc);
            if (applyLoc) {
                lastPosition_ = msg.frame_number();
                lastPositionEmu_ = lastServerFrameEmu_;
            }
            if (msg.frame().has_mission_end() || applyOwn) {
                break;  // the mission is over, or a cinematic comes first: the rest can wait
            }
        }
        return any;
    }

    virtual void on_async_tick() {
        if (dead_) {
            return;
        }
        NetworkMessage msg;
        RecvStatus st = conn_.poll(CAT_CHAT, msg);
        if (st.ok() && msg.has_in_flight_scene()) {
            host_scene((int)msg.in_flight_scene().scene());
        } else if (st.ok()) {
            handle_incoming_chat(msg.chat());
        } else if (st.failed()) {
            drop("the server went away");
        }
    }

    // Our game is gone (its own exit, or an error that dumped it to the DOS
    // prompt): close the link, so that the server sees us leave at once
    // instead of waiting for our frames.
    virtual void on_program_ended() {
        if (!dead_ && conn_.is_open()) {
            wclog(1, "our game has ended: leaving the server");
            conn_.close();
        }
    }

    virtual void on_barracks() {
        if (dead_) {
            return;
        }
        for (int attempt = 0; attempt < 10; attempt++) {
            if (!conn_.is_open() && !connect()) {
                wclog(1, "connect attempt %d failed", attempt + 1);
                pause_ms(2000);
                continue;
            }
            if (!hasSentConnect_ && !send_connect()) {
                conn_.close();
                pause_ms(2000);
                continue;
            }
            wclog(1, "connected to %s as %s", conn_.describe().c_str(), callsign_.c_str());
            return;
        }
    }

    virtual void check_mission_status() {
        int status = rd16(ds::missionStatus);
        if (status == lastWrittenMissionStatus_) {
            return;
        }
        if (status == Proceed) {
            lastWrittenMissionStatus_ = Proceed;
            return;
        }
        // Our own ending (landed, died, ejected, quit): tell the server so it
        // removes our ship, and let our game end the mission for us.  Only
        // the leader's ending is shared with everyone.
        if (!ownMissionOver_) {
            ownMissionOver_ = true;
            wclog(1, "we %s; leaving the mission", status_name(status));
            NetworkMessage msg;
            msg.set_epoch(epoch_);
            msg.set_frame_number(++frameNumber_);
            msg.mutable_frame()->mutable_player_end()->set_state((GameState)status);
            conn_.send(msg);
        }
        lastWrittenMissionStatus_ = (GameState)status;
    }

    virtual void on_trampoline_idle() {
        for (size_t i = 0; i < pendingHealth_.size(); i++) {
            const ShipUpdate &su = pendingHealth_[i];
            if (!entities_.is_mapped(su.ship_id())) {
                continue;
            }
            int slot = entities_.net_to_local(su.ship_id());
            if ((slot == kPlayerSlot && seat_ != SEAT_GUNNER) || !slot_in_use(slot)) {
                continue;  // our own ship's health is ours, unless it is the leader's ship we ride in
            }
            ShipHealthState h;
            if (parse_health(su.health(), &h)) {
                write_health(slot, h, is_human_net(su.ship_id()));
                wclog(3, "mirrored net %d health into slot %d: %s", su.ship_id(), slot, describe_health(slot).c_str());
            }
        }
        pendingHealth_.clear();
    }

    virtual bool in_space() const {
        return within_briefed_mission && !hasRestartedMission_;
    }

    // For ServerFrameJob: once, after host_scene made our pending scene
    // due, so that the frame which plays it runs without a server frame.
    bool scene_due() {
        bool due = sceneDue_;
        sceneDue_ = false;
        return due;
    }

    virtual void send_chat(const std::string &text) {
        NetworkMessage msg;
        Chat *chat = msg.mutable_chat();
        chat->set_ship_id(shipNet_);
        chat->set_message(text);
        chat->set_callsign(callsign_);
        if (!conn_.send(msg)) {
            wclog(0, "failed to send chat");
        }
    }

private:
    // The host's game has started an in-flight scene of the mission's
    // script.  WC2 arms such a scene on every machine by the nav point's
    // own steps and plays it forty frames later (seg001:0580), or at the end
    // of an autopilot at once (ovr107:01CA): the host, whose autopilot it
    // is, is always first; we would wait out its scene with our countdown
    // standing, then play ours while it waits out ours.  With the same
    // scene pending here it is made due at our next frame, and the wait for
    // the host's frame lets that frame run (scene_due).
    void host_scene(int scene) {
        if (!within_briefed_mission || !ds::known(ds::pendingScene) || !ds::known(ds::pendingSceneArmed) ||
            !ds::known(ds::pendingSceneTimer)) {
            return;
        }
        enum { kNoScene = 0xff, kDueCount = 0x28 };  // seg001:05A4 plays the scene at a count of 0x28
        int ours = rd8(ds::pendingScene);
        if (ours != scene) {
            wclog(1, "the host's game plays scene %d; ours has %d pending", scene, ours == kNoScene ? -1 : ours);
            return;
        }
        wr16(ds::pendingSceneArmed, 1);
        wr16(ds::pendingSceneTimer, kDueCount);
        sceneDue_ = true;
        wclog(1, "the host's game plays scene %d: ours plays it at the next frame", scene);
    }

    // Tear the session down at the next safe point; the dispatcher deletes us.
    void drop(const char *why) {
        wclog(0, "%s", why);
        conn_.close();
        dead_ = true;
        g_pendingUninit = true;
        wc_net_countdown = 0;  // (the hooks act on it at their next call)
    }

    bool send_connect() {
        NetworkMessage msg;
        msg.mutable_connect()->set_callsign(callsign_);
        if (!conn_.send(msg)) {
            return false;
        }
        hasSentConnect_ = true;
        return true;
    }

    void pre_briefing_handshake() {
        hasRestartedMission_ = true;
        NetworkMessage msg;
        msg.mutable_start_briefing_req();
        wclog(1, "asking the server which briefing to load");
        if (!conn_.send(msg)) {
            drop("lost the server while requesting the briefing");
            return;
        }
        while (true) {
            if (!conn_.recv(CAT_GAME, msg).ok()) {
                drop("lost the server while waiting for the briefing");
                return;
            }
            if (msg.has_briefing_start()) {
                break;
            }
        }
        epoch_ = msg.epoch();
        frameNumber_ = msg.frame_number();
        const ServerSendBriefingStart &b = msg.briefing_start();
        if (b.has_rocks()) {
            set_rocks_mode((int)b.rocks());  // before our own nav point setup runs
        }
        if (b.has_wing_bumps()) {
            set_wing_bumps_mode((int)b.wing_bumps());
        }
        load_mission_tree_progress(b.mission_tree_progress());
        load_campaign_state(b.campaign_state());
        wr8(ds::currentMission, (Bit8u)b.mission_id());
        wr8(ds::currentSeries, (Bit8u)b.series_id());
        wclog(1, "briefing for mission %d/%d", b.mission_id(), b.series_id());
    }

    void apply_start_state(const NetworkMessage &msg) {
        const Game &game = msg.game();
        shipNet_ = game.assigned_player_id();
        seat_ = !game.has_seat() ? SEAT_WINGMAN : game.seat() == SEAT_DRONE ? SEAT_DRONE
              : game.seat() == SEAT_GUNNER ? SEAT_GUNNER : SEAT_WINGMAN;
        if (seat_ == SEAT_GUNNER) {
            // Our ship is the leader's: the server's network id 0 is our
            // slot 0, and where it is, how it flies and how it fares come
            // from the server like any other ship's.
            entities_.set_own_ship(-1);
            entities_.map(kPlayerSlot, kPlayerSlot);
            seatFrames_ = 0;
        } else if (seat_ == SEAT_DRONE) {
            // No ship of the mission is ours: every spawn of the server's is
            // another ship here, and the leader's is made first.  (The seat
            // and its keys are the page's lobby to explain: the game's
            // screen stays the game's.)
            entities_.set_own_ship(-1);
            enqueue_host_body();
        } else {
            entities_.set_own_ship(shipNet_);
        }
        if (game.has_starting_state()) {
            apply_frame(game.starting_state(), true, true);
        }
        lastPosition_ = 0;
        lastPositionEmu_ = -1;
        if (game.has_fps()) {
            set_pace_fps((int)game.fps());
        }
        link_.reset();
        frameTimes_.clear();
        lastServerFrameEmu_ = -1;
        epoch_ = msg.epoch();
        frameNumber_ = msg.frame_number();
        lastServerFrame_ = msg.frame_number();
        isFresh_ = false;
        wclog(1, "we are player %d%s; epoch %u frame %llu", shipNet_,
              seat_ == SEAT_DRONE ? " (a drone)" : seat_ == SEAT_GUNNER ? " (the gunner)" : "", epoch_,
              (unsigned long long)frameNumber_);
    }

    // A drone has no place in the mission of its own: where the server has
    // the leader, close behind him.  At the start, after an autopilot
    // (the leader is somewhere else entirely) and every frame while riding.
    void follow_leader(const Location &leader) {
        // In lengths of the orientation vectors.  The "up" vector points
        // down the screen: this is below the leader, who is then in the
        // upper half of the canopy, clear of the instrument panel.
        enum { kBehind = 420, kBelow = 45 };
        if (!leader.has_pos() || !leader.has_fore() || !leader.has_up()) {
            return;
        }
        Location loc = leader;
        Vec3 pos = to_vec(leader.pos()), fore = to_vec(leader.fore()), up = to_vec(leader.up());
        pos.x += up.x * kBelow - fore.x * kBehind;
        pos.y += up.y * kBelow - fore.y * kBehind;
        pos.z += up.z * kBelow - fore.z * kBehind;
        write_vec(vec_slot(ds::gPositionVector, kPlayerSlot), pos);
        loc.clear_pos();
        apply_location(loc, kPlayerSlot);
    }

    void populate_own_update() {
        if (seat_ != SEAT_WINGMAN) {
            // No ship of our own to report, but a frame still goes out: the
            // exchange is one message each way.  A gunner says which turret
            // it sits in.
            Frame *frame = pendingFrame_.mutable_frame();
            if (seat_ == SEAT_DRONE && chase_ && entities_.is_mapped(0)) {
                // The copilot sees the leader's readouts: the leader's
                // shields, armour, guns and damage go on the drone's own
                // ship every frame, whose cockpit is the same (one mission,
                // one ship for the player).  Its own place and speed are the
                // leader's already (follow_leader).
                int leader = entities_.net_to_local(0);
                if (leader != kInvalidSlot && leader != kPlayerSlot && slot_in_use(leader)) {
                    write_health(kPlayerSlot, read_health(leader), true);
                }
            }
            if (seat_ == SEAT_GUNNER && ds::known(ds::cameraMode)) {
                enum { kInTurret = 4, kSeatAfter = 8 };
                // The game starts everybody in the pilot's seat; a gunner's
                // place is the turret.  A few frames in, when the cockpit
                // is up.
                if (seatFrames_ >= 0 && ++seatFrames_ >= kSeatAfter) {
                    seatFrames_ = -1;
                    if (rd16(ds::cameraMode) != kInTurret) {
                        enqueue_gunner_seat();
                    }
                }
                frame->set_manned_turret(rd16(ds::cameraMode) == kInTurret ? rd16(ds::mannedTurret) + 1 : 0);
            }
            return;
        }
        ShipUpdate *su = update_for(pendingFrame_.mutable_frame(), shipNet_);
        populate_location(su->mutable_loc(), kPlayerSlot);
        health_.next_frame();
        ShipHealthState h;
        if (health_.should_send(kPlayerSlot, &h)) {
            fill_health(su->mutable_health(), h);
        }
        health_.end_frame();
    }

    void apply_frame(const Frame &frame, bool applyOwnLocation, bool applyLoc) {
        if (frame.has_mission_end()) {
            const MissionEnd &end = frame.mission_end();
            if (end.has_game_update()) {
                wr16(ds::missionStatus, (Bit16u)end.game_update());
                lastWrittenMissionStatus_ = end.game_update();
                missionTreeProgress_ = end.mission_tree_progress();
                missionEndCampaignState_ = end.campaign_state();
                wclog(1, "server ended the mission: %s", status_name(end.game_update()));
            }
            if (end.has_landed_on_plus_one() && ds::known(ds::landedOn)) {
                // The ship the host landed on: WC.EXE's landing sequence draws
                // the carrier from this word (the game's own landing check
                // writes it; a landing by the host's word left it -1, and the
                // Tiger's Claw was missing from the wingman's landing).
                int net = (int)end.landed_on_plus_one() - 1;
                if (entities_.is_mapped(net)) {
                    wr16(ds::landedOn, (Bit16u)entities_.net_to_local(net));
                    wclog(1, "landing with the host on net %d (slot %d here)", net, entities_.net_to_local(net));
                }
            }
            if (end.has_victory_points_plus_one()) {
                lastVictoryPlusOne_ = end.victory_points_plus_one();  // (the score alone, after the host's landing animation)
            }
        }
        if (frame.has_wing_bumps() && (int)frame.wing_bumps() != wing_bumps_mode()) {
            set_wing_bumps_mode((int)frame.wing_bumps());
            show_notice(wing_bumps_notice(wing_bumps_mode()));
        }
        if (frame.humans_size() > 0) {
            std::set<int> humans;
            for (int i = 0; i < frame.humans_size(); i++) {
                humans.insert((int)frame.humans(i));
            }
            if (humans != humans_) {
                std::string list;
                for (std::set<int>::const_iterator it = humans.begin(); it != humans.end(); ++it) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%s%d", list.empty() ? "" : " ", *it);
                    list += buf;
                }
                wclog(1, "humans fly net %s (we are %d)", list.c_str(), shipNet_);
                humans_ = humans;
            }
        }
        if (frame.has_rocks() && (int)frame.rocks() != rocks_mode()) {
            // The host switched the rocks in flight: the jobs run with this
            // frame's events.
            enqueue_rocks_change((int)frame.rocks());
            show_notice(rocks_notice(rocks_mode()));
        }
        if (frame.has_mode() && (int)frame.mode() != mode_ && (frame.mode() == MODE_LOW || frame.mode() == MODE_HIGH)) {
            mode_ = (int)frame.mode();
            wclog(1, "exchange mode: %s (the server's word)", mode_name(mode_));
            if (within_briefed_mission && !hasRestartedMission_) {
                show_notice(mode_ == MODE_HIGH ? "High-latency mode: the host's game runs ahead" : "Low-latency mode");
            }
        }
        for (int i = 0; i < frame.update_size(); i++) {
            const ShipUpdate &su = frame.update(i);
            if (!su.has_ship_id()) {
                continue;
            }
            // Our own ship is slot 0 whatever the map knows yet: the start
            // state comes before any spawn is replayed, and carries where
            // the server has our ship (the wingman's place, not the
            // leader's, where our own mission setup put us).
            bool own = seat_ == SEAT_WINGMAN && (int)su.ship_id() == shipNet_;
            if (seat_ == SEAT_DRONE && su.ship_id() == (Bit32u)kPlayerSlot && su.has_loc() && applyLoc && (applyOwnLocation || chase_)) {
                follow_leader(su.loc());
            }
            if (!own && !entities_.is_mapped(su.ship_id())) {
                continue;
            }
            int slot = own ? (int)kPlayerSlot : entities_.net_to_local(su.ship_id());
            if (is_temp_slot(slot)) {
                continue;
            }
            if (su.has_loc() && applyLoc && (!own || applyOwnLocation)) {
                apply_location(su.loc(), slot);
            }
            if (su.has_health() && !own) {
                pendingHealth_.push_back(su);
            }
        }
        for (int i = 0; i < frame.event_size(); i++) {
            enqueue_remote_event(frame.event(i));
        }
    }

    Connection conn_;
    int shipNet_;
    std::set<int> humans_;  // network ids humans fly (Frame.humans)
    Seat seat_;
    bool chase_;  // a drone rides behind the leader
    int seatFrames_;  // a gunner: frames flown before it is put in its turret (-1: done)
    Bit64u lastServerFrame_;  // the number of the last server frame applied (Frame.ack)
    double lastServerFrameAt_;   // when it came (real ms): our ack_delay
    double lastServerFrameEmu_;  // the same in emulated ms (-1: none yet this mission)
    int mode_;                   // ExchangeMode in force, the server's word
    LinkMonitor link_;           // the round trip, from the server's acks of our frames
    std::deque<double> frameTimes_;  // our frame tops of the last emulated second (Frame.fps)
    EntityMap entities_;
    std::string callsign_;
    GameState lastWrittenMissionStatus_;
    Bit32u lastVictoryPlusOne_;
    std::string missionTreeProgress_;
    std::string missionEndCampaignState_;  // WC2: the host's campaign record at its flight loop's return
    bool isFresh_;
    bool hasRestartedMission_;
    bool hasSentConnect_;
    bool ignoreNextFrameTop_;
    bool ownMissionOver_;
    bool dead_;
    bool sceneDue_;  // host_scene: our pending scene plays at the next frame, which runs without a server frame
    Bit32u epoch_;
    Bit64u frameNumber_;
    NetworkMessage pendingFrame_;
    std::vector<ShipUpdate> pendingHealth_;
    HealthPublisher health_;
    // The server frame whose positions were applied last (by either channel), and when (emulated ms; -1: none yet).
    Bit64u lastPosition_ = 0;
    double lastPositionEmu_ = -1;
};

bool ClientLagJob::start() {
    if (g_session != server_ || !server_->clients_behind()) {
        done_ = true;
        perf_idle_wait(PIC_FullIndex() - began_);
        return false;
    }
    idle_call();
    return true;
}

bool ServerFrameJob::start() {
    // (The session can be gone: a drop tears it down at the next safe point.)
    if (g_session != client_) {
        done_ = true;
        perf_idle_wait(PIC_FullIndex() - began_);
        return false;
    }
    bool due = client_->scene_due();  // (consumed whether or not a frame came)
    if (client_->take_server_frame() || due) {
        done_ = true;
        perf_idle_wait(PIC_FullIndex() - began_);
        return false;
    }
    idle_call();
    return true;
}

static void trampoline_idle() {
    if (g_session) {
        g_session->on_trampoline_idle();
    }
}

}  // namespace wc

// ---------------------------------------------------------------------------
// Public API

using namespace wc;

bool init_network() {
    // WCNET=0 flies alone: no server, no room, the game's own wingman.
    const char *off = getenv("WCNET");
    if (off && off[0] == '0') {
        static bool said = false;
        if (!said) {
            said = true;
            wclog(1, "networking disabled (WCNET=0)");
        }
        return false;
    }
    net_config.reset_from_env();
    uninit_network();
    g_trampoline.set_idle_callback(trampoline_idle);
    bool client;
    std::string where;
    if (net_config.use_lobby()) {
#ifdef C_LOBBYLINK
        g_lobby = LobbyHub::join(net_config);
        if (!g_lobby) {
            return false;
        }
        client = !g_lobby->is_host();
        where = "room " + g_lobby->code();
#else
        wclog(0, "WCROOM is set but this build has no lobbylink support (needs cargo at configure time)");
        return false;
#endif
    } else {
#ifdef __EMSCRIPTEN__
        wclog(0, "the browser build has no TCP transport: set WCROOM (a room code) to play");
        return false;
#endif
        client = net_config.host && net_config.host[0];
        where = client ? std::string(net_config.host) + ":" + net_config.portstr
                       : std::string("port ") + net_config.portstr;
    }
    if (client) {
        ClientSession *c = new ClientSession();
        if (!c->connect()) {
            wclog(0, "could not connect to %s", where.c_str());
            delete c;
            uninit_network();
            return false;
        }
        where = c->where();
        g_session = c;
    } else {
        Listener *l = NULL;
#ifdef C_LOBBYLINK
        if (g_lobby) {
            l = g_lobby->make_listener();
        } else
#endif
        {
            l = tcp_listen(net_config.portstr);
        }
        if (!l) {
            wclog(0, "could not listen on %s", where.c_str());
            uninit_network();
            return false;
        }
        where = l->describe();
        g_session = new ServerSession(l);
    }
    fprintf(stderr,
            "=========================================================\n"
            "  %s  --  %s\n"
            "=========================================================\n\n",
            client ? "CLIENT" : "SERVER", where.c_str());
    return true;
}

void uninit_network() {
    net_config.reset_from_env();
    if (g_session) {
        Session *s = g_session;
        g_session = NULL;
        delete s;
    }
#ifdef C_LOBBYLINK
    if (g_lobby) {
        LobbyHub *l = g_lobby;
        g_lobby = NULL;
        delete l;
    }
#endif
}

bool is_wc_connected() {
    return g_session != NULL && has_started_up;
}

const char *wc_net_role() {
    if (!g_session) {
        return "";
    }
    return g_session->is_server() ? "server" : "client";
}

const char *wc_net_status_note() {
#ifdef C_LOBBYLINK
    if (g_session && g_lobby && !g_lobby->signaling_alive()) {
        return "The connection to the lobby server was lost: the game goes on, but nobody new can join.";
    }
#endif
    return "";
}

bool in_space() {
    return g_session != NULL && g_session->in_space();
}

bool wc_net_overlay_chat() {
    return !in_space() || !ds::known(ds::commGlobalTxt);
}

void wc_net_set_rocks(int mode) {
    if (g_session) {
        g_session->request_rocks(mode);
    } else {
        set_rocks_mode(mode);  // alone: fields from the next nav point on
    }
}

int wc_net_rocks() {
    return rocks_mode();
}

void wc_net_set_wing_bumps(int mode) {
    if (g_session) {
        g_session->request_wing_bumps(mode);
    } else {
        set_wing_bumps_mode(mode);
    }
}

int wc_net_wing_bumps() {
    return wing_bumps_mode();
}

void wc_net_set_mode(int setting) {
    if (g_session) {
        g_session->request_mode(setting);
    }
}

int wc_net_mode_setting() {
    return g_session ? g_session->mode_setting() : mode_from_name(getenv("WCNET_MODE"));
}

int wc_net_mode() {
    return g_session ? g_session->mode() : MODE_LOW;
}

double wc_net_rtt_ms() {
    return g_session ? g_session->rtt_ms() : -1;
}

void wcnetSendChatMessage(const std::string &msg) {
    wclog(1, "comms: message sent");  // (the page remembers: the hint about the 0 key is not needed again)
    // "/rocks on", "/rocks soft" and "/rocks off" in the comms prompt are a
    // command, not a message.
    if (strncasecmp(msg.c_str(), "/rocks", 6) == 0) {
        std::string arg = msg.substr(6);
        size_t first = arg.find_first_not_of(' ');
        arg = first == std::string::npos ? "" : arg.substr(first);
        if (!strcasecmp(arg.c_str(), "on")) {
            wc_net_set_rocks(ROCKS_ON);
        } else if (!strcasecmp(arg.c_str(), "off")) {
            wc_net_set_rocks(ROCKS_OFF);
        } else if (!strcasecmp(arg.c_str(), "soft")) {
            wc_net_set_rocks(ROCKS_SOFT);
        } else {
            show_notice(std::string(rocks_notice(rocks_mode())) + " (/rocks on, soft, off)");
        }
        return;
    }
    // "/bumps harmless" and "/bumps game": collisions between the humans'
    // ships (the host's to set); "/bumps" alone says which is in force.
    if (strncasecmp(msg.c_str(), "/bumps", 6) == 0) {
        std::string arg = msg.substr(6);
        size_t first = arg.find_first_not_of(' ');
        arg = first == std::string::npos ? "" : arg.substr(first);
        if (!strcasecmp(arg.c_str(), "harmless") || !strcasecmp(arg.c_str(), "off")) {
            wc_net_set_wing_bumps(WING_BUMPS_HARMLESS);
        } else if (!strcasecmp(arg.c_str(), "game") || !strcasecmp(arg.c_str(), "on")) {
            wc_net_set_wing_bumps(WING_BUMPS_GAME);
        } else {
            show_notice(std::string(wing_bumps_notice(wing_bumps_mode())) + " (/bumps harmless, game)");
        }
        return;
    }
    // "/latency auto", "/latency low" and "/latency high": the exchange
    // mode (the host's to set); "/latency" alone says which is in force.
    if (strncasecmp(msg.c_str(), "/latency", 8) == 0) {
        std::string arg = msg.substr(8);
        size_t first = arg.find_first_not_of(' ');
        arg = first == std::string::npos ? "" : arg.substr(first);
        if (!g_session) {
            show_notice("Not connected");
        } else if (!g_session->is_server()) {
            g_session->request_mode(MODE_AUTO);  // (a client is told who decides, and the mode in force)
        } else if (arg.empty()) {
            char buf[128], rtt[32] = "";
            if (g_session->rtt_ms() >= 0) {
                snprintf(rtt, sizeof(rtt), ", round trip %.0f ms", g_session->rtt_ms());
            }
            snprintf(buf, sizeof(buf), "%s mode%s%s (/latency auto, low, high)", g_session->mode() == MODE_HIGH ? "High-latency" : "Low-latency",
                     g_session->mode_setting() == MODE_AUTO ? " (auto)" : "", rtt);
            show_notice(buf);
        } else {
            g_session->request_mode(mode_from_name(arg.c_str()));
        }
        return;
    }
    if (strncasecmp(msg.c_str(), "/chase", 6) == 0) {
        if (g_session) {
            g_session->toggle_chase();
        }
        return;
    }
    if (g_session) {
        g_session->send_chat(msg);
    }
}
