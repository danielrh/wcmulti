// opl3.cpp: the OPL3 the native build's music plays on: ymfm's YMF262 (host/ymfm, BSD 3-Clause; the dev tools keep
// DOSBox's dbopl).  The game writes its registers from the timer's interrupt (music.c), which the host delivers in
// bursts (hle.c, at the game's polls); each write comes stamped with when its interrupt was due, and the chip hears
// it at that time on the audio's clock, which runs OPL3_LAG_US behind the host's: the notes keep the music clock's
// time to the sample, whatever the bursts.
#include <atomic>
#include <cstdint>
#include <cstring>
#include "ymfm/ymfm_opl.h"

extern "C" {
#include "opl3.h"
}

namespace {

struct Bus : ymfm::ymfm_interface {};   // (no timers, no IRQ: hle.c fakes the status the detection reads)

struct Write { uint64_t at; uint16_t reg; uint8_t val; };
constexpr uint32_t QN = 1u << 15;
Write g_q[QN];
std::atomic<uint32_t> g_qw{0}, g_qr{0};   // one writer (the game), one reader (the audio)

Bus g_bus;
ymfm::ymf262 *g_chip;
double g_rate;                          // the chip's own: its clock / 288
double g_us_per_sample;
double g_t;                             // the audio's clock: the time of the next sample made, host microseconds
bool g_started;
uint32_t g_lag_us;
unsigned long g_late, g_resyncs;

void apply(const Write &w)
{
    g_chip->write((w.reg & 0x100) ? 2 : 0, (uint8_t)w.reg);
    g_chip->write((w.reg & 0x100) ? 3 : 1, w.val);
}

}

extern "C" double opl3_init(uint32_t lag_us)
{
    g_chip = new ymfm::ymf262(g_bus);
    g_chip->reset();
    g_rate = g_chip->sample_rate(14318181);
    g_us_per_sample = 1e6 / g_rate;
    g_lag_us = lag_us;
    return g_rate;
}

extern "C" void opl3_write(uint64_t at_us, uint16_t reg, uint8_t val)
{
    uint32_t w = g_qw.load(std::memory_order_relaxed);
    if (w - g_qr.load(std::memory_order_acquire) >= QN)
        return;                         // (full: the audio has stopped taking them)
    g_q[w % QN] = Write{at_us, reg, val};
    g_qw.store(w + 1, std::memory_order_release);
}

// n stereo frames of the chip's sound (at its own rate), as of the host's clock now_us
extern "C" void opl3_render(int16_t *out, int n, uint64_t now_us)
{
    double want = (double)now_us - g_lag_us;
    if (!g_started || g_t < want - 4 * g_lag_us || g_t > want + 4 * g_lag_us) {
        g_resyncs += g_started;         // (the audio stalled, or the host's clock jumped: start again from now)
        g_t = want;
        g_started = true;
    }
    uint32_t r = g_qr.load(std::memory_order_relaxed), w = g_qw.load(std::memory_order_acquire);
    ymfm::ymf262::output_data o;
    for (int i = 0; i < n; i++) {
        while (r != w && (double)g_q[r % QN].at <= g_t) {
            if ((double)g_q[r % QN].at < g_t - 2 * g_us_per_sample - g_lag_us)
                g_late++;
            apply(g_q[r % QN]);
            r++;
        }
        g_chip->generate(&o);
        for (int c = 0; c < 2; c++) {
            int32_t v = o.data[c];
            out[2 * i + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        g_t += g_us_per_sample;
    }
    g_qr.store(r, std::memory_order_release);
}

extern "C" unsigned long opl3_resyncs(void) { return g_resyncs; }
