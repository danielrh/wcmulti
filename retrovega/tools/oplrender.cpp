// oplrender: our OPL3 music played through DOSBox's own OPL3 (dbopl), to a WAV.
//
//   oplrender --bank RVOPL.BNK --mid SONG.MID --out SONG.WAV [--seconds S] [--loops N]
//       the song through src/opl.c, the driver RV.EXE runs, on the same 240 Hz
//       clock as the game's music engine (src/music.c), looped N times (default 1): from the song's loop
//       marker (a "loop" marker meta event) when it has one, as the engine does, the intro played once
//   oplrender --log OPLLOG.BIN --out RUN.WAV
//       the register writes the game made (RV.EXE /midilog), as they came
//
// It says what it found: the loudest sample, the level, how many notes had to
// take a sounding voice (steals) and the most voices sounding at once.
// Built by tools/oplrender.sh.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "dosbox.h"
#include "../../src/hardware/dbopl.h"

extern "C" {
#include "../src/opl.h"
}

// What dbopl links against in DOSBox and does not use here.
void MixerChannel::AddSamples_m32(Bitu, const Bit32s *) {}
void MixerChannel::AddSamples_s32(Bitu, const Bit32s *) {}
void GFX_ShowMsg(char const *format, ...) { (void)format; }

static const int RATE = 49716;      // the chip's own
static const int TICK_HZ = 240;
static DBOPL::Handler g_handler;         // Init builds the chip's tables
static DBOPL::Chip &g_chip = g_handler.chip;
static std::vector<short> g_out;    // stereo
static double g_peak = 0, g_sum2 = 0;

extern "C" void opl_write(unsigned reg, unsigned char val) { g_chip.WriteReg(reg, val); }

static void render(long samples)
{
    static Bit32s buf[512 * 2];
    while (samples > 0) {
        long n = samples > 512 ? 512 : samples;
        g_chip.GenerateBlock3(n, buf);
        for (long i = 0; i < n * 2; i++) {
            double v = buf[i];
            g_peak = std::max(g_peak, std::fabs(v));
            g_sum2 += v * v;
            g_out.push_back((short)std::max(-32768.0, std::min(32767.0, v)));
        }
        samples -= n;
    }
}

static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    unsigned data = g_out.size() * 2;
    auto u32 = [&](unsigned v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](unsigned short v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(RATE); u32(RATE * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(data);
    fwrite(g_out.data(), 2, g_out.size(), f);
    fclose(f);
}

static std::vector<unsigned char> slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    std::vector<unsigned char> d;
    unsigned char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
    fclose(f);
    return d;
}

struct Ev { double us; unsigned char st, d1, d2; };

// a Standard MIDI File's channel events, merged, in microseconds
static std::vector<Ev> read_mid(const char *path, double *beat_us, int *beats_bar, double *loop_us)
{
    auto d = slurp(path);
    auto be32 = [&](size_t p) { return (unsigned)d[p] << 24 | d[p + 1] << 16 | d[p + 2] << 8 | d[p + 3]; };
    auto be16 = [&](size_t p) { return (unsigned)d[p] << 8 | d[p + 1]; };
    if (d.size() < 14 || memcmp(d.data(), "MThd", 4)) { fprintf(stderr, "%s: not a MIDI file\n", path); exit(1); }
    unsigned ntrk = be16(10), division = be16(12);
    struct Raw { unsigned long tick; int order; int meta; unsigned long us; unsigned char st, d1, d2; };   // meta 1 tempo, 2 loop
    std::vector<Raw> raw;
    *beats_bar = 4;
    size_t p = 8 + be32(4);
    int order = 0;
    for (unsigned t = 0; t < ntrk && p + 8 <= d.size(); t++) {
        size_t end = p + 8 + be32(p + 4);
        p += 8;
        unsigned long tick = 0;
        unsigned char running = 0;
        while (p < end) {
            unsigned long v = 0;
            unsigned char c;
            do { c = d[p++]; v = v << 7 | (c & 0x7F); } while (c & 0x80);
            tick += v;
            unsigned char st = d[p];
            if (st & 0x80) p++; else st = running;
            if (st == 0xFF) {
                unsigned char type = d[p++];
                unsigned long len = 0;
                do { c = d[p++]; len = len << 7 | (c & 0x7F); } while (c & 0x80);
                if (type == 0x51 && len == 3) raw.push_back({tick, order++, 1, (unsigned long)d[p] << 16 | d[p + 1] << 8 | d[p + 2], 0, 0, 0});
                if (type == 0x06 && len == 4 && !memcmp(&d[p], "loop", 4)) raw.push_back({tick, order++, 2, 0, 0, 0, 0});
                if (type == 0x58) *beats_bar = d[p];
                p += len;
                continue;
            }
            if (st == 0xF0 || st == 0xF7) {
                unsigned long len = 0;
                do { c = d[p++]; len = len << 7 | (c & 0x7F); } while (c & 0x80);
                p += len;
                continue;
            }
            running = st;
            unsigned char d1 = d[p++], d2 = 0;
            if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0) d2 = d[p++];
            raw.push_back({tick, order++, 0, 0, st, d1, d2});
        }
        p = end;
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Raw &a, const Raw &b) { return a.tick < b.tick; });
    std::vector<Ev> out;
    double tempo = 500000, us = 0;
    unsigned long last = 0;
    *beat_us = 0;
    *loop_us = 0;
    for (auto &r : raw) {
        us += (double)(r.tick - last) * tempo / division;
        last = r.tick;
        if (r.meta == 1) { tempo = r.us; if (!*beat_us) *beat_us = tempo; continue; }
        if (r.meta == 2) { *loop_us = us; continue; }
        out.push_back({us, r.st, r.d1, r.d2});
    }
    if (!*beat_us) *beat_us = tempo;
    return out;
}

int main(int argc, char **argv)
{
    const char *bank = 0, *mid = 0, *log = 0, *out = 0;
    double seconds = 0;
    int loops = 1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--bank" && i + 1 < argc) bank = argv[++i];
        else if (a == "--mid" && i + 1 < argc) mid = argv[++i];
        else if (a == "--log" && i + 1 < argc) log = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--seconds" && i + 1 < argc) seconds = atof(argv[++i]);
        else if (a == "--loops" && i + 1 < argc) loops = atoi(argv[++i]);
        else { fprintf(stderr, "usage: see the top of tools/oplrender.cpp\n"); return 2; }
    }
    if (!out || (!mid && !log) || (mid && !bank)) { fprintf(stderr, "usage: see the top of tools/oplrender.cpp\n"); return 2; }
    g_handler.Init(RATE);
    double per_tick = (double)RATE / TICK_HZ, owed = 0;
    if (log) {
        auto d = slurp(log);
        unsigned long now = 0;
        bool first = true;
        for (size_t p = 0; p + 7 <= d.size(); p += 7) {
            unsigned long t = d[p] | d[p + 1] << 8 | d[p + 2] << 16 | (unsigned long)d[p + 3] << 24;
            unsigned reg = d[p + 4] | d[p + 5] << 8;
            if (first) { now = t; first = false; }
            while (now < t) { owed += per_tick; long n = (long)owed; owed -= n; render(n); now++; }
            g_chip.WriteReg(reg, d[p + 6]);
        }
        render(RATE);
    } else {
        auto b = slurp(bank);
        static std::vector<unsigned char> keep = b;
        if (!opl_bank(keep.data(), keep.size())) { fprintf(stderr, "%s: not a bank\n", bank); return 1; }
        opl_init();
        double beat_us, mark_us;
        int beats_bar;
        auto ev = read_mid(mid, &beat_us, &beats_bar, &mark_us);
        // the engine's ticks: each event at its own tick, the loop padded to a whole bar
        double end_us = ev.empty() ? 0 : ev.back().us;
        double bar_us = beat_us * beats_bar;
        double loop_us = std::ceil(end_us / bar_us - 1e-6) * bar_us;
        long loop_ticks = (long)std::llround(loop_us * TICK_HZ / 1e6);
        long mark_ticks = (long)std::llround(mark_us * TICK_HZ / 1e6);
        size_t mark_at = 0;
        while (mark_at < ev.size() && ev[mark_at].us < mark_us - 1e-3) mark_at++;
        long total = seconds > 0 ? (long)(seconds * TICK_HZ) : loop_ticks + (loop_ticks - mark_ticks) * (loops - 1);
        size_t at = 0;
        long base = 0;
        for (long tick = 0; tick < total; tick++) {
            if (tick - base >= loop_ticks) { base = tick - mark_ticks; at = mark_at; }   // (round again from the marker)
            opl_tick();
            while (at < ev.size() && (long)std::llround(ev[at].us * TICK_HZ / 1e6) <= tick - base) {
                opl_event(ev[at].st, ev[at].d1, ev[at].d2);
                at++;
            }
            owed += per_tick;
            long n = (long)owed;
            owed -= n;
            render(n);
        }
        opl_silence();
        render(RATE / 2);
        printf("%s: %.1f s a loop, %zu events, %lu steals, at most %d voices\n", mid, loop_us / 1e6, ev.size(),
               opl_steals, opl_peak);
        printf("%s: loops from %.3f s\n", mid, mark_us / 1e6);
    }
    write_wav(out);
    double rms = std::sqrt(g_sum2 / std::max<size_t>(1, g_out.size()));
    printf("%s: %.1f s, peak %.0f (%.1f dBFS%s), rms %.1f dBFS\n", out, g_out.size() / 2.0 / RATE, g_peak,
           20 * std::log10(std::max(1.0, g_peak) / 32768), g_peak > 32767 ? ", CLIPPED" : "",
           20 * std::log10(std::max(1e-9, rms) / 32768));
    return 0;
}
