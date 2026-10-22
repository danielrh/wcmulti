/*
 *  How the flight loop is doing, measured: frames per emulated second (what
 *  the game's own speed depends on), how fast the emulator runs against the
 *  wall clock (below 1: the host cannot keep up with the cycles asked for)
 *  and how long a frame waits for the other machines.  WCNET_PERF=<seconds>
 *  logs a line per interval; the page shows the same figures.
 */
#ifndef WCNET_PERF_H_
#define WCNET_PERF_H_

namespace wc {

struct PerfSample {
    bool valid;
    double fps;           // game frames per emulated second
    double speed;         // emulated time / real time over the interval
    double waitMs;        // ms per frame spent waiting for the other machines (blocked or idle)
    double worstFrameMs;  // the longest frame of the interval, emulated ms
    double load;          // share of the emulated time the game was working, not waiting for its frame's turn
    int cycles;           // emulated instructions per ms asked for
    int ships;            // ship slots in use
    int others;           // every other entity (bolts, missiles, rocks, debris)
    double posAgeMs;      // how old the other machine's positions were when a frame began, on average (-1: none)
    double posAgeWorstMs; // and at worst
};

// Pacing.  Wing Commander 1 does everything per frame and never waits: at a
// fixed CPU speed an empty sky runs at twice the speed of a furball.  (WC2
// waits for 1/15 s a frame by itself.)  So in flight the emulated CPU is
// given more cycles than the heaviest frame needs, and a frame that is done
// early waits, in emulated time, for its turn: the game runs at one speed.
//
//   WCFPS=<n>           frames per second (default 30 for WC.EXE; 0: no pacing)
//   WCFLIGHTCYCLES=<n>  cycles in flight (default 800 per frame a second for WC.EXE, at
//                       least 16000; 12000 for WC2; 0: leave alone)
//
// pace_frame() is called at the top of a new flight frame.  True: the frame
// is early and a wait was started on the trampoline; the caller returns and
// is back at the same place when it is over.  A client does not wait
// (wait = false): it has the server's frames to wait for, and those come at
// the server's pace.
bool pace_frame(bool wait);
void pace_tick();              // now and then: out of flight the CPU speed goes back
int pace_fps();
void set_pace_fps(int fps);    // a client takes the server's

void perf_frame();            // at the top of every flight frame
void perf_wait(double ms);    // real time spent blocked on the network
void perf_idle_wait(double emulatedMs);  // emulated time spent idle, waiting for the network
void perf_position_age(double ms);       // at a frame top: how old the other machine's last positions are
const PerfSample &perf_last();
double perf_now_ms();         // the host's clock

}  // namespace wc

#endif
