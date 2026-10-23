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

void panel_load(const RvCockpit __far *c)
{
    const RvAsset __far *a;
    g_n = 0;
    if (!c || c->panel == 0xFFFF || !c->npanel)
        return;
    a = dat_asset(c->panel);
    g_n = c->npanel < RV_PANEL ? c->npanel : RV_PANEL;
    dat_read(a->at, g_pc, (unsigned long)g_n * sizeof(RvPanelControl));
    _fmemset(g_shown, 0xFF, sizeof g_shown);
}

void panel_unload(void)
{
    g_n = 0;
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

static void needle(const RvPanelControl __far *p, const CockpitState *st, int y0, int y1)
{
    if (p->fn == RV_PF_CLOCK_DIAL) {
        long s = (long)(clock240() / 240);
        hand(p->x, p->y, (int)(s / 60 % 60) * 6, p->w * 6 / 10, UI_GREY5, y0, y1);
        hand(p->x, p->y, (int)(s % 60) * 6, p->w, UI_WHITE, y0, y1);
    } else {
        hand(p->x, p->y, needle_angle(p, st), p->w, UI_WHITE, y0, y1);
    }
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
    case RV_PF_POWER_BAR: return st->damage >> 9 & 1 ? 35 : 100;
    case RV_PF_ENGINE_BAR: return pct(st->speed, st->max_speed);
    case RV_PF_SPEED_BAR: return pct(st->speed, st->max_speed);
    case RV_PF_RANGE_BAR: return g_sys.target_km < 0 ? 0 : 100 - pct(g_sys.target_km, 4000L << g_sys.radar_range);
    }
    return 0;
}

static void bar(const RvPanelControl __far *p, int v, int y0, int y1)
{
    int len;
    if (p->flags & 1) {
        len = (int)((long)(p->h - 2) * v / 100);
        if (len > 0)
            gfx_fill(p->x + 1, p->y + p->h - 1 - len, p->w - 2, len, level(v), y0, y1);
    } else {
        len = (int)((long)(p->w - 2) * v / 100);
        if (len > 0)
            gfx_fill(p->x + 1, p->y + 1, len, p->h - 2 > 0 ? p->h - 2 : 1, level(v), y0, y1);
    }
}

static void km(char *s, long v)
{
    if (v < 0) strcpy(s, "-");
    else if (v < 10000) sprintf(s, "%ld.%ldK", v / 1000, v / 100 % 10);
    else sprintf(s, "%ldK", v / 1000);
}

/* a readout's lines now */
static int readout(const RvPanelControl __far *p, char *a, char *b)
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

static unsigned char hash(const char *a, const char *b)
{
    unsigned char h = 0;
    for (; *a; a++) h = (unsigned char)(h * 31 + *a);
    for (; *b; b++) h = (unsigned char)(h * 31 + *b);
    return h;
}

static void text(const RvPanelControl __far *p, int y0, int y1)
{
    char a[16], b[16];
    int big = gfx_font_height(FONT_BODY), lines;
    readout(p, a, b);
    lines = b[0] ? 2 : 1;
    if (p->h >= big * lines + 2) {
        int y = p->y + (p->h - big * lines) / 2;
        gfx_text(FONT_BODY, p->x + 3, y, a, RV_TEXT_AMBER, y0, y1);
        if (b[0])
            gfx_text(FONT_BODY, p->x + 3, y + big, b, RV_TEXT_AMBER, y0, y1);
    } else {
        int y = p->y + (p->h - 6 * lines + 1) / 2;
        if (lines == 2 && p->h < 13 && p->w >= 4 * (int)(strlen(a) + strlen(b)) + 6) {   /* (one line, both) */
            strcat(a, " ");
            strcat(a, b);
            lines = 1;
            y = p->y + (p->h - 5) / 2;
        }
        tiny(p->x + 2, y, a, UI_AMBER4, y0, y1);
        if (lines == 2)
            tiny(p->x + 2, y + 6, b, UI_AMBER4, y0, y1);
    }
}

/* --- the frame --- */

static int now_shown(const RvPanelControl __far *p, const CockpitState *st)
{
    char a[16], b[16];
    switch (p->kind) {
    case RV_PK_NEEDLE:
        return p->fn == RV_PF_CLOCK_DIAL ? (int)(clock240() / 240 % 240) : (needle_angle(p, st) / 2) & 0xFF;
    case RV_PK_BAR:
        return bar_value(p, st);
    case RV_PK_TEXT:
        readout(p, a, b);
        return hash(a, b);
    }
    return state(p, st);
}

void panel_dirty(const CockpitState *st)
{
    int i;
    for (i = 0; i < g_n; i++) {
        const RvPanelControl __far *p = &g_pc[i];
        int s = now_shown(p, st);
        if (s == g_shown[i])
            continue;
        g_shown[i] = (unsigned char)s;
        if (p->kind == RV_PK_NEEDLE)
            vid_dirty(p->y / 2 - p->w - 2, p->y / 2 + p->w + 3);
        else
            vid_dirty(p->y - 2, p->y + p->h + 2);
    }
}

void panel_draw(const CockpitState *st, int y0, int y1)
{
    int i;
    for (i = 0; i < g_n; i++) {
        const RvPanelControl __far *p = &g_pc[i];
        int s = g_shown[i];
        switch (p->kind) {
        case RV_PK_NEEDLE:
            if (y1 > p->y / 2 - p->w - 2 && y0 < p->y / 2 + p->w + 3)
                needle(p, st, y0, y1);
            break;
        case RV_PK_BAR:
            if (y1 > p->y && y0 < p->y + p->h)
                bar(p, s, y0, y1);
            break;
        case RV_PK_TEXT:
            if (y1 > p->y && y0 < p->y + p->h)
                text(p, y0, y1);
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
