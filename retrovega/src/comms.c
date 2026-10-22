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
#define PATCHES 4                       /* after a pilot's window: eyes half, shut, left, right */
#define MAX_STEPS 200                   /* mouth shapes a line (20 s): only whether one is open is used (the light) */
#define STEP_TICKS 24                   /* one a step: lipsync's 100 ms, on the music clock (240 a second) */
#define TAIL_TICKS 240                  /* the face stays a second after the line */

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
static unsigned char __far g_ramp[2 * LEVELS + 4];   /* a shade's entry, its scan line's; the callsign's x y, the light's */
static unsigned char __far g_shade[256];          /* an entry's shade (the grain's), 0xFF: not the link's */
static unsigned char __far g_steps[MAX_STEPS];
static unsigned char __far g_kept[COMMS_W];       /* a kept row's shades */
static unsigned char __far g_part[COMMS_W];       /* (an asset's row) */
static unsigned char __far g_out[2][COMMS_W];     /* a kept row as drawn: its two rows */
static struct {
    int vdu, on, nsteps, cols, sx;
    unsigned win;                       /* the pilot's window asset; its patches after it */
    int eyes;                           /* shown: 0 open, 1 half, 2 shut, 3 left, 4 right */
    unsigned long t0, until, eyes_at, next_look;
    int look;                           /* the eye animation: 0 none, 1 a blink, 4 / 5 a glance left / right */
    unsigned rnd;
    char callsign[12];
    int callsign_w;                     /* its width in the font, pixels */
    unsigned char ink, shadow, bg[2];
} __far g_c;

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
    if (hd->comm_helmet == 0xFFFF || hd->comm_ramp == 0xFFFF)
        return;
    g_pic = xms_alloc((unsigned long)COMMS_W * COMMS_H, XMS_MISSION);
    dat_read(dat_asset(hd->comm_ramp)->at, g_ramp, sizeof g_ramp);
    _fmemset(g_shade, 0xFF, sizeof g_shade);
    for (i = LEVELS - 1; i >= 0; i--) {      /* (an entry both ramps share: the even row's shade) */
        g_shade[g_ramp[LEVELS + i]] = (unsigned char)i;
        g_shade[g_ramp[i]] = (unsigned char)i;
    }
    g_c.cols = dat_asset(hd->comm_helmet)->w;
    g_c.sx = g_c.cols ? COMMS_W / g_c.cols : 1;
}

void comms_flight_end(void)
{
    g_pic = 0;                          /* (the mission level gives it back) */
    g_c.on = 0;
}

/* an asset of the link's shades over kept row r (its part on that row, if any) */
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
        g_kept[a->x + x] = g_part[x];
}

/* kept rows r0 .. r1-1 put together again (the helmet, the window, the eyes' patch) and written as drawn */
static void rows(int r0, int r1)
{
    int r, x, k;
    for (r = r0; r < r1; r++) {
        over(dat_header()->comm_helmet, r);
        over(g_c.win, r);
        if (g_c.eyes)
            over(g_c.win + g_c.eyes, r);
        for (x = 0; x < g_c.cols; x++)
            for (k = 0; k < g_c.sx; k++) {
                unsigned char s = g_kept[x] & (LEVELS - 1);
                g_out[0][x * g_c.sx + k] = g_ramp[s];
                g_out[1][x * g_c.sx + k] = g_ramp[LEVELS + s];
            }
        xms_put(g_pic, (unsigned long)(2 * r) * COMMS_W, g_out, 2UL * COMMS_W);
    }
}

/* the eyes' patch: 0 open, 1 half, 2 shut, 3 left, 4 right; the rows the old and the new cover put together again */
static void show(int eyes)
{
    const RvAsset __far *a;
    int r0 = ROWS, r1 = 0, k;
    if (eyes == g_c.eyes)
        return;
    for (k = 0; k < 2; k++) {
        int e = k ? eyes : g_c.eyes;
        if (!e)
            continue;
        a = dat_asset(g_c.win + e);
        if (!a->h)
            continue;
        if (a->y < r0) r0 = a->y;
        if (a->y + (int)a->h > r1) r1 = a->y + a->h;
    }
    g_c.eyes = eyes;
    if (r0 < r1)
        rows(r0, r1);
}

int comms_say(int person, int vdu, const char __far *text)
{
    const RvDatHeader __far *hd = dat_header();
    unsigned win;
    int sum = 0, n = 0, x, y;
    if (!g_pic || person < 0 || person >= (int)hd->npeople || (win = dat_person(person)->comm) == 0xFFFF)
        return 0;
    g_c.vdu = vdu;
    g_c.win = win;
    g_c.nsteps = lip_steps(text, g_steps, MAX_STEPS);
    g_c.t0 = clock240();
    g_c.until = g_c.t0 + (unsigned long)g_c.nsteps * STEP_TICKS + TAIL_TICKS;
    g_c.eyes = 0;
    g_c.look = 0;
    g_c.next_look = g_c.t0 + 120 + rnd() % 480;
    for (n = 0; n < (int)sizeof g_c.callsign - 1 && g_pilots[person].callsign[n]; n++) {
        char ch = g_pilots[person].callsign[n];
        g_c.callsign[n] = ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
    }
    g_c.callsign[n] = 0;
    for (g_c.callsign_w = -1, n = 0; g_c.callsign[n]; n++)
        if (g_c.callsign[n] >= 'A' && g_c.callsign[n] <= 'Z')
            g_c.callsign_w += g_glyph[g_c.callsign[n] - 'A'][0] + 1;
    n = 0;
    rows(0, ROWS);
    /* the ground (the picture's corner) and the callsign's ink: light on a dark shell, dark on a light one */
    xms_get(g_pic, 0, g_out, 2UL * COMMS_W);
    g_c.bg[0] = g_out[0][0];
    g_c.bg[1] = g_out[1][0];
    for (y = g_ramp[2 * LEVELS + 1] - 3; y < g_ramp[2 * LEVELS + 1] + 3; y += 2) {
        xms_get(g_pic, (unsigned long)y * COMMS_W, g_out, COMMS_W);
        for (x = g_ramp[2 * LEVELS] - 16; x < g_ramp[2 * LEVELS] + 16; x++, n++)
            sum += g_shade[g_out[0][x]] & (LEVELS - 1);
    }
    if (n && sum * 100 / n < (LEVELS - 1) * 55) {
        g_c.ink = g_ramp[6];
        g_c.shadow = g_ramp[1];
    } else {
        g_c.ink = g_ramp[1];
        g_c.shadow = g_ramp[6];
    }
    g_c.on = 1;
    return 1;
}

int comms_on(int vdu)
{
    return g_c.on && g_c.vdu == vdu;
}

static int speaking(void)
{
    int step = (int)((clock240() - g_c.t0) / STEP_TICKS);
    return step < g_c.nsteps && g_steps[step] != 0;
}

void comms_tick(void)
{
    unsigned long now = clock240();
    int eyes = g_c.eyes;
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
    show(eyes);
}

/* the callsign's pixels on screen row y (picture row sy), offset px: its shadow first, a pixel down and right */
static void callsign_row(unsigned char __far *d, int sy, int px, int x0, int x1)
{
    int k, x, w, row, shadow, top = g_ramp[2 * LEVELS + 1] - 3;
    const char __far *c;
    if (g_c.callsign_w <= 0 || sy < top || sy > top + 6)
        return;
    for (shadow = 1; shadow >= 0; shadow--) {
        row = sy - top - shadow;
        if (row < 0 || row >= 6)
            continue;
        x = px + g_ramp[2 * LEVELS] - g_c.callsign_w / 2 + shadow;
        for (c = g_c.callsign; *c; c++) {
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

int comms_draw(int vdu, const RvRect __far *r, int y0, int y1)
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
    if (y0 < y1)
        lit = speaking();
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
            if (lit && (sy == g_ramp[2 * LEVELS + 3] || sy == g_ramp[2 * LEVELS + 3] - 1)) {   /* the light: 2 x 2 */
                int lx = px + g_ramp[2 * LEVELS + 2] - 1;
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
