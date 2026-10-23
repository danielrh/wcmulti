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
 * keeps playing through every load: it is resident.
 *
 * A sting (the flight's victories and its tragedy, RV_MUSIC_STINGS on) is
 * played once over whatever plays: music_sting cuts the song short, notes
 * where it was, plays the sting and then goes on with the song from there,
 * its channels' programs, volumes, pans and expressions as they stood
 * played first (the chase).  The cues' files are rvdata.h's RV_MUSIC_FILES. */
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
#define MAX_SONGS RV_MUSIC_COUNT
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

static const char __far g_files[MAX_SONGS][13] = { RV_MUSIC_FILES };
static Song __far g_song[MAX_SONGS];    /* (far, all of it: DGROUP is full) */
static volatile unsigned __far g_loops;         /* times the playing song came round (music_loops) */
static volatile unsigned char __far g_sting_over;   /* a sting played to its end: music_poll goes back */
static int __far g_back = -1;           /* the song a sting cut short, */
static unsigned __far g_back_at;        /* ... the event it was at */
static unsigned long __far g_start_ticks;   /* where in its bar the next song starts (a chase's) */
static int __far g_last = -2;           /* the last cue asked for: asked again, it is left alone */
static void cue_song(int song, int fade_ms, int at, unsigned from);
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
#define REC_MAX 1024U                   /* (7 KB: written out every frame, some 14 entries a frame) */
static unsigned long __far *g_rec_tick;
static unsigned short __far *g_rec_reg;
static unsigned char __far *g_rec_val;
static volatile unsigned g_rec_head, g_rec_tail;
static unsigned long g_rec_lost;
static int g_recording;
static FILE *g_rec_file;
static volatile unsigned long g_clock;

static unsigned char __far g_want_rec;
static void record(unsigned reg, unsigned char val);

void music_record(int on)
{
    g_want_rec = (unsigned char)on;
}

/* the log begins at the first music_poll, once the game runs: its buffers taken at start-up from the far heap
 * left the fonts no room (the heap grew round them), and upper memory is the ships' dense buffers' to the byte.
 * So the chip's set-up is written into it first, as opl_init wrote it, and every voice's patch again at its
 * next note. */
static void record_start(void)
{
    g_want_rec = 0;
    g_rec_tick = (unsigned long __far *)_fmalloc(REC_MAX * 4UL);
    g_rec_reg = (unsigned short __far *)_fmalloc(REC_MAX * 2UL);
    g_rec_val = (unsigned char __far *)_fmalloc(REC_MAX);
    if (!g_rec_tick || !g_rec_reg || !g_rec_val
        || !(g_rec_file = fopen(g_dev == DEV_OPL3 ? "OPLLOG.BIN" : "MIDILOG.BIN", "wb"))) {
        rv_log("music: no room for the log");
        return;
    }
    _disable();
    g_recording = 1;
    if (g_dev == DEV_OPL3) {
        record(0x105, 1);
        record(0x104, 0);
        record(0x001, 0x20);
        record(0x0BD, 0);
        opl_uncache();
    }
    _enable();
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
    const Song __far *s;
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
    if (g_want_rec && g_dev != DEV_NONE)
        record_start();
    if (g_sting_over) {
        g_sting_over = 0;
        if (g_back >= 0 && g_next == -1) {      /* the sting over: the song it cut short goes on */
            int back = g_back;
            g_back = -1;
            g_last = back;
            cue_song(back, 600, MUSIC_NOW, g_back_at);
        }
    }
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
    g_song_ticks = g_start_ticks;
    g_loops = 0;
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
            const Song __far *s = &g_song[g_cur];
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
                if (g_pend.flags & EV_LAST) {
                    g_song_ticks = 0;           /* round again */
                    g_loops++;
                    if (g_cur >= RV_MUSIC_STINGS) {
                        g_cur = -1;             /* a sting: once, then quiet until music_poll goes back */
                        g_sting_over = 1;
                        break;
                    }
                }
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

/* Reading a song: every track in a block of its own (up to 64 KB each, so
 * a song may be as long as its tracks allow), the tracks merged as they
 * are read (each is in order: the earliest next event goes first, the
 * first track's on a tie), timed through the tempo map in music ticks, and
 * written to XMS a chunk at a time.  Twice: once to count, once to write. */
#define MAX_TRACKS 24
#define CHUNK_EV 256U
static Ev __far *g_chunk;               /* CHUNK_EV events, while music_init reads the songs */

typedef struct {
    const unsigned char __far *p, __far *end;
    unsigned long tick;                 /* the next event's */
    unsigned char running;
    unsigned char st, d1, d2, d3;       /* the next event: a channel message, or (st 0xFF) a tempo in d1 d2 d3 */
    int live;
} Cursor;

/* the track's next channel message or tempo (the rest skipped); 0 at its end */
static int advance(Cursor *c, unsigned char *beats_bar)
{
    while (c->p < c->end) {
        unsigned char st;
        c->tick += varlen(&c->p);
        st = *c->p;
        if (st & 0x80)
            c->p++;
        else
            st = c->running;
        if (st == 0xFF) {
            unsigned char type = *c->p++;
            unsigned long len = varlen(&c->p);
            if (type == 0x51 && len == 3) {
                c->st = 0xFF;
                c->d1 = c->p[0];
                c->d2 = c->p[1];
                c->d3 = c->p[2];
                c->p += len;
                return 1;
            }
            if (type == 0x58 && len >= 2)
                *beats_bar = c->p[0];
            c->p += len;
            continue;
        }
        if (st == 0xF0 || st == 0xF7) {
            c->p += varlen(&c->p);
            continue;
        }
        c->running = st;
        c->st = st;
        c->d1 = *c->p++;
        c->d2 = (st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0 ? *c->p++ : 0;
        return 1;
    }
    return 0;
}

static int load_song(const char *path, Song *song)
{
    static unsigned char buf[256];
    unsigned char hdr[14], hd[8];
    unsigned char __far *trk[MAX_TRACKS];
    unsigned long len[MAX_TRACKS];
    Cursor cur[MAX_TRACKS];
    unsigned ntrk = 0, t, division, pass;
    unsigned long nev = 0;
    int ok = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    if (fread(hdr, 1, 14, f) != 14 || memcmp(hdr, "MThd", 4) != 0 || (be16(hdr + 12) & 0x8000)) {
        fclose(f);
        return 0;
    }
    division = be16(hdr + 12);
    fseek(f, 8 + be32(hdr + 4), SEEK_SET);
    while (ntrk < MAX_TRACKS && ntrk < be16(hdr + 10) && fread(hd, 1, 8, f) == 8 && !memcmp(hd, "MTrk", 4)) {
        unsigned long k, n = be32(hd + 4);
        if (n > 65000UL || !(trk[ntrk] = (unsigned char __far *)_fmalloc((size_t)n + 1))) {
            rv_log("music: %s: a track of %lu bytes does not fit", path, n);
            break;
        }
        for (k = 0; k < n; k += sizeof buf) {
            unsigned m = (unsigned)(n - k < sizeof buf ? n - k : sizeof buf);
            fread(buf, 1, m, f);
            _fmemcpy(trk[ntrk] + k, buf, m);
        }
        len[ntrk++] = n;
    }
    fclose(f);
    song->beats_bar = 4;
    for (pass = 0; pass < 2; pass++) {
        unsigned long tempo = 500000UL, last_tick = 0, n = 0;
        double us = 0, last_us_out = 0;
        song->beat = (unsigned)((double)tempo * TICK_HZ / 1e6 + 0.5);
        for (t = 0; t < ntrk; t++) {
            cur[t].p = trk[t];
            cur[t].end = trk[t] + len[t];
            cur[t].tick = 0;
            cur[t].running = 0;
            cur[t].live = advance(&cur[t], &song->beats_bar);
        }
        for (;;) {
            Cursor *c;
            int best = -1;
            for (t = 0; t < ntrk; t++)
                if (cur[t].live && (best < 0 || cur[t].tick < cur[best].tick))
                    best = t;
            if (best < 0)
                break;
            c = &cur[best];
            us += (double)(c->tick - last_tick) * tempo / division;
            last_tick = c->tick;
            if (c->st == 0xFF) {
                tempo = ((unsigned long)c->d1 << 16) | ((unsigned)c->d2 << 8) | c->d3;
                if (c->tick == 0)
                    song->beat = (unsigned)((double)tempo * TICK_HZ / 1e6 + 0.5);
            } else {
                if (pass) {
                    Ev __far *e = &g_chunk[(unsigned)(n % CHUNK_EV)];
                    e->delta = (unsigned short)((us - last_us_out) * TICK_HZ / 1e6 + 0.5);
                    last_us_out += (double)e->delta * 1e6 / TICK_HZ;
                    e->status = c->st;
                    e->d1 = c->d1;
                    e->d2 = c->d2;
                    e->flags = 0;
                    if (n % CHUNK_EV == CHUNK_EV - 1)
                        xms_put(song->x, (n + 1 - CHUNK_EV) * sizeof(Ev), g_chunk, CHUNK_EV * sizeof(Ev));
                }
                n++;
            }
            c->live = advance(c, &song->beats_bar);
        }
        if (!pass) {
            nev = n;
            song->x = n ? xms_alloc((n + 1) * sizeof(Ev), XMS_RESIDENT) : 0;
            if (!song->x)
                break;
        } else {
            /* a rest to the end of the last bar, so the loop keeps time */
            unsigned long bar = (unsigned long)song->beat * song->beats_bar;
            unsigned long total = (unsigned long)(last_us_out * TICK_HZ / 1e6 + 0.5);
            Ev __far *e = &g_chunk[(unsigned)(n % CHUNK_EV)];
            e->delta = (unsigned short)(bar ? (bar - total % bar) % bar : 0);
            e->status = e->d1 = e->d2 = 0;
            e->flags = EV_LAST;
            n++;
            ok = xms_put(song->x, (n - 1) / CHUNK_EV * CHUNK_EV * sizeof(Ev), g_chunk,
                         ((n - 1) % CHUNK_EV + 1) * sizeof(Ev));
            song->n = ok ? (unsigned)n : 0;
        }
    }
    for (t = 0; t < ntrk; t++)
        _ffree(trk[t]);
    if (nev >= 65535UL) {
        rv_log("music: %s has too many events (%lu)", path, nev);
        song->n = 0;
        ok = 0;
    }
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
    for (i = 0; i < 2; i++) {
        g_ring[i].ev = (Ev __far *)_fmalloc(sizeof(Ev) * RING);
        g_ring[i].song = -1;
        if (!g_ring[i].ev) {
            rv_log("music: no memory for its rings");
            g_dev = DEV_NONE;
        }
    }
    g_chunk = (Ev __far *)_fmalloc(sizeof(Ev) * CHUNK_EV);    /* only while reading */
    for (i = 0; g_chunk && i < MAX_SONGS; i++) {
        char name[13];
        Song song;
        _fstrcpy(name, g_files[i]);
        if (load_song(name, &song)) {
            _fmemcpy(&g_song[i], &song, sizeof song);
            loaded++;
            rv_log("music: %s, %u events, beat %u ticks, %u to the bar", name, song.n, song.beat, song.beats_bar);
        }
    }
    if (g_chunk)
        _ffree(g_chunk);
    g_chunk = 0;
    _fheapshrink();                     /* (the tracks read are given back to DOS: the far heap grew for them) */
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

static int playable(int cue)
{
    return cue >= 0 && cue < MAX_SONGS && g_song[cue].n && g_dev != DEV_NONE ? cue : -1;
}

/* the channels' programs, volumes, pans and expressions as they stood before event `upto` of the song, put in
 * the ring to be played at once (the chase); returns the music ticks into its bar at that event */
static unsigned long chase(Ring *r, int song, unsigned upto)
{
    static const unsigned char cc[3] = { 7, 10, 11 };
    const Song __far *s = &g_song[song];
    unsigned long ticks = 0, bar = (unsigned long)s->beat * s->beats_bar;
    unsigned char st[16][4];            /* the program, then controllers 7, 10, 11; 0xFF: never set */
    Ev buf[32];
    unsigned i, n, k, ch;
    memset(st, 0xFF, sizeof st);
    for (i = 0; i < upto; i += n) {
        n = upto - i < 32 ? upto - i : 32;
        xms_get(s->x, (unsigned long)i * sizeof(Ev), (void __far *)buf, (unsigned long)n * sizeof(Ev));
        for (k = 0; k < n; k++) {
            const Ev *e = &buf[k];
            ch = e->status & 15;
            ticks += e->delta;
            if ((e->status & 0xF0) == 0xC0)
                st[ch][0] = e->d1;
            else if ((e->status & 0xF0) == 0xB0 && (e->d1 == 7 || e->d1 == 10 || e->d1 == 11))
                st[ch][e->d1 == 7 ? 1 : e->d1 == 10 ? 2 : 3] = e->d2;
        }
    }
    for (ch = 0; ch < 16; ch++)
        for (k = 0; k < 4; k++)
            if (st[ch][k] != 0xFF) {
                Ev __far *e = &r->ev[r->wr % RING];
                e->delta = 0;
                e->flags = 0;
                e->status = (unsigned char)((k ? 0xB0 : 0xC0) | ch);
                e->d1 = k ? cc[k - 1] : st[ch][0];
                e->d2 = k ? st[ch][k] : 0;
                r->wr++;
            }
    return bar ? ticks % bar : 0;
}

/* the song (or none) into the waiting ring, from event `from` (its channels chased there), and the switch to it
 * once the playing one has faded out */
static void cue_song(int song, int fade_ms, int at, unsigned from)
{
    int step = fade_ms > 0 ? (int)(1024L * 1000 / ((long)fade_ms * TICK_HZ)) : 1024;
    unsigned long ticks = 0;
    _disable();
    g_next = -1;                        /* no switch to a ring while it is filled */
    _enable();
    if (song >= 0) {
        Ring *r = &g_ring[1 - g_play];
        unsigned first;
        r->song = song;
        r->rd = r->wr = 0;
        if (from)
            ticks = chase(r, song, from);
        first = r->wr;
        r->src = from;
        fill(r);
        if (from && r->wr != first)
            r->ev[first % RING].delta = 0;      /* (where it was cut: at once) */
    }
    _disable();
    g_step = step < 1 ? 1 : step;
    g_next_ring = 1 - g_play;
    g_next = song;
    g_start_ticks = ticks;
    g_wait = g_cur >= 0 ? at : 0;
    if (g_cur < 0)
        g_level = 0;                    /* (nothing sounding: no fade out to wait for) */
    if (!g_wait)
        g_target = 0;
    _enable();
}

void music_cue_at(int cue, int fade_ms, int at)
{
    int song = playable(cue);
    char name[13];
    if (cue == g_last)
        return;
    g_last = cue;
    if (song >= 0)
        _fstrcpy(name, g_files[cue]);
    if (song >= 0 && song < RV_MUSIC_STINGS && g_cur >= RV_MUSIC_STINGS && g_next == -1) {
        g_back = song;                  /* a sting playing: the new song after it, from its start */
        g_back_at = 0;
        rv_log("music: cue %d (%s) after the sting", cue, name);
        return;
    }
    g_back = -1;
    rv_log("music: cue %d (%s), fade %d ms%s", cue, song >= 0 ? name : "none", fade_ms,
           at == MUSIC_BAR ? " on the bar" : at == MUSIC_BEAT ? " on the beat" : "");
    cue_song(song, fade_ms, at, 0);
}

void music_sting(int cue)
{
    int song = playable(cue);
    char name[13];
    if (song < 0)
        return;
    _disable();
    if (g_next >= 0 && g_next < RV_MUSIC_STINGS) {         /* (a switch under way: then the song coming) */
        g_back = g_next;
        g_back_at = 0;
    } else if (g_cur >= 0 && g_cur < RV_MUSIC_STINGS) {    /* (a sting over a sting: the first one's song) */
        const Ring *r = &g_ring[g_play];
        long n = g_song[g_cur].n, at = (long)r->src - (long)(unsigned)(r->wr - r->rd) - (g_have_pend ? 1 : 0);
        g_back = g_cur;
        g_back_at = (unsigned)(((at % n) + n) % n);
    }
    _enable();
    _fstrcpy(name, g_files[cue]);
    rv_log("music: sting %d (%s), then cue %d from event %u", cue, name, g_back, g_back_at);
    g_last = cue;
    cue_song(song, 40, MUSIC_NOW, 0);
}

unsigned music_loops(void)
{
    return g_loops;
}

int music_playing(void)
{
    return g_cur;
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
