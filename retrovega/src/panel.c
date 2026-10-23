/* panel.c: the cockpit's panel (panel.h). */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "rv.h"
#include "music.h"
#include "lightfield.h"
#include "panel.h"

ShipSystems __far g_sys;

static RvPanelControl __far g_pc[RV_PANEL];
static int __far g_n;
static unsigned char __far g_shown[RV_PANEL];   /* each control's state as drawn (a needle: its angle / 2, a bar or
                                                   a readout: a hash of what it shows) */
static unsigned char __far *__far g_map[RV_PANEL];   /* a map's pixels (its IMG, read in) */

/* a display's reading (needles, bars, readouts, maps): what it shows, drawn from this alone.  The displays take
 * turns, one a frame (the user's, 2026-10-23), so each lags the ship a little, as a scanning instrument would;
 * the lamps and switches are every frame's own. */
typedef struct {
    short v[6];                         /* a needle: its angle (the clock: and its minute hand's); a bar: its value;
                                           the survival display: the shields' ranks lit (front, rear), the armour's
                                           colours (front, rear, left, right); the heat picture: the heat */
    char a[16], b[12];                  /* a readout's lines (the survival display's other pages too) */
    unsigned char page, pad;            /* the survival display's page */
} Reading;
static Reading __far g_rd[RV_PANEL];
static int __far g_turn;                /* the display whose turn it is */
static unsigned char __far g_read_all;  /* the first frame: every display reads */

/* flight palette entries (the same in every cockpit's palette): the shields' blue ramp, the armour's levels, the
 * ship's grey, the heat's ramp (navy, blue, violet, the reds, the fire's to white) */
#define SHIELD_RAMP 73                  /* .. 79 */
static const unsigned char __far g_heat_ramp[] = { 72, 88, 89, 91, 93, 171, 96, 98, 99, 100, 130, 133, 134, 136, 138,
                                                   140, 142, 143 };
#define HEAT_STEPS (sizeof g_heat_ramp)

/* a 3 x 5 font for the small readouts: a glyph's five rows of three pixels, the top row in the high bits */
#define G(a, b, c, d, e) ((a) << 12 | (b) << 9 | (c) << 6 | (d) << 3 | (e))
static const unsigned short __far g_digit[10] = {
    G(7, 5, 5, 5, 7), G(2, 6, 2, 2, 7), G(7, 1, 7, 4, 7), G(7, 1, 7, 1, 7), G(5, 5, 7, 1, 1),
    G(7, 4, 7, 1, 7), G(7, 4, 7, 5, 7), G(7, 1, 1, 1, 1), G(7, 5, 7, 5, 7), G(7, 5, 7, 1, 7) };
static const unsigned short __far g_alpha[26] = {
    G(2, 5, 7, 5, 5), G(6, 5, 6, 5, 6), G(3, 4, 4, 4, 3), G(6, 5, 5, 5, 6), G(7, 4, 6, 4, 7), G(7, 4, 6, 4, 4),
    G(3, 4, 5, 5, 3), G(5, 5, 7, 5, 5), G(7, 2, 2, 2, 7), G(1, 1, 1, 5, 2), G(5, 5, 6, 5, 5), G(4, 4, 4, 4, 7),
    G(5, 7, 7, 5, 5), G(6, 5, 5, 5, 5), G(2, 5, 5, 5, 2), G(6, 5, 6, 4, 4), G(2, 5, 5, 6, 3), G(6, 5, 6, 5, 5),
    G(3, 4, 2, 1, 6), G(7, 2, 2, 2, 2), G(5, 5, 5, 5, 7), G(5, 5, 5, 5, 2), G(5, 5, 7, 7, 5), G(5, 5, 2, 5, 5),
    G(5, 5, 2, 2, 2), G(7, 1, 2, 4, 7) };

static unsigned short glyph(char ch)
{
    if (ch >= '0' && ch <= '9') return g_digit[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return g_alpha[ch - 'A'];
    if (ch >= 'a' && ch <= 'z') return g_alpha[ch - 'a'];
    if (ch == '.') return G(0, 0, 0, 0, 2);
    if (ch == '%') return G(5, 1, 2, 4, 5);
    if (ch == '-') return G(0, 0, 7, 0, 0);
    if (ch == ':') return G(0, 2, 0, 2, 0);
    return 0;
}

/* small text, 4 pixels a character; returns its width */
static int tiny(int x, int y, const char *s, unsigned char col, int y0, int y1)
{
    int x0 = x, r, k;
    for (; *s; s++, x += 4) {
        unsigned short g = glyph(*s);
        for (r = 0; r < 5; r++) {
            if (y + r < y0 || y + r >= y1)
                continue;
            for (k = 0; k < 3; k++)
                if ((g >> (12 - 3 * r + 2 - k)) & 1)
                    vid_row(y + r)[x + k] = col;
        }
    }
    return x - x0;
}

void panel_unload(void)
{
    int i;
    for (i = 0; i < RV_PANEL; i++) {
        if (g_map[i])
            _ffree(g_map[i]);
        g_map[i] = 0;
    }
    g_n = 0;
}

void panel_load(const RvCockpit __far *c)
{
    const RvAsset __far *a;
    int i;
    panel_unload();
    if (!c || c->panel == 0xFFFF || !c->npanel)
        return;
    a = dat_asset(c->panel);
    g_n = c->npanel < RV_PANEL ? c->npanel : RV_PANEL;
    dat_read(a->at, g_pc, (unsigned long)g_n * sizeof(RvPanelControl));
    _fmemset(g_shown, 0xFF, sizeof g_shown);
    _fmemset(g_rd, 0, sizeof g_rd);
    g_read_all = 1;
    g_turn = 0;
    for (i = 0; i < g_n; i++)           /* (the maps in conventional memory: a few KB, drawn row by row) */
        if (g_pc[i].kind == RV_PK_MAP) {
            const RvAsset __far *m = dat_asset(g_pc[i].sprite);
            g_map[i] = (unsigned char __far *)_fmalloc((size_t)m->size);
            RV_ASSERT(g_map[i] != 0);
            dat_read(m->at, g_map[i], m->size);
        }
}

int panel_has(int fn)
{
    int i;
    for (i = 0; i < g_n; i++)
        if (g_pc[i].fn == fn)
            return 1;
    return 0;
}

static int blink(int hz2)               /* on and off hz2 / 2 times a second */
{
    return (int)((clock240() * hz2 / 240) & 1);
}

static int pct(long v, long of)
{
    return of <= 0 ? 0 : v <= 0 ? 0 : v >= of ? 100 : (int)(v * 100 / of);
}

/* a lamp's, knob's or ladder's state now */
static int state(const RvPanelControl __far *p, const CockpitState *st)
{
    int a = p->arg, n = p->flags ? p->flags : 1;
    switch (p->fn) {
    case RV_PF_LOCK: return g_sys.lock;
    case RV_PF_WARN: return g_sys.warn && blink(4);
    case RV_PF_AB_LAMP: return g_sys.burning;
    case RV_PF_NAV_LAMP: return !(st->damage >> 12 & 1) && (!g_sys.autopilot_ready || blink(2));
    case RV_PF_COM_LAMP: return g_sys.talking;
    case RV_PF_COM_ON: return !g_sys.muted;
    case RV_PF_IFF_LAMP: return g_sys.iff;
    case RV_PF_EM_LAMP: return g_sys.emcon == EMCON_ACTIVE;
    case RV_PF_EMCON_LAMP: return a == 3 ? g_sys.emcon != EMCON_ACTIVE : g_sys.emcon == a;
    case RV_PF_SENSOR_LAMP: return g_sys.sensor == a;
    case RV_PF_TARGET_LAMP: return g_sys.target >= 0;
    case RV_PF_STATUS_LAMP:
        switch (a) {
        case 0: return (st->shield_f < st->shield_r ? st->shield_f : st->shield_r) < 34;
        case 1: return st->armor[0] < 34 || st->armor[1] < 34 || st->armor[2] < 34 || st->armor[3] < 34;
        case 2: return g_sys.hull < 50;
        case 3: return g_sys.fuel < 250;
        case 4: return (int)(st->damage >> 9 & 1);
        default: return (int)((st->damage >> 8 | st->damage >> 11) & 1);
        }
    case RV_PF_MFD_LAMP: return !(st->damage >> (a ? 1 : 0) & 1);
    case RV_PF_SURV_PAGE: return g_sys.surv_page == a;
    case RV_PF_RADIO_CHAN: return g_sys.chan == a;
    case RV_PF_ARM_LAMP: return g_sys.armed;
    case RV_PF_AUTO_LAMP:
        return a == 0 ? g_sys.autopilot_ready : a == 1 ? g_sys.match : a == 2 ? g_sys.autotarget : g_sys.autodecoy;
    case RV_PF_DEFENCE: return g_sys.decoys > 0;
    case RV_PF_SIG_LADDER: return g_sys.signature > a * 100 / n;
    case RV_PF_RANGE_LADDER: return a * 3 < (g_sys.radar_range + 1) * n;
    case RV_PF_CABIN: return g_sys.lights;
    case RV_PF_RADIO_DIGIT: return g_sys.digit[a < 3 ? a : 2];
    case RV_PF_RADIO_LIT: return g_sys.talking;
    case RV_PF_CHAN_KNOB: return g_sys.chan;
    case RV_PF_EMCON_KNOB: return g_sys.emcon;
    case RV_PF_NAV_KNOB: return g_sys.nav & 3;
    case RV_PF_IFF_KNOB: return g_sys.iff;
    case RV_PF_RADAR_KNOB: return g_sys.radar_range;
    case RV_PF_ARM_KNOB: return g_sys.armed;
    case RV_PF_VOL_KNOB: return !g_sys.muted;
    case RV_PF_THROTTLE_TICKS:
        return st->max_speed ? (int)((long)st->set_speed * (p->nstates - 1) / st->max_speed) : 0;
    }
    return p->painted;
}

/* --- the drawn controls: needles, bars, readouts --- */

/* a needle's angle now, degrees clockwise from up (a clock: its second hand) */
static int needle_angle(const RvPanelControl __far *p, const CockpitState *st)
{
    long full = p->sprite ? p->sprite : st->max_speed, v;
    if (p->fn == RV_PF_CLOCK_DIAL)
        return (int)(clock240() / 240 % 60) * 6;
    v = st->speed;
    if (v > full) v = full;
    return full > 0 ? p->a0 + (int)((long)(p->a1 - p->a0) * v / full) : p->a0;
}

static void hand(int cx2, int cy2, int angle, int len, unsigned char col, int y0, int y1)
{
    int a = (int)((long)angle * 1024 / 360), cx = cx2 / 2, cy = cy2 / 2;
    int x = cx + (int)(lf_sin(a) * len / 16384), y = cy - (int)(lf_cos(a) * len / 16384);
    gfx_line(cx, cy + 1, x, y + 1, UI_BLACK, 0, y0, y1);     /* (a shadow under it) */
    gfx_line(cx, cy, x, y, col, 0, y0, y1);
}

static void needle(const RvPanelControl __far *p, const Reading __far *r, int y0, int y1)
{
    if (p->fn == RV_PF_CLOCK_DIAL)
        hand(p->x, p->y, r->v[1], p->w * 6 / 10, UI_GREY5, y0, y1);
    hand(p->x, p->y, r->v[0], p->w, UI_WHITE, y0, y1);
}

static unsigned char level(int v)
{
    return v > 66 ? UI_HILITE : v > 33 ? UI_AMBER4 : UI_RED;
}

/* a bar's value now, percent */
static int bar_value(const RvPanelControl __far *p, const CockpitState *st)
{
    int i, m;
    switch (p->fn) {
    case RV_PF_SHIELD_BAR: return st->shield_f < st->shield_r ? st->shield_f : st->shield_r;
    case RV_PF_ARMOR_BAR:
        for (m = 100, i = 0; i < 4; i++)
            if (st->armor[i] < m) m = st->armor[i];
        return m;
    case RV_PF_HULL_BAR: return g_sys.hull;
    case RV_PF_FUEL_BAR: return g_sys.fuel / 10;
    case RV_PF_SIGNATURE: return g_sys.sig_shown;
    case RV_PF_POWER_BAR: return st->damage >> 9 & 1 ? 35 : 100;
    case RV_PF_ENGINE_BAR: return pct(st->speed, st->max_speed);
    case RV_PF_SPEED_BAR: return pct(st->speed, st->max_speed);
    case RV_PF_RANGE_BAR: return g_sys.target_km < 0 ? 0 : 100 - pct(g_sys.target_km, 4000L << g_sys.radar_range);
    }
    return 0;
}

static unsigned char shield_colour(int v) { return (unsigned char)(SHIELD_RAMP + (v < 0 ? 0 : v > 100 ? 6 : v * 6 / 100)); }
/* the survival display's colours (flight palette entries; the user's, 2026-10-23: one each, black when gone) */
#define SHIELD_LIT 79
#define ARMOUR_LIT 70
#define SHIP_GREY 190
static unsigned char __far g_svcol[256];   /* the survival display's map value to its colour (sv_colours) */

static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

/* the survival display's colours for a reading: each region's pixels lit while their rank is under its strength
 * (the outermost go first, inward), the rest black; the ship grey */
static void sv_colours(const Reading __far *r)
{
    int k, j;
    _fmemset(g_svcol, 0, sizeof g_svcol);
    g_svcol[RV_SV_SHIP] = SHIP_GREY;
    for (k = 0; k < 6; k++) {
        int lit = (int)((long)clamp100(r->v[k]) * RV_SV_RANKS / 100);
        for (j = 0; j < RV_SV_RANKS; j++)
            g_svcol[RV_SV_BASE + k * RV_SV_RANKS + j] = j < lit ? (k < 2 ? SHIELD_LIT : ARMOUR_LIT) : UI_BLACK;
    }
}
static unsigned char armour_colour(int v) { return v >= 67 ? 70 : v >= 34 ? 115 : v >= 1 ? 99 : 104; }

void panel_fuel_bar(int x, int y, int w, int h, int pct, int upright, int y0, int y1)
{
    /* backlit: the fuel left lit amber with a brighter edge along it; the spent part drawn not at all */
    int len;
    if (w <= 2 || h <= 2 || pct <= 0)
        return;
    if (upright) {
        len = (int)((long)(h - 2) * (pct > 100 ? 100 : pct) / 100);
        if (len > 0) {
            gfx_fill(x + 1, y + h - 1 - len, w - 2, len, 115, y0, y1);
            gfx_fill(x + 1, y + h - 1 - len, 1, len, 118, y0, y1);
        }
    } else {
        len = (int)((long)(w - 2) * (pct > 100 ? 100 : pct) / 100);
        if (len > 0) {
            gfx_fill(x + 1, y + 1, len, h - 2, 115, y0, y1);
            gfx_fill(x + 1, y + 1, len, 1, 118, y0, y1);
        }
    }
}

static void bar(const RvPanelControl __far *p, int v, int y0, int y1)
{
    int len, up = p->flags & 1, inner = up ? p->h - 2 : p->w - 2, k;
    unsigned char c;
    if (p->fn == RV_PF_FUEL_BAR) {
        panel_fuel_bar(p->x, p->y, p->w, p->h, v, up, y0, y1);
        return;
    }
    if (p->fn == RV_PF_SIGNATURE) {     /* segments, lit to the signature: green, amber, red; the rest their dim own */
        int n = (p->w - 2) / 4, lit = (n * v + 50) / 100;
        for (k = 0; k < n; k++) {
            unsigned char hi = k * 10 < n * 4 ? 70 : k * 10 < n * 7 ? 115 : 99;
            unsigned char lo = k * 10 < n * 4 ? 65 : k * 10 < n * 7 ? 112 : 96;
            gfx_fill(p->x + 2 + k * 4, p->y + 3, 3, p->h - 6 > 1 ? p->h - 6 : 1, k < lit ? hi : lo, y0, y1);
        }
        return;
    }
    c = p->fn == RV_PF_SHIELD_BAR ? shield_colour(v) : p->fn == RV_PF_ARMOR_BAR ? armour_colour(v)
        : p->fn == RV_PF_HULL_BAR || p->fn == RV_PF_RANGE_BAR ? level(v) : 126;
    len = (int)((long)inner * v / 100);
    if (len <= 0)
        return;
    if (up)
        gfx_fill(p->x + 1, p->y + p->h - 1 - len, p->w - 2, len, c, y0, y1);
    else
        gfx_fill(p->x + 1, p->y + 1, len, p->h - 2 > 0 ? p->h - 2 : 1, c, y0, y1);
}

static void km(char *s, long v)
{
    if (v < 0) strcpy(s, "-");
    else if (v < 10000) sprintf(s, "%ld.%ldK", v / 1000, v / 100 % 10);
    else sprintf(s, "%ldK", v / 1000);
}

/* a readout's lines now */
static int readout(const RvPanelControl __far *p, char *a, char *b)   /* a: 32, b: 16 */
{
    char k[12];
    b[0] = 0;
    switch (p->fn) {
    case RV_PF_HEAT: sprintf(a, "%d%%", g_sys.heat); break;
    case RV_PF_SIGNATURE: sprintf(a, "%d%%", g_sys.signature); break;
    case RV_PF_AUX_NAV:
        sprintf(a, "NAV %d", g_sys.nav + 1);
        km(b, g_sys.nav_km);
        break;
    case RV_PF_TARGET_ID:
        if (g_sys.target < 0) {
            strcpy(a, "NO TGT");
        } else {
            sprintf(a, "%.8Fs", (const char __far *)g_sys.target_name);
            km(k, g_sys.target_km);
            strcpy(b, k);
        }
        break;
    default: a[0] = 0;
    }
    return 1;
}

/* one or two lines inside a box: the body font when both fit it, else the small one, cut to the box's width */
static void fit_text(int x, int yb, int w, int h, char *a, char *b, int y0, int y1)
{
    int big = gfx_font_height(FONT_BODY), lines = b[0] ? 2 : 1, k = (w - 4) / 4, y;
    if (h >= big * lines + 2 && gfx_text_width(FONT_BODY, a) <= w - 4 && gfx_text_width(FONT_BODY, b) <= w - 4) {
        y = yb + (h - big * lines) / 2;
        gfx_text(FONT_BODY, x + 2 + (w - 4 - gfx_text_width(FONT_BODY, a)) / 2, y, a, RV_TEXT_AMBER, y0, y1);
        if (b[0])
            gfx_text(FONT_BODY, x + 2 + (w - 4 - gfx_text_width(FONT_BODY, b)) / 2, y + big, b, RV_TEXT_AMBER, y0, y1);
        return;
    }
    if (lines == 2 && h < 13) {         /* (too short for two: one, both, if they fit) */
        if (w < 4 * (int)(strlen(a) + strlen(b)) + 8)
            b[0] = 0;
        else {
            strcat(a, " ");
            strcat(a, b);
            b[0] = 0;
        }
        lines = 1;
    }
    if (k >= 0 && k < 15) {             /* (cut to the box) */
        a[k] = 0;
        b[k] = 0;
    }
    y = yb + (h - 6 * lines + 1) / 2;
    tiny(x + (w - 4 * (int)strlen(a)) / 2 + 1, y, a, UI_AMBER4, y0, y1);
    if (lines == 2)
        tiny(x + (w - 4 * (int)strlen(b)) / 2 + 1, y + 6, b, UI_AMBER4, y0, y1);
}

static void text(const RvPanelControl __far *p, const Reading __far *r, int y0, int y1)
{
    char a[32], b[16];
    _fstrcpy((char __far *)a, r->a);
    _fstrcpy((char __far *)b, r->b);
    fit_text(p->x, p->y, p->w, p->h, a, b, y0, y1);
}

/* a map's pixels in its colours: the survival display's regions by the shields' and the armour's levels, the
 * ship grey; the heat picture by the heat (as last taken) */
static void map(int i, const RvPanelControl __far *p, const Reading __far *r, int y0, int y1)
{
    unsigned char __far *src;
    int x, y, ya = p->y > y0 ? p->y : y0, yb = p->y + p->h < y1 ? p->y + p->h : y1;
    if (!g_map[i] || ya >= yb)
        return;
    if (p->fn == RV_PF_SURVIVAL && r->page) {   /* its other pages (the hull and the damage; the nav): text */
        text(p, r, y0, y1);
        return;
    }
    if (p->fn == RV_PF_SURVIVAL) {      /* (its colours: sv_colours, made when its reading changed) */
        for (y = ya; y < yb; y++) {
            unsigned char __far *d = vid_row(y) + p->x;
            src = g_map[i] + (unsigned)(y - p->y) * p->w;
            for (x = 0; x < p->w; x++)
                if (src[x])
                    d[x] = g_svcol[src[x]];
        }
    } else {                            /* the heat: a pixel's own heat times the ship's, through the ramp */
        long k = (long)r->v[0] * (HEAT_STEPS - 1);
        for (y = ya; y < yb; y++) {
            unsigned char __far *d = vid_row(y) + p->x;
            src = g_map[i] + (unsigned)(y - p->y) * p->w;
            for (x = 0; x < p->w; x++)
                if (src[x])
                    d[x] = g_heat_ramp[(unsigned)(src[x] * k / 25500L)];
        }
    }
}

/* --- the frame --- */

/* a display's reading now */
static void read(const RvPanelControl __far *p, const CockpitState *st, Reading *r)
{
    char a[32];
    memset(r, 0, sizeof *r);
    switch (p->kind) {
    case RV_PK_NEEDLE:
        if (p->fn == RV_PF_CLOCK_DIAL) {
            long t = (long)(clock240() / 240);
            r->v[0] = (short)(t % 60 * 6);
            r->v[1] = (short)(t / 60 % 60 * 6);
        } else {
            r->v[0] = (short)needle_angle(p, st);
        }
        break;
    case RV_PK_BAR:
        r->v[0] = (short)bar_value(p, st);
        break;
    case RV_PK_TEXT:
        readout(p, a, r->b);
        a[15] = 0;
        strcpy(r->a, a);
        break;
    case RV_PK_MAP:
        if (p->fn == RV_PF_SURVIVAL) {
            r->page = g_sys.surv_page;
            if (r->page == 1) {
                unsigned long d = st->damage;
                int n = 0;
                for (; d; d >>= 1)
                    n += (int)(d & 1);
                sprintf(r->a, "HULL %d%%", g_sys.hull);
                sprintf(r->b, "DMG %d", n);
            } else if (r->page == 2) {
                sprintf(r->a, "NAV %d", g_sys.nav + 1);
                km(r->b, g_sys.nav_km);
            } else {
                r->v[0] = (short)clamp100(st->shield_f);
                r->v[1] = (short)clamp100(st->shield_r);
                r->v[2] = (short)clamp100(st->armor[0]);
                r->v[3] = (short)clamp100(st->armor[1]);
                r->v[4] = (short)clamp100(st->armor[2]);
                r->v[5] = (short)clamp100(st->armor[3]);
            }
        } else {
            r->v[0] = (short)g_sys.heat_shown;
        }
        break;
    }
}

static int is_display(const RvPanelControl __far *p)
{
    return p->kind == RV_PK_NEEDLE || p->kind == RV_PK_BAR || p->kind == RV_PK_TEXT || p->kind == RV_PK_MAP;
}

static void mark(const RvPanelControl __far *p)
{
    if (p->kind == RV_PK_NEEDLE)
        vid_dirty(p->y / 2 - p->w - 2, p->y / 2 + p->w + 3);
    else
        vid_dirty(p->y - 2, p->y + p->h + 2);
}

void panel_dirty(const CockpitState *st)
{
    int i, k;
    Reading r;
    /* the lamps, knobs and switches: every frame */
    for (i = 0; i < g_n; i++) {
        const RvPanelControl __far *p = &g_pc[i];
        int s;
        if (is_display(p))
            continue;
        s = state(p, st);
        if (s != g_shown[i]) {
            g_shown[i] = (unsigned char)s;
            mark(p);
        }
    }
    /* the displays: one a frame, in turn (every one the first frame) */
    for (k = 0; k < g_n; k++) {
        if (!g_read_all) {
            g_turn = (g_turn + 1) % g_n;
            i = g_turn;
        } else {
            i = k;
        }
        if (!is_display(&g_pc[i]))
            continue;
        read(&g_pc[i], st, &r);
        if (_fmemcmp(&g_rd[i], (const void __far *)&r, sizeof r)) {
            _fmemcpy(&g_rd[i], (const void __far *)&r, sizeof r);
            if (g_pc[i].fn == RV_PF_SURVIVAL)
                sv_colours(&g_rd[i]);
            mark(&g_pc[i]);
        }
        if (!g_read_all)
            break;
    }
    g_read_all = 0;
}

void panel_draw(const CockpitState *st, int y0, int y1)
{
    int i;
    (void)st;
    for (i = 0; i < g_n; i++) {
        const RvPanelControl __far *p = &g_pc[i];
        int s = g_shown[i];
        switch (p->kind) {
        case RV_PK_NEEDLE:
            if (y1 > p->y / 2 - p->w - 2 && y0 < p->y / 2 + p->w + 3)
                needle(p, &g_rd[i], y0, y1);
            break;
        case RV_PK_BAR:
            if (y1 > p->y && y0 < p->y + p->h)
                bar(p, g_rd[i].v[0], y0, y1);
            break;
        case RV_PK_TEXT:
            if (y1 > p->y && y0 < p->y + p->h)
                text(p, &g_rd[i], y0, y1);
            break;
        case RV_PK_MAP:
            map(i, p, &g_rd[i], y0, y1);
            break;
        default:                        /* a state the base does not show: its sprite */
            if (s != p->painted && s < p->nstates && p->sprite != 0xFFFF)
                gfx_sprite(p->sprite + s, 0, 0, y0, y1);
        }
    }
}

int panel_at(int x, int y, int *fn, int *arg)
{
    int i;
    for (i = 0; i < g_n; i++) {
        const RvPanelControl __far *p = &g_pc[i];
        int px = p->x, py = p->y, w = p->w, h = p->h;
        if (p->kind == RV_PK_NEEDLE) {
            px = p->x / 2 - p->w;
            py = p->y / 2 - p->w;
            w = h = 2 * p->w;
        }
        if (x >= px - 2 && x < px + w + 2 && y >= py - 2 && y < py + h + 2) {
            *fn = p->fn;
            *arg = p->arg;
            return 1;
        }
    }
    return 0;
}
