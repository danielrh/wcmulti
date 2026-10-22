#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include "dosbox.h"
#include "cpu.h"
#include "pic.h"
#include "wcnet_perf.h"
#include "wcnet_log.h"
#include "wcnet_memory.h"
#include "wcnet_game.h"
#include "wcnet_vm.h"

namespace wc {

static PerfSample g_last = { false, 0, 0, 0, 0, 0, 0, 0, 0 };
static double g_paced = 0;  // emulated ms of this interval spent waiting for a frame's turn
static double g_emuStart = -1, g_realStart = 0, g_lastFrameEmu = 0, g_wait = 0, g_worst = 0;
static double g_ageSum = 0, g_ageWorst = 0;
static int g_frames = 0, g_ages = 0;

double perf_now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void perf_wait(double ms) {
    g_wait += ms;
}

void perf_idle_wait(double emulatedMs) {
    g_wait += emulatedMs;
    g_paced += emulatedMs;  // not working either
}

void perf_position_age(double ms) {
    g_ageSum += ms;
    g_ages++;
    if (ms > g_ageWorst) {
        g_ageWorst = ms;
    }
}

const PerfSample &perf_last() {
    return g_last;
}

// ---------------------------------------------------------------------------
// Pacing

static int g_fps = -1;              // -1: not decided yet
static double g_frameDue = -1;      // emulated ms at which the next frame may begin
static double g_lastTop = -1;
static Bit32s g_baseCycles = 0;     // what was set before the flight's boost
static Bit32s g_boostCycles = 0;    // what we set (0: not boosted)

static int env_int(const char *name, int fallback) {
    const char *env = getenv(name);
    return env && env[0] ? atoi(env) : fallback;
}

int pace_fps() {
    if (g_fps < 0) {
        g_fps = env_int("WCFPS", is_wc1() ? 30 : 0);
        if (g_fps < 0 || g_fps > 100) {
            g_fps = 0;
        }
    }
    return g_fps;
}

void set_pace_fps(int fps) {
    g_fps = fps < 0 || fps > 100 ? 0 : fps;
}

// Idles the emulated CPU (idle_call) until the frame is due.
class PaceJob : public VmJob {
public:
    explicit PaceJob(double until) : until_(until) {}
    virtual bool start() {
        if (PIC_FullIndex() >= until_) {
            return false;
        }
        idle_call();
        return true;
    }
    virtual void finish() {
        if (PIC_FullIndex() < until_) {
            g_trampoline.enqueue_front(new PaceJob(until_));
        }
    }
    virtual const char *describe() const { return "pace"; }

private:
    double until_;
};

static void boost_cycles() {
    // WC.EXE's heaviest frames (Gimle 1: Raptors, Jalthi and the Exeter) are
    // 400 to 660 thousand cycles of work, and a frame at 20 a second has
    // 50 ms: 16000 leaves room, and as much again for every frame a second
    // more (24000 at 30).  (WC2 holds its own 15 frames a second with GOG's
    // 8000 cycles in an ordinary furball, with little to spare: 12000.)
    int fps = pace_fps();
    int want = env_int("WCFLIGHTCYCLES", is_wc1() ? (fps > 20 ? 800 * fps : 16000) : (g_game != GAME_NONE ? 12000 : 0));
    if (g_boostCycles && g_boostCycles != want && CPU_CycleMax == g_boostCycles && want > g_baseCycles) {
        // (A client boosted before the server's frame rate reached it.)
        g_boostCycles = want;
        CPU_CycleMax = want;
        wclog(2, "in flight: %d cycles for %d frames a second", want, fps);
        return;
    }
    if (want <= 0 || CPU_CycleAutoAdjust || g_boostCycles || CPU_CycleMax >= want) {
        return;
    }
    g_baseCycles = CPU_CycleMax;
    g_boostCycles = want;
    CPU_CycleMax = want;
    wclog(2, "in flight: %d cycles (%d outside)", want, (int)g_baseCycles);
}

void pace_tick() {
    // No flight frame for a while (a cutscene, the autopilot's camera, the
    // barracks): those take their speed from the CPU's, so it gets its own back.
    if (g_boostCycles && PIC_FullIndex() - g_lastTop > 300.0) {
        if (CPU_CycleMax == g_boostCycles) {
            CPU_CycleMax = g_baseCycles;  // (changed by hand meanwhile: it is the player's)
            wclog(2, "out of flight: back to %d cycles", (int)g_baseCycles);
        }
        g_boostCycles = 0;
    }
}

bool pace_frame(bool wait) {
    double now = PIC_FullIndex();
    int fps = pace_fps();
    if (!wait) {
        g_frameDue = -1;
    } else if (fps > 0) {
        double period = 1000.0 / fps;
        if (g_frameDue < 0 || now - g_frameDue > period || now < g_lastTop) {
            g_frameDue = now;              // the first frame, or one that is late by more than a frame
        } else if (now < g_frameDue) {
            g_paced += g_frameDue - now;
            g_trampoline.enqueue_front(new PaceJob(g_frameDue));
            return true;
        }
        g_frameDue += period;              // from when it was due, not from now: the rate holds on average
    }
    g_lastTop = now;
    boost_cycles();
    return false;
}

// ---------------------------------------------------------------------------
// Measuring

void perf_frame() {
    static double interval = -1;
    static bool log = false;
    if (interval < 0) {
        const char *env = getenv("WCNET_PERF");
        log = env && env[0];
        interval = 1000.0 * (log && atof(env) > 0 ? atof(env) : 2.0);
    }
    double emu = PIC_FullIndex();
    if (g_emuStart < 0 || emu - g_lastFrameEmu > 3000.0 || emu < g_lastFrameEmu) {
        // The first frame of a flight (or the first after a cutscene).
        g_emuStart = emu;
        g_realStart = perf_now_ms();
        g_frames = 0;
        g_wait = 0;
        g_worst = 0;
        g_paced = 0;
        g_ageSum = g_ageWorst = 0;
        g_ages = 0;
        g_lastFrameEmu = emu;
        return;
    }
    if (emu - g_lastFrameEmu > g_worst) {
        g_worst = emu - g_lastFrameEmu;
    }
    g_lastFrameEmu = emu;
    g_frames++;
    if (emu - g_emuStart < interval) {
        return;
    }
    double real = perf_now_ms();
    g_last.valid = true;
    g_last.fps = 1000.0 * g_frames / (emu - g_emuStart);
    g_last.speed = real > g_realStart ? (emu - g_emuStart) / (real - g_realStart) : 1.0;
    g_last.waitMs = g_wait / g_frames;
    g_last.worstFrameMs = g_worst;
    // (Only where frames are paced: a game that waits by itself, as WC2
    // does, is busy all the time as far as the CPU can tell.)
    g_last.load = pace_fps() > 0 ? 1.0 - g_paced / (emu - g_emuStart) : -1.0;
    g_last.cycles = (int)CPU_CycleMax;
    g_last.posAgeMs = g_ages ? g_ageSum / g_ages : -1;
    g_last.posAgeWorstMs = g_ages ? g_ageWorst : -1;
    g_last.ships = 0;
    g_last.others = 0;
    for (int slot = 0; slot < kNumSlots; slot++) {
        if (slot_in_use(slot)) {
            (slot <= kMaxShipSlot ? g_last.ships : g_last.others)++;
        }
    }
    if (log) {
        char age[64] = "";
        if (g_ages) {
            snprintf(age, sizeof(age), ", positions %.0f ms old (worst %.0f)", g_last.posAgeMs, g_last.posAgeWorstMs);
        }
        wclog(0, "perf: %.1f fps, worst frame %.0f ms, load %.0f%%, emulator at %.0f%% of real time, "
                 "%.1f ms/frame waiting for the network, %d cycles, %d ships, %d other entities%s",
              g_last.fps, g_last.worstFrameMs, g_last.load < 0 ? 100.0 : 100.0 * g_last.load, 100.0 * g_last.speed, g_last.waitMs,
              g_last.cycles, g_last.ships, g_last.others, age);
    }
    g_emuStart = emu;
    g_realStart = real;
    g_frames = 0;
    g_wait = 0;
    g_worst = 0;
    g_paced = 0;
    g_ageSum = g_ageWorst = 0;
    g_ages = 0;
}

}  // namespace wc
