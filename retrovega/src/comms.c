/* comms.c: the comm link on a cockpit VDU (comms.h). */
#include <string.h>
#include <malloc.h>
#include "rv.h"
#include "music.h"
#include "lipsync.h"
#include "campaign.h"
#include "comms.h"

#define LEVELS 8
#define ROWS 40                         /* the kept picture's rows: each drawn twice */
#define PATCHES 4                       /* after a voice's window: eyes half, shut, left, right; then its mouths */
#define MAX_STEPS 200                   /* mouth shapes a line (20 s) */
#define STEP_TICKS 24                   /* one a step: lipsync's 100 ms, on the music clock (240 a second) */
#define TAIL_TICKS 240                  /* the face stays a second after the line */
#define LINE 80

/* the 4 x 6 font the callsign is stencilled in (tools/comms.py's): a glyph's width (3, 4 or 5), then its six rows,
 * each row's pixels in its low bits, the leftmost the highest */
static const unsigned char __far g_glyph[26][7] = {
    { 4, 0x06, 0x09, 0x09, 0x0F, 0x09, 0x09 }, { 4, 0x0E, 0x09, 0x0E, 0x09, 0x09, 0x0E },   /* A B */
    { 4, 0x07, 0x08, 0x08, 0x08, 0x08, 0x07 }, { 4, 0x0E, 0x09, 0x09, 0x09, 0x09, 0x0E },   /* C D */
    { 4, 0x0F, 0x08, 0x0E, 0x08, 0x08, 0x0F }, { 4, 0x0F, 0x08, 0x0E, 0x08, 0x08, 0x08 },   /* E F */
    { 4, 0x07, 0x08, 0x0B, 0x09, 0x09, 0x07 }, { 4, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x09 },   /* G H */
    { 3, 0x07, 0x02, 0x02, 0x02, 0x02, 0x07 }, { 4, 0x03, 0x01, 0x01, 0x01, 0x09, 0x06 },   /* I J */
    { 4, 0x09, 0x0A, 0x0C, 0x0A, 0x09, 0x09 }, { 4, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0F },   /* K L */
    { 5, 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11 }, { 4, 0x09, 0x0D, 0x0B, 0x09, 0x09, 0x09 },   /* M N */
    { 4, 0x06, 0x09, 0x09, 0x09, 0x09, 0x06 }, { 4, 0x0E, 0x09, 0x09, 0x0E, 0x08, 0x08 },   /* O P */
    { 4, 0x06, 0x09, 0x09, 0x09, 0x0A, 0x05 }, { 4, 0x0E, 0x09, 0x09, 0x0E, 0x0A, 0x09 },   /* Q R */
    { 4, 0x07, 0x08, 0x06, 0x01, 0x01, 0x0E }, { 3, 0x07, 0x02, 0x02, 0x02, 0x02, 0x02 },   /* S T */
    { 4, 0x09, 0x09, 0x09, 0x09, 0x09, 0x06 }, { 4, 0x09, 0x09, 0x09, 0x09, 0x05, 0x02 },   /* U V */
    { 5, 0x11, 0x11, 0x11, 0x15, 0x1B, 0x11 }, { 4, 0x09, 0x09, 0x06, 0x06, 0x09, 0x09 },   /* W X */
    { 3, 0x05, 0x05, 0x05, 0x02, 0x02, 0x02 }, { 4, 0x0F, 0x01, 0x02, 0x04, 0x08, 0x0F },   /* Y Z */
};

static XmsBlock g_pic;                  /* the picture as the VDU shows it (COMMS_W x COMMS_H): entries, rows twice */
static RvCommVoice __far g_cast[RV_COMM_VOICES];  /* RV.DAT's voices */
static int __far g_ncast;
static unsigned char __far g_ramp[2 * LEVELS];   /* a shade's entry, its scan line's */
static unsigned char __far g_shade[256];          /* an entry's shade (the grain's), 0xFF: not the link's */
static unsigned char __far g_steps[MAX_STEPS];
static char __far g_line[LINE];                   /* a voice's own line, when it says that */
static unsigned char __far g_kept[COMMS_W];       /* a kept row's shades */
static unsigned char __far g_part[COMMS_W];       /* (an asset's row) */
static unsigned char __far g_out[2][COMMS_W];     /* a kept row as drawn: its two rows */
static struct {
    int vdu, on, nsteps, cols, sx;
    RvCommVoice v;                      /* who speaks */
    int eyes;                           /* shown: 0 open, 1 half, 2 shut, 3 left, 4 right */
    int mouth;                          /* shown: 0 at rest, else its mouth patch (1 .. v.nmouth) */
    unsigned long t0, until, eyes_at, next_look;
    int look;                           /* the eye animation: 0 none, 1 a blink, 4 / 5 a glance left / right */
    unsigned rnd;
    int callsign_w;                     /* its width in the font, pixels (0: none) */
    unsigned char ink, shadow, bg[2];
} __far g_c;

/* the bench's draws: frames left to time, the music ticks they took, the VDU rows they drew */
#define BENCH_REPS 16
static int __far g_bench_frames;
static unsigned long __far g_bench_ticks, __far g_bench_rows;
static int __far g_bench_h, __far g_bench_voice;

static unsigned rnd(void)
{
    g_c.rnd = g_c.rnd * 25173U + 13849U;
    return g_c.rnd >> 4;
}

void comms_flight_start(void)
{
    const RvDatHeader __far *hd = dat_header();
    int i;
    g_c.on = 0;
    g_c.rnd = 7;
    g_pic = 0;
    g_ncast = 0;
    if (hd->comm_cast == 0xFFFF || hd->comm_ramp == 0xFFFF)
        return;
    g_ncast = (int)(dat_asset(hd->comm_cast)->size / sizeof(RvCommVoice));
    if (g_ncast > RV_COMM_VOICES)
        g_ncast = RV_COMM_VOICES;
    dat_read(dat_asset(hd->comm_cast)->at, g_cast, (unsigned long)g_ncast * sizeof(RvCommVoice));
    g_pic = xms_alloc((unsigned long)COMMS_W * COMMS_H, XMS_MISSION);
    dat_read(dat_asset(hd->comm_ramp)->at, g_ramp, sizeof g_ramp);
    _fmemset(g_shade, 0xFF, sizeof g_shade);
    for (i = LEVELS - 1; i >= 0; i--) {      /* (an entry both ramps share: the even row's shade) */
        g_shade[g_ramp[LEVELS + i]] = (unsigned char)i;
        g_shade[g_ramp[i]] = (unsigned char)i;
    }
}

void comms_flight_end(void)
{
    g_pic = 0;                          /* (the mission level gives it back) */
    g_c.on = 0;
}

int comms_voices(void)
{
    return g_ncast;
}

int comms_voice(int person)
{
    unsigned v;
    if (person < 0 || person >= (int)dat_header()->npeople || (v = dat_person(person)->comm) >= (unsigned)g_ncast)
        return -1;
    return (int)v;
}

int comms_voice_for(int side, int type)
{
    int i, k, any = -1;
    for (i = 0; i < g_ncast; i++) {
        if (g_cast[i].side != side)
            continue;
        if (any < 0)
            any = i;
        for (k = 0; k < 4; k++)
            if (g_cast[i].ships[k] == type)
                return i;
    }
    return any;
}

/* an asset of the link's shades over kept row r (its part on that row, if any; a patch's clear pixels left) */
static void over(unsigned asset, int r)
{
    const RvAsset __far *a = dat_asset(asset);
    int x, n;
    if (!a->w || r < a->y || r >= a->y + (int)a->h)
        return;
    n = a->w;
    if (a->x + n > g_c.cols)
        n = g_c.cols - a->x;
    dat_read(a->at + (unsigned long)(r - a->y) * a->w, g_part, n);
    for (x = 0; x < n; x++)
        if (g_part[x] < LEVELS)
            g_kept[a->x + x] = g_part[x];
}

/* kept rows r0 .. r1-1 put together again (the base, the window, the eyes' patch, the mouth's) and written as
 * drawn */
static void rows(int r0, int r1)
{
    int r, x, k;
    for (r = r0; r < r1; r++) {
        over(g_c.v.base, r);
        over(g_c.v.win, r);
        if (g_c.eyes)
            over(g_c.v.win + g_c.eyes, r);
        if (g_c.mouth)
            over(g_c.v.win + PATCHES + g_c.mouth, r);
        for (x = 0; x < g_c.cols; x++)
            for (k = 0; k < g_c.sx; k++) {
                unsigned char s = g_kept[x] & (LEVELS - 1);
                g_out[0][x * g_c.sx + k] = g_ramp[s];
                g_out[1][x * g_c.sx + k] = g_ramp[LEVELS + s];
            }
        xms_put(g_pic, (unsigned long)(2 * r) * COMMS_W, g_out, 2UL * COMMS_W);
    }
}

/* the rows a patch covers, into r0 .. r1 */
static void span(unsigned asset, int *r0, int *r1)
{
    const RvAsset __far *a = dat_asset(asset);
    if (!a->h)
        return;
    if (a->y < *r0) *r0 = a->y;
    if (a->y + (int)a->h > *r1) *r1 = a->y + a->h;
}

/* the eyes (0 open, 1 half, 2 shut, 3 left, 4 right) and the mouth (0 at rest, else its patch): the rows the old
 * patches and the new cover put together again */
static void show(int eyes, int mouth)
{
    int r0 = ROWS, r1 = 0;
    if (eyes == g_c.eyes && mouth == g_c.mouth)
        return;
    if (eyes != g_c.eyes) {
        if (g_c.eyes) span(g_c.v.win + g_c.eyes, &r0, &r1);
        if (eyes) span(g_c.v.win + eyes, &r0, &r1);
    }
    if (mouth != g_c.mouth) {
        if (g_c.mouth) span(g_c.v.win + PATCHES + g_c.mouth, &r0, &r1);
        if (mouth) span(g_c.v.win + PATCHES + mouth, &r0, &r1);
    }
    g_c.eyes = eyes;
    g_c.mouth = mouth;
    if (r0 < r1)
        rows(r0, r1);
}

int comms_say(int voice, int vdu, const char __far *text)
{
    int sum = 0, n = 0, x, y;
    const char __far *c;
    if (!g_pic || voice < 0 || voice >= g_ncast)
        return 0;
    g_c.v = g_cast[voice];
    if (!text) {                        /* (the voice's own line) */
        g_line[0] = 0;
        if (g_c.v.line != 0xFFFF) {
            const RvAsset __far *a = dat_asset(g_c.v.line);
            dat_read(a->at, g_line, a->size < LINE ? a->size : LINE);
            g_line[LINE - 1] = 0;
        }
        text = g_line;
    }
    g_c.vdu = vdu;
    g_c.cols = dat_asset(g_c.v.base)->w;
    g_c.sx = g_c.cols ? COMMS_W / g_c.cols : 1;
    g_c.nsteps = lip_steps(text, g_steps, MAX_STEPS);
    g_c.t0 = clock240();
    g_c.until = g_c.t0 + (unsigned long)g_c.nsteps * STEP_TICKS + TAIL_TICKS;
    g_c.eyes = g_c.mouth = 0;
    g_c.look = 0;
    g_c.next_look = g_c.t0 + 120 + rnd() % 480;
    g_c.callsign_w = 0;
    if (g_c.v.cx)
        for (g_c.callsign_w = -1, c = g_c.v.callsign; c < g_c.v.callsign + sizeof g_c.v.callsign && *c; c++)
            if (*c >= 'A' && *c <= 'Z')
                g_c.callsign_w += g_glyph[*c - 'A'][0] + 1;
    rows(0, ROWS);
    /* the ground (the picture's corner) and the callsign's ink: light on a dark shell, dark on a light one */
    xms_get(g_pic, 0, g_out, 2UL * COMMS_W);
    g_c.bg[0] = g_out[0][0];
    g_c.bg[1] = g_out[1][0];
    if (g_c.callsign_w > 0) {
        for (y = g_c.v.cy - 3; y < g_c.v.cy + 3; y += 2) {
            xms_get(g_pic, (unsigned long)y * COMMS_W, g_out, COMMS_W);
            for (x = g_c.v.cx - 16; x < g_c.v.cx + 16; x++, n++)
                sum += g_shade[g_out[0][x]] & (LEVELS - 1);
        }
        if (n && sum * 100 / n < (LEVELS - 1) * 55) {
            g_c.ink = g_ramp[6];
            g_c.shadow = g_ramp[1];
        } else {
            g_c.ink = g_ramp[1];
            g_c.shadow = g_ramp[6];
        }
    }
    g_c.on = 1;
    return 1;
}

int comms_on(int vdu)
{
    return g_c.on && g_c.vdu == vdu;
}

int comms_side(void)
{
    return g_c.on ? g_c.v.side : -1;
}

/* lipsync's shape now (0 at rest or after the line) */
static int shape(void)
{
    int step = (int)((clock240() - g_c.t0) / STEP_TICKS);
    return step < g_c.nsteps ? g_steps[step] : 0;
}

void comms_tick(void)
{
    unsigned long now = clock240();
    int eyes = g_c.eyes, s;
    if (g_bench_frames > 0 && --g_bench_frames == 0 && g_bench_rows) {
        static const char __far s_draw[] = "comms: cost: a draw (%d rows) %lu us, from %lu rows in %lu ticks";
        rv_logf(s_draw, g_bench_h, g_bench_ticks * 1000000UL / 240 * g_bench_h / g_bench_rows, g_bench_rows,
                g_bench_ticks);
    }
    if (!g_c.on)
        return;
    if (now >= g_c.until) {
        g_c.on = 0;
        return;
    }
    /* the eyes: open; now and then a blink (half, shut, half) or a glance aside, half a second */
    if (g_c.look) {
        unsigned long k = now - g_c.eyes_at;
        if (g_c.look == 1) {
            eyes = k < 12 ? 1 : k < 30 ? 2 : k < 42 ? 1 : 0;
            if (k >= 42) g_c.look = 0;
        } else {
            eyes = k < 130 ? g_c.look - 1 : 0;
            if (k >= 130) g_c.look = 0;
        }
        if (!g_c.look)
            g_c.next_look = now + 300 + rnd() % 700;
    } else if (now >= g_c.next_look) {
        g_c.look = rnd() % 4 == 0 ? 4 + (int)(rnd() & 1) : 1;
        g_c.eyes_at = now;
        eyes = g_c.look == 1 ? 1 : g_c.look - 1;
    }
    /* the mouth: lipsync's shape, all eight (a bare face) or by how open (an alien's lights: 1 .. 4, 5 .. 8) */
    s = shape();
    show(eyes, s && g_c.v.nmouth ? (s * g_c.v.nmouth + 7) / 8 : 0);
}

/* the callsign's pixels on screen row y (picture row sy), offset px: its shadow first, a pixel down and right */
static void callsign_row(unsigned char __far *d, int sy, int px, int x0, int x1)
{
    int k, x, w, row, shadow, top = g_c.v.cy - 3;
    const char __far *c;
    if (g_c.callsign_w <= 0 || sy < top || sy > top + 6)
        return;
    for (shadow = 1; shadow >= 0; shadow--) {
        row = sy - top - shadow;
        if (row < 0 || row >= 6)
            continue;
        x = px + g_c.v.cx - g_c.callsign_w / 2 + shadow;
        for (c = g_c.v.callsign; c < g_c.v.callsign + sizeof g_c.v.callsign && *c; c++) {
            if (*c < 'A' || *c > 'Z')
                continue;
            w = g_glyph[*c - 'A'][0];
            for (k = 0; k < w; k++)
                if ((g_glyph[*c - 'A'][1 + row] >> (w - 1 - k)) & 1 && x + k >= x0 && x + k < x1)
                    d[x + k] = shadow ? g_c.shadow : g_c.ink;
            x += w + 1;
        }
    }
}

static int draw(int vdu, const RvRect __far *r, int y0, int y1);

int comms_draw(int vdu, const RvRect __far *r, int y0, int y1)
{
    if (g_bench_frames > 0 && vdu == g_c.vdu && g_c.on) {   /* (/commdemo: the draw timed, BENCH_REPS times over) */
        unsigned long t0 = clock240();
        int k;
        for (k = 0; k < BENCH_REPS; k++)
            draw(vdu, r, y0, y1);
        g_bench_ticks += clock240() - t0;
        g_bench_rows += (unsigned long)BENCH_REPS * ((y1 < r->y + r->h ? y1 : r->y + r->h) - (y0 > r->y ? y0 : r->y));
        g_bench_h = r->h;
    }
    return draw(vdu, r, y0, y1);
}

static int draw(int vdu, const RvRect __far *r, int y0, int y1)
{
    int px, py, y, sx0, sx1, lit = 0;
    if (!g_c.on || vdu != g_c.vdu || !g_pic || !r->w)
        return 0;
    px = r->x + (r->w - COMMS_W) / 2;   /* (centred: cropped on a smaller VDU, the link's ground round a bigger; */
    py = r->y + (r->h < COMMS_H ? (r->h - COMMS_H) / 4 : (r->h - COMMS_H) / 2);   /*  a short one loses the
                                           most at the bottom, the mask and the hose, keeping the callsign) */
    if (y0 < r->y) y0 = r->y;
    if (y1 > r->y + r->h) y1 = r->y + r->h;
    sx0 = r->x > px ? r->x - px : 0;
    sx1 = r->x + r->w < px + COMMS_W ? r->x + r->w - px : COMMS_W;
    if (y0 < y1 && g_c.v.lx)
        lit = shape() != 0;
    for (y = y0; y < y1; y++) {
        unsigned char __far *d = vid_row(y);
        int sy = y - py, bg = g_c.bg[(y - py) & 1], k;
        if (sy < 0 || sy >= COMMS_H) {
            _fmemset(d + r->x, bg, r->w);
        } else {
            if (px + sx0 > r->x)
                _fmemset(d + r->x, bg, px + sx0 - r->x);
            xms_get(g_pic, (unsigned long)sy * COMMS_W + sx0, d + px + sx0, sx1 - sx0);   /* (host speed: DOSBox) */
            if (px + sx1 < r->x + r->w)
                _fmemset(d + px + sx1, bg, r->x + r->w - (px + sx1));
            callsign_row(d, sy, px, r->x, r->x + r->w);
            if (lit && (sy == g_c.v.ly || sy == g_c.v.ly - 1)) {   /* the regulator's light: 2 x 2 */
                int lx = px + g_c.v.lx - 1;
                if (lx >= r->x && lx + 2 <= r->x + r->w)
                    d[lx] = d[lx + 1] = g_ramp[LEVELS - 1];
            }
        }
        for (k = 0; k < 3; k++) {       /* the grain: a few pixels a shade up or down, new every frame */
            unsigned c = rnd(), x = r->x + c % r->w;
            int s = g_shade[d[x]];
            if (s != 0xFF) {
                s += (c & 0x8000) ? 1 : -1;
                if (s >= 0 && s < LEVELS)
                    d[x] = g_ramp[((y - py) & 1) * LEVELS + s];
            }
        }
    }
    return 1;
}

/* --- what a comm costs (RV.EXE /commdemo logs it first) --- */

static const char __far s_copy[] = "Copy that.";

/* how many times op(i) runs in a quarter of a second (60 music ticks), from a tick's edge */
static unsigned long per_quarter(void (*op)(unsigned long))
{
    unsigned long t0 = clock240(), n = 0;
    while (clock240() == t0)
        ;
    t0 = clock240();
    do
        op(n++);
    while (clock240() - t0 < 60);
    return n;
}

static void op_say(unsigned long i)
{
    (void)i;
    comms_say(g_bench_voice, 0, s_copy);
}

static void op_eyes(unsigned long i)
{
    show((int)(i & 1) * 2, g_c.mouth);  /* (shut, open) */
}

static void op_mouth(unsigned long i)
{
    show(g_c.eyes, (int)(i % g_c.v.nmouth) + 1);
}

void comms_bench(void)
{
    static const char __far s_cost[] = "comms: cost: voice %d (%Fs, %d mouths): a start %lu us, a blink's step %lu "
                                       "us, a mouth's %lu us (a third of the frames, speaking)";
    static const char __far s_none[] = "comms: cost: no comm art";
    static const char __far s_alien[] = "an alien";
    int k, v;
    if (!g_pic || !g_ncast) {
        rv_logf(s_none);
        return;
    }
    for (k = 0; k < 2; k++) {           /* the first pilot, and the first alien */
        unsigned long eyes, mouth = 0, start;
        if ((v = k ? comms_voice_for(1, -1) : 0) < 0)
            continue;
        g_bench_voice = v;
        start = 250000UL / per_quarter(op_say);
        eyes = 250000UL / per_quarter(op_eyes);
        if (g_c.v.nmouth)
            mouth = 250000UL / per_quarter(op_mouth);
        rv_logf(s_cost, v, g_c.v.callsign[0] ? (const char __far *)g_c.v.callsign : s_alien, g_c.v.nmouth, start,
                eyes, mouth);
    }
    /* then the draw, in the next 8 frames (it needs the frame's rows): the first pilot on the left VDU */
    comms_say(0, 0, s_copy);
    g_bench_frames = 8;
    g_bench_ticks = g_bench_rows = 0;
}
