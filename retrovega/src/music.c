/* music.c: interactive music (docs/engine.md).
 *
 * Songs are Standard MIDI Files (type 0 or 1), read at start-up and flattened
 * into one event stream each, timed in ticks of the music clock: the PIT
 * reprogrammed to TICK_HZ, the BIOS's 18.2 Hz clock still chained so ticks()
 * keeps counting.  The interrupt plays the stream to the MPU-401 (UART mode,
 * port 330h) and loops it.
 *
 * Every channel's volume (controller 7) is the song's own value times a
 * master fade.  music_cue(cue, fade_ms) fades the playing song out over
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

#define TICK_HZ 240U
#define PIT_HZ 1193182UL
#define DIVISOR ((unsigned)(PIT_HZ / TICK_HZ))
#define MAX_SONGS 8
#define MAX_EVENTS 2000U               /* a song's events, while it is read */
#define MPU_DATA 0x330
#define MPU_STAT 0x331

typedef struct {
    unsigned short delta;               /* music ticks after the previous event */
    unsigned char status, d1, d2, pad;
} Ev;

typedef struct {
    Ev __far *ev;
    unsigned n;
    unsigned beat;                      /* music ticks a beat, from the first tempo */
    unsigned char beats_bar;
} Song;

static const char *g_files[] = { "BRIEF.MID", "BAR.MID", "LAUNCH.MID", "WIN.MID", "LOSE.MID", "QUARTERS.MID" };
static Song g_song[MAX_SONGS];
static int g_have_mpu;

/* the interrupt's state */
static void (__interrupt __far *g_old8)(void);
static volatile int g_cur = -1, g_next = -1, g_wait;         /* g_wait: 0 now, 1 beat, 2 bar */
static volatile unsigned g_pos, g_left;
static volatile unsigned long g_song_ticks;
static volatile int g_level = 0, g_target = 0, g_step = 1;   /* master fade, 0..1024 */
static volatile unsigned g_resend;
static unsigned char g_vol[16];
static unsigned g_chain;
static volatile unsigned long g_bytes;
static int g_installed;

/* tests (RV.EXE /midilog): every byte sent, with the music clock's tick */
#define REC_MAX 12000U
static unsigned long __far *g_rec_tick;
static unsigned char __far *g_rec_byte;
static unsigned g_nrec;
static int g_recording;
static volatile unsigned long g_clock;

void music_record(int on)
{
    if (on && !g_rec_tick) {
        g_rec_tick = (unsigned long __far *)_fmalloc(REC_MAX * 4UL);
        g_rec_byte = (unsigned char __far *)_fmalloc(REC_MAX);
    }
    g_recording = on && g_rec_tick && g_rec_byte;
}

static void mpu_put(unsigned char b)
{
    unsigned n = 0;
    while ((inp(MPU_STAT) & 0x40) && ++n < 2000);
    outp(MPU_DATA, b);
    g_bytes++;
    if (g_recording && g_nrec < REC_MAX) {
        g_rec_tick[g_nrec] = g_clock;
        g_rec_byte[g_nrec++] = b;
    }
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
    for (ch = 0; ch < 16; ch++) {
        mpu_put((unsigned char)(0xB0 | ch)); mpu_put(123); mpu_put(0);    /* all notes off */
        mpu_put((unsigned char)(0xB0 | ch)); mpu_put(121); mpu_put(0);    /* reset controllers */
        g_vol[ch] = 100;
    }
}

static void start(int song)
{
    silence();
    g_cur = song;
    g_pos = 0;
    g_song_ticks = 0;
    g_left = song >= 0 && g_song[song].n ? g_song[song].ev[0].delta : 0;
}

static void __interrupt __far isr(void)
{
    g_clock++;
    if (g_have_mpu) {
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
            if (++g_resend % 4 == 0 || g_level == g_target) {
                int ch;
                for (ch = 0; ch < 16; ch++)
                    send_volume(ch);
            }
        }
        if (g_next != -1 && g_level == 0) {
            start(g_next);
            g_next = -1;
            g_target = 1024;
            g_step *= 2;                        /* in over half the time it took to go */
        }
        if (g_cur >= 0 && g_song[g_cur].n) {
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
            while (g_left == 0) {
                const Ev __far *e = &s->ev[g_pos];
                unsigned char st = e->status;
                if ((st & 0xF0) == 0xB0 && e->d1 == 7) {
                    g_vol[st & 15] = e->d2;
                    send_volume(st & 15);
                } else if (st >= 0x80) {
                    mpu_put(st);
                    mpu_put(e->d1);
                    if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0)
                        mpu_put(e->d2);
                }
                if (++g_pos >= s->n) {
                    g_pos = 0;                  /* loop the song */
                    g_song_ticks = 0;
                }
                g_left = s->ev[g_pos].delta;
            }
            g_left--;
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

typedef struct { unsigned long tick; unsigned char st, d1, d2, tempo; unsigned long us; } Raw;

/* the file's events, merged across tracks, in microseconds */
static int load_song(const char *path, Song *song)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[14];
    unsigned char __far *data;
    Raw __far *raw;
    unsigned nraw = 0, ntrk, division, t, i;
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
    raw = (Raw __far *)_fmalloc(sizeof(Raw) * MAX_EVENTS);   /* only while reading */
    if (!data || !raw) {
        rv_log("music: no memory to read %s", path);
        fclose(f);
        if (data) _ffree(data);
        if (raw) _ffree(raw);
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
        _ffree(raw);
        return 0;
    }
    ntrk = be16(hdr + 10);
    division = be16(hdr + 12);
    song->beats_bar = 4;
    {
        const unsigned char __far *p = data + 8 + be32(hdr + 4);
        for (t = 0; t < ntrk && p + 8 <= data + size; t++) {
            unsigned char hd[8];
            const unsigned char __far *end;
            unsigned long tick = 0;
            unsigned char running = 0;
            _fmemcpy(hd, p, 8);
            if (memcmp(hd, "MTrk", 4) != 0)
                break;
            end = p + 8 + be32(hd + 4);
            p += 8;
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
                        raw[nraw].st = 0;
                        raw[nraw].tempo = 1;
                        raw[nraw].us = ((unsigned long)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
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
                raw[nraw].tempo = 0;
                nraw++;
            }
            p = end;
        }
    }
    _ffree(data);
    /* merge the tracks: a stable sort by tick (insertion; the tracks are sorted) */
    for (i = 1; i < nraw; i++) {
        Raw r = raw[i];
        unsigned k = i;
        while (k > 0 && raw[k - 1].tick > r.tick) {
            raw[k] = raw[k - 1];
            k--;
        }
        raw[k] = r;
    }
    /* to music ticks, through the tempo map */
    song->ev = (Ev __far *)_fmalloc(sizeof(Ev) * (nraw + 1));
    if (!song->ev) {
        _ffree(raw);
        return 0;
    }
    {
        unsigned long last_tick = 0;
        double us = 0, last_us_out = 0;
        unsigned n = 0;
        song->beat = (unsigned)((double)tempo * TICK_HZ / 1e6 + 0.5);
        for (i = 0; i < nraw; i++) {
            us += (double)(raw[i].tick - last_tick) * tempo / division;
            last_tick = raw[i].tick;
            if (raw[i].tempo) {
                tempo = raw[i].us;
                if (raw[i].tick == 0)
                    song->beat = (unsigned)((double)tempo * TICK_HZ / 1e6 + 0.5);
                continue;
            }
            song->ev[n].delta = (unsigned short)((us - last_us_out) * TICK_HZ / 1e6 + 0.5);
            last_us_out += (double)song->ev[n].delta * 1e6 / TICK_HZ;
            song->ev[n].status = raw[i].st;
            song->ev[n].d1 = raw[i].d1;
            song->ev[n].d2 = raw[i].d2;
            n++;
        }
        /* a rest to the end of the last bar, so the loop keeps time */
        {
            unsigned long bar = (unsigned long)song->beat * song->beats_bar;
            unsigned long total = (unsigned long)(last_us_out * TICK_HZ / 1e6 + 0.5);
            song->ev[n].delta = (unsigned short)(bar ? (bar - total % bar) % bar : 0);
            song->ev[n].status = 0;
            n++;
        }
        song->n = n;
    }
    _ffree(raw);
    return 1;
}

int music_init(void)
{
    int i, loaded = 0;
    g_have_mpu = mpu_init();
    rv_log("music: MPU-401 %s", g_have_mpu ? "in UART mode" : "not answering (silent)");
    for (i = 0; i < (int)RV_COUNT(g_files) && i < MAX_SONGS; i++)
        if (load_song(g_files[i], &g_song[i])) {
            loaded++;
            rv_log("music: %s, %u events, beat %u ticks, %u to the bar", g_files[i], g_song[i].n, g_song[i].beat,
                   g_song[i].beats_bar);
        }
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
    if (g_have_mpu)
        silence();
    rv_log("music: %lu bytes sent to the MPU-401", g_bytes);
    if (g_recording) {
        FILE *f = fopen("MIDILOG.BIN", "wb");
        unsigned i;
        if (f) {
            for (i = 0; i < g_nrec; i++) {
                unsigned long t = g_rec_tick[i];
                unsigned char b = g_rec_byte[i];
                fwrite(&t, 4, 1, f);
                fwrite(&b, 1, 1, f);
            }
            fclose(f);
        }
    }
}

void music_cue_at(int cue, int fade_ms, int at)
{
    static int last = -2;
    int song = cue >= 0 && cue < MAX_SONGS && g_song[cue].n ? cue : -1;
    int step = fade_ms > 0 ? (int)(1024L * 1000 / ((long)fade_ms * TICK_HZ)) : 1024;
    if (cue == last)
        return;
    last = cue;
    rv_log("music: cue %d (%s), fade %d ms%s", cue, song >= 0 ? g_files[cue] : "none", fade_ms,
           at == MUSIC_BAR ? " on the bar" : at == MUSIC_BEAT ? " on the beat" : "");
    _disable();
    g_step = step < 1 ? 1 : step;
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
