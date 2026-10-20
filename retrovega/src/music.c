/* music.c: interactive music (docs/engine.md).
 *
 * Songs are Standard MIDI Files (type 0 or 1), read at start-up and flattened
 * into one event stream each, timed in ticks of the music clock: the PIT
 * reprogrammed to TICK_HZ, the BIOS's 18.2 Hz clock still chained so ticks()
 * keeps counting.  The interrupt plays the stream and loops it, on the Sound
 * Blaster's OPL3 (opl.c, with our bank RVOPL.BNK) or, asked (/mpu) or with
 * no OPL3 found, to an MPU-401 (UART mode, port 330h) for a General MIDI
 * synthesizer.
 *
 * The master fade turns the OPL3's voices down (opl_fade), or on the MPU-401
 * scales every channel's volume (controller 7, the song's own value times
 * the fade).  music_cue(cue, fade_ms) fades the playing song out over
 * fade_ms, waiting first for the next beat or bar if asked (music_cue_at),
 * then silences every channel, starts the cue's song and fades it in over
 * half that: 150-300 ms gives the quick cuts between rooms.  The engine
 * keeps playing through every load: it is resident. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include <conio.h>
#include "rv.h"
#include "rvdata.h"
#include "music.h"
#include "opl.h"

#define TICK_HZ 240U
#define PIT_HZ 1193182UL
#define DIVISOR ((unsigned)(PIT_HZ / TICK_HZ))
#define MAX_SONGS 8
#define MAX_EVENTS 5000U               /* a song's events, while it is read (12 bytes each) */
#define MPU_DATA 0x330
#define MPU_STAT 0x331
#define OPL_PORT 0x388
enum { DEV_NONE, DEV_OPL3, DEV_MPU };

typedef struct {
    unsigned short delta;               /* music ticks after the previous event */
    unsigned char status, d1, d2, flags;
} Ev;
#define EV_LAST 1                       /* the song's last event: it starts again after it */

typedef struct {
    XmsBlock x;                         /* the events, in XMS (resident) */
    unsigned n;
    unsigned beat;                      /* music ticks a beat, from the first tempo */
    unsigned char beats_bar;
} Song;

/* The interrupt plays from a ring in conventional memory, which music_poll
 * keeps topped up from XMS (a ring holds some 40 s of the busiest song, so a
 * long load costs nothing); the next song's ring is filled by music_cue,
 * before the interrupt switches to it. */
#define RING 1024U
#define REFILL 128U
typedef struct {
    Ev __far *ev;
    volatile unsigned rd, wr;           /* events taken by the interrupt; put by music_poll and music_cue */
    int song;                           /* -1: none */
    unsigned src;                       /* the song's next event to put */
} Ring;
static Ring g_ring[2];
static volatile int g_play, g_next_ring;
static volatile unsigned long g_starved;

static const char *g_files[] = { "BRIEF.MID", "BAR.MID", "LAUNCH.MID", "WIN.MID", "LOSE.MID", "QUARTERS.MID" };
static Song g_song[MAX_SONGS];
static int g_dev, g_want = MUSIC_AUTO;

/* the interrupt's state */
static void (__interrupt __far *g_old8)(void);
static volatile int g_cur = -1, g_next = -1, g_wait;         /* g_wait: 0 now, 1 beat, 2 bar */
static volatile unsigned g_left;
static volatile int g_have_pend;
static Ev g_pend;                       /* the next event, waiting g_left ticks */
static volatile unsigned long g_song_ticks;
static volatile int g_level = 0, g_target = 0, g_step = 1;   /* master fade, 0..1024 */
static volatile unsigned g_resend;
static unsigned char g_vol[16];
static unsigned g_chain;
static volatile unsigned long g_bytes;
static int g_installed;

/* tests (RV.EXE /midilog): every byte sent to the MPU-401 (MIDILOG.BIN: the
 * music clock's tick, u32, and the byte) or every OPL3 register written
 * (OPLLOG.BIN: the tick, the register u16, the value), little-endian.  The
 * interrupt puts them in a ring; music_poll writes them out. */
#define REC_MAX 4096U
static unsigned long __far *g_rec_tick;
static unsigned short __far *g_rec_reg;
static unsigned char __far *g_rec_val;
static volatile unsigned g_rec_head, g_rec_tail;
static unsigned long g_rec_lost;
static int g_recording;
static FILE *g_rec_file;
static volatile unsigned long g_clock;

void music_record(int on)
{
    if (on && !g_rec_tick) {
        g_rec_tick = (unsigned long __far *)_fmalloc(REC_MAX * 4UL);
        g_rec_reg = (unsigned short __far *)_fmalloc(REC_MAX * 2UL);
        g_rec_val = (unsigned char __far *)_fmalloc(REC_MAX);
    }
    g_recording = on && g_rec_tick && g_rec_reg && g_rec_val;
}

static void record(unsigned reg, unsigned char val)
{
    unsigned next = (g_rec_head + 1) % REC_MAX;
    if (next == g_rec_tail) {
        g_rec_lost++;
        return;
    }
    g_rec_tick[g_rec_head] = g_clock;
    g_rec_reg[g_rec_head] = (unsigned short)reg;
    g_rec_val[g_rec_head] = val;
    g_rec_head = next;
}

/* tops up a ring from its song in XMS, round to the start again at the end */
static void fill(Ring *r)
{
    const Song *s;
    if (r->song < 0)
        return;
    s = &g_song[r->song];
    while (RING - (unsigned)(r->wr - r->rd) >= REFILL) {
        unsigned at = r->wr % RING, n = REFILL;
        if (n > RING - at)
            n = RING - at;
        if (n > s->n - r->src)
            n = s->n - r->src;
        xms_get(s->x, (unsigned long)r->src * sizeof(Ev), r->ev + at, (unsigned long)n * sizeof(Ev));
        r->src = (r->src + n) % s->n;
        r->wr += n;                     /* (after the move: the interrupt may take them now) */
    }
}

void music_poll(void)
{
    if (g_dev != DEV_NONE) {
        fill(&g_ring[g_play]);
        if (g_next != -1)
            fill(&g_ring[g_next_ring]);
    }
    while (g_rec_file && g_rec_tail != g_rec_head) {
        unsigned i = g_rec_tail;
        unsigned long t = g_rec_tick[i];
        unsigned char v = g_rec_val[i];
        fwrite(&t, 4, 1, g_rec_file);
        if (g_dev == DEV_OPL3) {
            unsigned short r = g_rec_reg[i];
            fwrite(&r, 2, 1, g_rec_file);
        }
        fwrite(&v, 1, 1, g_rec_file);
        g_rec_tail = (i + 1) % REC_MAX;
    }
}

static void mpu_put(unsigned char b)
{
    unsigned n = 0;
    while ((inp(MPU_STAT) & 0x40) && ++n < 2000);
    outp(MPU_DATA, b);
    g_bytes++;
    if (g_recording)
        record(0, b);
}

/* opl.c's: a register of the OPL3 (the second bank at 38Ah) */
void opl_write(unsigned reg, unsigned char val)
{
    unsigned port = reg & 0x100 ? OPL_PORT + 2 : OPL_PORT;
    outp(port, (unsigned char)reg);
    inp(port); inp(port); inp(port); inp(port);
    outp(port + 1, val);
    inp(port); inp(port); inp(port); inp(port);
    g_bytes++;
    if (g_recording)
        record(reg, val);
}

/* 3 for an OPL3, 2 for an OPL2, 0 for none: the timer test, then the status bits */
static int opl_detect(void)
{
    unsigned char s1, s2;
    unsigned long t;
    int rec = g_recording;
    g_recording = 0;
    opl_write(4, 0x60);
    opl_write(4, 0x80);
    s1 = (unsigned char)inp(OPL_PORT);
    opl_write(2, 0xFF);
    opl_write(4, 0x21);
    t = ticks();
    while (ticks() - t < 2);            /* timer 1 overflows after 80 microseconds */
    s2 = (unsigned char)inp(OPL_PORT);
    opl_write(4, 0x60);
    opl_write(4, 0x80);
    g_recording = rec;
    if ((s1 & 0xE0) != 0 || (s2 & 0xE0) != 0xC0)
        return 0;
    return (s2 & 0x06) ? 2 : 3;
}

static int load_bank(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char __far *bank;
    static unsigned char buf[256];
    long k;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bank = size > 0 && size < 32000L ? (unsigned char __far *)_fmalloc((size_t)size) : 0;
    if (!bank) {
        fclose(f);
        return 0;
    }
    for (k = 0; k < size; k += sizeof buf) {
        unsigned n = (unsigned)(size - k < (long)sizeof buf ? size - k : sizeof buf);
        fread(buf, 1, n, f);
        _fmemcpy(bank + k, buf, n);
    }
    fclose(f);
    if (!opl_bank(bank, (unsigned long)size)) {
        _ffree(bank);
        return 0;
    }
    return 1;
}

static int mpu_init(void)
{
    unsigned n = 0;
    outp(MPU_STAT, 0xFF);                       /* reset */
    while ((inp(MPU_STAT) & 0x80) && ++n < 30000);
    inp(MPU_DATA);
    n = 0;
    while ((inp(MPU_STAT) & 0x40) && ++n < 30000);
    outp(MPU_STAT, 0x3F);                       /* UART mode */
    n = 0;
    while ((inp(MPU_STAT) & 0x80) && ++n < 30000);
    return inp(MPU_DATA) == 0xFE;
}

static void send_volume(int ch)
{
    mpu_put((unsigned char)(0xB0 | ch));
    mpu_put(7);
    mpu_put((unsigned char)((unsigned long)g_vol[ch] * (unsigned)g_level / 1024));   /* 32 bits: 127 x 1024 */
}

static void silence(void)
{
    int ch;
    if (g_dev == DEV_OPL3) {
        opl_silence();
        return;
    }
    for (ch = 0; ch < 16; ch++) {
        mpu_put((unsigned char)(0xB0 | ch)); mpu_put(123); mpu_put(0);    /* all notes off */
        mpu_put((unsigned char)(0xB0 | ch)); mpu_put(121); mpu_put(0);    /* reset controllers */
        g_vol[ch] = 100;
    }
}

static void start(int song, int ring)
{
    silence();
    g_cur = song;
    g_play = ring;
    g_song_ticks = 0;
    g_have_pend = 0;
}

static void play(const Ev *e)
{
    unsigned char st = e->status;
    if (g_dev == DEV_OPL3) {
        if (st >= 0x80)
            opl_event(st, e->d1, e->d2);
    } else if ((st & 0xF0) == 0xB0 && e->d1 == 7) {
        g_vol[st & 15] = e->d2;
        send_volume(st & 15);
    } else if (st >= 0x80) {
        mpu_put(st);
        mpu_put(e->d1);
        if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0)
            mpu_put(e->d2);
    }
}

static void __interrupt __far isr(void)
{
    g_clock++;
    if (g_dev == DEV_OPL3)
        opl_tick();
    if (g_dev != DEV_NONE) {
        /* the fade, and the switch to the next song when it reaches silence */
        if (g_level != g_target) {
            if (g_level < g_target) {
                g_level += g_step;
                if (g_level > g_target)
                    g_level = g_target;
            } else {
                g_level -= g_step;
                if (g_level < g_target)
                    g_level = g_target;
            }
            if (g_dev == DEV_OPL3) {
                opl_fade(g_level);
            } else if (++g_resend % 4 == 0 || g_level == g_target) {
                int ch;
                for (ch = 0; ch < 16; ch++)
                    send_volume(ch);
            }
        }
        if (g_next != -1 && g_level == 0) {
            start(g_next, g_next_ring);
            g_next = -1;
            g_target = 1024;
            g_step *= 2;                        /* in over half the time it took to go */
        }
        if (g_cur >= 0) {
            const Song *s = &g_song[g_cur];
            int beat_due = 0;
            g_song_ticks++;
            if (g_wait && g_next != -1) {
                unsigned long unit = (unsigned long)s->beat * (g_wait == 2 ? s->beats_bar : 1);
                beat_due = unit && g_song_ticks % unit == 0;
                if (beat_due) {
                    g_wait = 0;
                    g_target = 0;
                }
            }
            for (;;) {
                Ring *r = &g_ring[g_play];
                if (!g_have_pend) {
                    if (r->rd == r->wr) {
                        g_starved++;            /* music_poll has not kept up: the song waits */
                        break;
                    }
                    g_pend = r->ev[r->rd % RING];
                    r->rd++;
                    g_have_pend = 1;
                    g_left = g_pend.delta;
                }
                if (g_left) {
                    g_left--;
                    break;
                }
                g_have_pend = 0;
                play(&g_pend);
                if (g_pend.flags & EV_LAST)
                    g_song_ticks = 0;           /* round again */
            }
        }
    }
    g_chain += DIVISOR;
    if (g_chain < DIVISOR) {                    /* 65536 PIT counts gone by: the BIOS's tick */
        _chain_intr(g_old8);
    }
    outp(0x20, 0x20);
}

/* --- reading Standard MIDI Files --- */

static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }

static unsigned long varlen(const unsigned char __far **pp)
{
    unsigned long v = 0;
    unsigned char c;
    do {
        c = *(*pp)++;
        v = (v << 7) | (c & 0x7F);
    } while (c & 0x80);
    return v;
}

/* a file's event while it is read: a channel message, or (st 0xFF) a tempo in d1 d2 d3 */
typedef struct { unsigned long tick; unsigned char st, d1, d2, d3; } Raw;
#define MAX_TRACKS 24
static Raw __far *g_raw;                /* MAX_EVENTS, while music_init reads the songs */

/* the file's events, the tracks merged, in music ticks */
static int load_song(const char *path, Song *song)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[14];
    unsigned char __far *data;
    Raw __far *raw = g_raw;
    Ev __far *ev;
    unsigned nraw = 0, ntrk, division, t, nev = 0;
    int ok;
    unsigned first[MAX_TRACKS], last[MAX_TRACKS];
    long size;
    unsigned long tempo = 500000UL;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 22 || size > 60000L) {
        fclose(f);
        return 0;
    }
    data = (unsigned char __far *)_fmalloc((size_t)size);
    if (!data) {
        rv_log("music: no memory to read %s", path);
        fclose(f);
        return 0;
    }
    {
        long k;
        static unsigned char buf[256];
        for (k = 0; k < size; k += sizeof buf) {
            unsigned n = (unsigned)(size - k < (long)sizeof buf ? size - k : sizeof buf);
            fread(buf, 1, n, f);
            _fmemcpy(data + k, buf, n);
        }
    }
    fclose(f);
    _fmemcpy(hdr, data, 14);
    if (memcmp(hdr, "MThd", 4) != 0 || (be16(hdr + 12) & 0x8000)) {
        _ffree(data);
        return 0;
    }
    ntrk = be16(hdr + 10);
    division = be16(hdr + 12);
    song->beats_bar = 4;
    {
        const unsigned char __far *p = data + 8 + be32(hdr + 4);
        for (t = 0; t < ntrk && t < MAX_TRACKS && p + 8 <= data + size; t++) {
            unsigned char hd[8];
            const unsigned char __far *end;
            unsigned long tick = 0;
            unsigned char running = 0;
            _fmemcpy(hd, p, 8);
            if (memcmp(hd, "MTrk", 4) != 0)
                break;
            end = p + 8 + be32(hd + 4);
            p += 8;
            first[t] = nraw;
            while (p < end && nraw < MAX_EVENTS) {
                unsigned char st, d1 = 0, d2 = 0;
                tick += varlen(&p);
                st = *p;
                if (st & 0x80)
                    p++;
                else
                    st = running;
                if (st == 0xFF) {
                    unsigned char type = *p++;
                    unsigned long len = varlen(&p);
                    if (type == 0x51 && len == 3) {
                        raw[nraw].tick = tick;
                        raw[nraw].st = 0xFF;
                        raw[nraw].d1 = p[0];
                        raw[nraw].d2 = p[1];
                        raw[nraw].d3 = p[2];
                        nraw++;
                    } else if (type == 0x58 && len >= 2) {
                        song->beats_bar = p[0];
                    }
                    p += len;
                    continue;
                }
                if (st == 0xF0 || st == 0xF7) {
                    p += varlen(&p);
                    continue;
                }
                running = st;
                d1 = *p++;
                if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0)
                    d2 = *p++;
                raw[nraw].tick = tick;
                raw[nraw].st = st;
                raw[nraw].d1 = d1;
                raw[nraw].d2 = d2;
                nraw++;
                nev++;
            }
            last[t] = nraw;
            p = end;
        }
        ntrk = t;
    }
    _ffree(data);
    if (nraw >= MAX_EVENTS)
        rv_log("music: %s has more than %u events: cut short", path, MAX_EVENTS);
    ev = (Ev __far *)_fmalloc(sizeof(Ev) * (nev + 1));     /* until it is in XMS */
    if (!ev)
        return 0;
    /* the tracks merged (each is in order: take the earliest next event, the
     * first track's on a tie), to music ticks through the tempo map */
    {
        unsigned long last_tick = 0;
        double us = 0, last_us_out = 0;
        unsigned n = 0;
        song->beat = (unsigned)((double)tempo * TICK_HZ / 1e6 + 0.5);
        for (;;) {
            const Raw __far *r;
            int best = -1;
            for (t = 0; t < ntrk; t++)
                if (first[t] < last[t] && (best < 0 || raw[first[t]].tick < raw[first[best]].tick))
                    best = t;
            if (best < 0)
                break;
            r = &raw[first[best]++];
            us += (double)(r->tick - last_tick) * tempo / division;
            last_tick = r->tick;
            if (r->st == 0xFF) {
                tempo = ((unsigned long)r->d1 << 16) | ((unsigned)r->d2 << 8) | r->d3;
                if (r->tick == 0)
                    song->beat = (unsigned)((double)tempo * TICK_HZ / 1e6 + 0.5);
                continue;
            }
            ev[n].delta = (unsigned short)((us - last_us_out) * TICK_HZ / 1e6 + 0.5);
            last_us_out += (double)ev[n].delta * 1e6 / TICK_HZ;
            ev[n].status = r->st;
            ev[n].d1 = r->d1;
            ev[n].d2 = r->d2;
            ev[n].flags = 0;
            n++;
        }
        /* a rest to the end of the last bar, so the loop keeps time */
        {
            unsigned long bar = (unsigned long)song->beat * song->beats_bar;
            unsigned long total = (unsigned long)(last_us_out * TICK_HZ / 1e6 + 0.5);
            ev[n].delta = (unsigned short)(bar ? (bar - total % bar) % bar : 0);
            ev[n].status = 0;
            ev[n].flags = EV_LAST;
            n++;
        }
        song->n = n;
    }
    song->x = xms_alloc((unsigned long)song->n * sizeof(Ev), XMS_RESIDENT);
    ok = song->x && xms_put(song->x, 0, ev, (unsigned long)song->n * sizeof(Ev));
    _ffree(ev);
    if (!ok)
        song->n = 0;
    return ok;
}

void music_device(int want)
{
    g_want = want;
}

int music_init(void)
{
    int i, loaded = 0, opl = 0;
    if (g_want == MUSIC_AUTO || g_want == MUSIC_OPL3) {
        opl = opl_detect();
        if (opl == 3 && load_bank("RVOPL.BNK")) {
            g_dev = DEV_OPL3;
            opl_init();
        }
        rv_log("music: %s", g_dev ? "OPL3 at 388h, our bank" : opl == 3 ? "OPL3 at 388h, but no RVOPL.BNK"
               : opl == 2 ? "an OPL2 only (the songs need an OPL3)" : "no OPL3 at 388h");
    }
    if (!g_dev && (g_want == MUSIC_AUTO || g_want == MUSIC_MPU401)) {
        g_dev = mpu_init() ? DEV_MPU : DEV_NONE;
        rv_log("music: MPU-401 %s", g_dev ? "in UART mode" : "not answering (silent)");
    }
    if (g_recording) {
        g_rec_file = fopen(g_dev == DEV_OPL3 ? "OPLLOG.BIN" : "MIDILOG.BIN", "wb");
        g_recording = g_rec_file != 0;
    }
    for (i = 0; i < 2; i++) {
        g_ring[i].ev = (Ev __far *)_fmalloc(sizeof(Ev) * RING);
        g_ring[i].song = -1;
        if (!g_ring[i].ev) {
            rv_log("music: no memory for its rings");
            g_dev = DEV_NONE;
        }
    }
    g_raw = (Raw __far *)_fmalloc(sizeof(Raw) * MAX_EVENTS);   /* only while reading */
    for (i = 0; g_raw && i < (int)RV_COUNT(g_files) && i < MAX_SONGS; i++)
        if (load_song(g_files[i], &g_song[i])) {
            loaded++;
            rv_log("music: %s, %u events, beat %u ticks, %u to the bar", g_files[i], g_song[i].n, g_song[i].beat,
                   g_song[i].beats_bar);
        }
    if (g_raw)
        _ffree(g_raw);
    g_raw = 0;
    g_old8 = _dos_getvect(8);
    atexit(music_shutdown);             /* a stop (rv_fail) gives the timer back too */
    g_installed = 1;
    _disable();
    _dos_setvect(8, isr);
    outp(0x43, 0x36);
    outp(0x40, DIVISOR & 0xFF);
    outp(0x40, DIVISOR >> 8);
    _enable();
    return loaded;
}

void music_shutdown(void)
{
    if (!g_installed)
        return;
    g_installed = 0;
    _disable();
    outp(0x43, 0x36);
    outp(0x40, 0);
    outp(0x40, 0);
    _dos_setvect(8, g_old8);
    _enable();
    if (g_dev != DEV_NONE)
        silence();
    if (g_dev == DEV_OPL3)
        rv_log("music: %lu OPL3 register writes, %lu notes took a sounding voice, at most %d voices", g_bytes,
               opl_steals, opl_peak);
    else
        rv_log("music: %lu bytes sent to the MPU-401", g_bytes);
    if (g_rec_file) {
        music_poll();
        fclose(g_rec_file);
        g_rec_file = 0;
        if (g_rec_lost)
            rv_log("music: the log lost %lu entries (music_poll too seldom)", g_rec_lost);
    }
    if (g_starved) {
        rv_log("music: %lu ticks waited for music_poll", g_starved);
    }
}

void music_cue_at(int cue, int fade_ms, int at)
{
    static int last = -2;
    int song = cue >= 0 && cue < MAX_SONGS && g_song[cue].n && g_dev != DEV_NONE ? cue : -1;
    int step = fade_ms > 0 ? (int)(1024L * 1000 / ((long)fade_ms * TICK_HZ)) : 1024;
    if (cue == last)
        return;
    last = cue;
    rv_log("music: cue %d (%s), fade %d ms%s", cue, song >= 0 ? g_files[cue] : "none", fade_ms,
           at == MUSIC_BAR ? " on the bar" : at == MUSIC_BEAT ? " on the beat" : "");
    _disable();
    g_next = -1;                        /* no switch to a ring while it is filled */
    _enable();
    if (song >= 0 && g_dev != DEV_NONE) {
        Ring *r = &g_ring[1 - g_play];
        r->song = song;
        r->src = 0;
        r->rd = r->wr = 0;
        fill(r);
    }
    _disable();
    g_step = step < 1 ? 1 : step;
    g_next_ring = 1 - g_play;
    g_next = song;
    g_wait = g_cur >= 0 ? at : 0;
    if (!g_wait)
        g_target = 0;
    _enable();
}

void music_cue(int cue, int fade_ms) { music_cue_at(cue, fade_ms, MUSIC_NOW); }

unsigned long clock240(void)
{
    unsigned long t;
    _disable();                         /* two words: not torn by the interrupt */
    t = g_clock;
    _enable();
    return t;
}
