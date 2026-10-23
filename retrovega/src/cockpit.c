/* cockpit.c: the cockpit's picture (docs/engine.md, "Flight").
 *
 * A band of rows is drawn back to front: the base picture restored from XMS
 * (one move a band: the opening is black in it), the world inside the
 * opening's row extents, the struts the base paints inside those extents,
 * then the instruments: the survival display (shields fore and aft, armour
 * on four sides), speed and fuel, the radar's contacts, the MFDs, the
 * throttle slid by the set speed, the joystick at one of nine places, and
 * the damage overlays whose bits are set, each with its control when it is
 * attached to one; the panel's lamps, knobs, needles and readouts (panel.c)
 * over the instruments.  cockpit_dirty marks the rows a frame changes: the
 * opening and the radar always, the rest only when their values moved. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "rvhooks.h"
#include "cockpit.h"
#include "panel.h"

static RvCockpit __far *g_cp;
static short __far *g_ext;
static int g_ext_y0, g_ext_rows;
static CockpitState g_last;
static int g_last_valid;
static int g_cockpit_index = -1;
static unsigned char __far g_last_page;   /* the survival display's page last drawn */

#define THROTTLE_TRAVEL 14              /* pixels the throttle slides over the speed range */
#define STICK_DX 4
#define STICK_DY 3

const RvCockpit __far *cockpit_record(void) { return g_cp; }

const short __far *cockpit_extents(int *y0, int *rows)
{
    *y0 = g_ext_y0;
    *rows = g_ext_rows;
    return g_ext;
}

int cockpit_load(int ship_type)
{
    const RvDatHeader __far *h = dat_header();
    unsigned i;
    cockpit_unload();
    for (i = 0; i < h->ncockpits; i++) {
        const RvCockpit __far *c = dat_cockpit(i);
        if (c->ship_type == ship_type) {
            const RvAsset __far *ex = dat_asset(c->extents);
            g_cp = (RvCockpit __far *)_fmalloc(sizeof(RvCockpit));
            RV_ASSERT(g_cp != 0);
            _fmemcpy(g_cp, c, sizeof(RvCockpit));
            g_ext_y0 = c->open_y0;
            g_ext_rows = c->open_y1 - c->open_y0;
            g_ext = (short __far *)_fmalloc((size_t)ex->size + 2);
            RV_ASSERT(g_ext != 0 && ex->size == 4UL * g_ext_rows);
            dat_read(ex->at, g_ext, ex->size);
            g_cockpit_index = (int)i;   /* (the palette goes in with cockpit_palette, once the flight's loading is done) */
            g_last_valid = 0;
            panel_load(g_cp);
            rv_log("cockpit: %.8Fs for ship type %d, rows %d-%d open, %d damage layers", (char __far *)c->key,
                   ship_type, c->open_y0, c->open_y1, c->nlayers);
            return 1;
        }
    }
    rv_log("cockpit: none for ship type %d", ship_type);
    return 0;
}

void cockpit_palette(void)
{
    if (g_cp)
        gfx_use_palette(g_cp->palette);
}

void cockpit_unload(void)
{
    panel_unload();
    if (g_cp) _ffree(g_cp);
    if (g_ext) _ffree(g_ext);
    g_cp = 0;
    g_ext = 0;
    g_cockpit_index = -1;
}

/* --- what a frame changes --- */

static void dirty_rect(const RvRect __far *r)
{
    if (r->w)
        vid_dirty(r->y, r->y + r->h);
}

static void dirty_sprite(unsigned a, int dy0, int dy1)
{
    const RvAsset __far *as;
    if (a == 0xFFFF)
        return;
    as = dat_asset(a);
    vid_dirty(as->y + (dy0 < dy1 ? dy0 : dy1), as->y + as->h + (dy0 > dy1 ? dy0 : dy1));
}

static int throttle_dy(const CockpitState *st)
{
    return st->max_speed ? -(int)((long)st->set_speed * THROTTLE_TRAVEL / st->max_speed) : 0;
}

void cockpit_dirty(const CockpitState *st)
{
    const RvCockpit __far *c = g_cp;
    unsigned long changed;
    int i;
    if (!c)
        return;
    vid_dirty(c->open_y0, c->open_y1);
    vid_dirty(c->radar_y - c->radar_r, c->radar_y + c->radar_r + 1);
    panel_dirty(st);
    if (!g_last_valid) {
        vid_dirty(0, SCR_H);
        g_last = *st;
        g_last_valid = 1;
        return;
    }
    if (st->shield_f != g_last.shield_f || st->shield_r != g_last.shield_r || memcmp(st->armor, g_last.armor, sizeof st->armor)
        || g_sys.surv_page != g_last_page || (g_sys.surv_page && (frameCounter & 15) == 0))
        dirty_rect(&c->survival);
    g_last_page = g_sys.surv_page;
    if (st->speed != g_last.speed || st->set_speed != g_last.set_speed)
        dirty_rect(&c->speed);
    if (st->fuel != g_last.fuel)
        dirty_rect(&c->fuel);
    for (i = 0; i < 2; i++) {
        int k, diff = 0;
        for (k = 0; k < 4; k++)
            diff |= st->mfd_text[i][k] != g_last.mfd_text[i][k]
                    || (st->mfd_text[i][k] && strcmp(st->mfd_text[i][k], g_last.mfd_text[i][k]) != 0);
        if (diff || st->mfd_draw)
            dirty_rect(&c->mfd[i]);
    }
    if (st->set_speed != g_last.set_speed || st->max_speed != g_last.max_speed)
        dirty_sprite(c->throttle.sprite, throttle_dy(&g_last), throttle_dy(st));
    if (st->stick_x != g_last.stick_x || st->stick_y != g_last.stick_y)
        dirty_sprite(c->joystick.sprite, g_last.stick_y * STICK_DY, st->stick_y * STICK_DY);
    changed = st->damage ^ g_last.damage;
    for (i = 0; i < RV_CP_LAYERS; i++)
        if ((changed >> i) & 1)
            dirty_sprite(c->layer[i].sprite, 0, 0);
    g_last = *st;
}

/* --- the instruments --- */

static unsigned char level_colour(int pct)
{
    return pct > 66 ? UI_HILITE : pct > 33 ? UI_AMBER4 : UI_RED;
}

/* a bar of the rect's width: the part up to pct lit */
static void bar(const RvRect __far *r, int pct, int y0, int y1)
{
    int w = (int)((long)r->w * (pct < 0 ? 0 : pct > 100 ? 100 : pct) / 100);
    gfx_fill(r->x, r->y, r->w, r->h, UI_GREY1, y0, y1);
    if (w > 0)
        gfx_fill(r->x, r->y, w, r->h, level_colour(pct), y0, y1);
}

/* shields as arcs fore and aft, armour as four bars round a hull */
static void survival(const RvRect __far *r, const CockpitState *st, int y0, int y1)
{
    int cx = r->x + r->w / 2, cy = r->y + r->h / 2, hw = r->w * 3 / 10, hh = r->h * 3 / 10, i, k;
    int pct[4];
    if (!r->w || y1 <= r->y || y0 >= r->y + r->h)
        return;
    if (g_sys.surv_page) {              /* (its other pages: the hull and how many systems are damaged; the nav) */
        char a[16], b[16];
        unsigned long d = st->damage;
        int n = 0, h = gfx_font_height(FONT_BODY);
        for (; d; d >>= 1)
            n += (int)(d & 1);
        if (g_sys.surv_page == 1) {
            sprintf(a, "HULL %d%%", g_sys.hull);
            sprintf(b, "DMG %d", n);
        } else {
            sprintf(a, "NAV %d", g_sys.nav + 1);
            if (g_sys.nav_km < 0) strcpy(b, "-");
            else sprintf(b, "%ldK", g_sys.nav_km / 1000);
        }
        gfx_text(FONT_BODY, r->x + 2, r->y + (r->h - 2 * h) / 2, a, RV_TEXT_AMBER, y0, y1);
        gfx_text(FONT_BODY, r->x + 2, r->y + (r->h - 2 * h) / 2 + h, b, RV_TEXT_AMBER, y0, y1);
        return;
    }
    /* the hull */
    gfx_frame(cx - hw / 2, cy - hh / 2, hw, hh, UI_GREY4, y0, y1);
    /* armour: front and rear bars, left and right bars, shortened as they wear */
    for (k = 0; k < 4; k++)
        pct[k] = st->armor[k] < 0 ? 0 : st->armor[k] > 100 ? 100 : st->armor[k];
    i = (int)((long)hw * pct[0] / 100);
    gfx_fill(cx - i / 2, cy - hh / 2 - 4, i, 3, level_colour(pct[0]), y0, y1);
    i = (int)((long)hw * pct[1] / 100);
    gfx_fill(cx - i / 2, cy + hh / 2 + 1, i, 3, level_colour(pct[1]), y0, y1);
    i = (int)((long)hh * pct[2] / 100);
    gfx_fill(cx - hw / 2 - 4, cy - i / 2, 3, i, level_colour(pct[2]), y0, y1);
    i = (int)((long)hh * pct[3] / 100);
    gfx_fill(cx + hw / 2 + 1, cy - i / 2, 3, i, level_colour(pct[3]), y0, y1);
    /* shields: an arc of eight segments each side, lit from the middle outwards */
    for (k = 0; k < 2; k++) {
        int pc = k ? st->shield_r : st->shield_f, ry = hh / 2 + 7, rx = hw / 2 + 7, s;
        unsigned char col = level_colour(pc);
        if (pc < 0) pc = 0;
        for (s = 0; s < 8; s++) {
            /* segment s spans angles (s, s+1) of 8 over the half; lit when within pc of the middle */
            int from_mid = s < 4 ? 3 - s : s - 4;         /* 0 nearest the middle, 3 the ends */
            int xa, ya, xb, yb;
            if (from_mid * 25 >= pc)
                continue;
            xa = cx + (int)((long)rx * (s * 2 - 8) / 8);
            xb = cx + (int)((long)rx * ((s + 1) * 2 - 8) / 8);
            ya = cy + (k ? 1 : -1) * (int)((long)ry * (rx - (xa - cx < 0 ? cx - xa : xa - cx)) / rx);
            yb = cy + (k ? 1 : -1) * (int)((long)ry * (rx - (xb - cx < 0 ? cx - xb : xb - cx)) / rx);
            ya = k ? (ya > cy + ry ? cy + ry : ya) : (ya < cy - ry ? cy - ry : ya);
            yb = k ? (yb > cy + ry ? cy + ry : yb) : (yb < cy - ry ? cy - ry : yb);
            gfx_line(xa, ya, xb, yb, col, 0, y0, y1);
            gfx_line(xa, ya + (k ? 1 : -1), xb, yb + (k ? 1 : -1), col, 0, y0, y1);
        }
    }
}

static void radar(const RvCockpit __far *c, const CockpitState *st, int y0, int y1)
{
    int i, r = c->radar_r;
    if (y1 <= c->radar_y - r || y0 >= c->radar_y + r + 1)
        return;
    gfx_circle(c->radar_x, c->radar_y, r - 1, UI_GREY2, 1, y0, y1);
    gfx_fill(c->radar_x - 2, c->radar_y, 5, 1, UI_GREY4, y0, y1);
    gfx_fill(c->radar_x, c->radar_y - 2, 1, 5, UI_GREY4, y0, y1);
    for (i = 0; i < st->ncontacts && i < CP_CONTACTS; i++) {
        int x = c->radar_x + (int)((long)st->contact[i].x * (r - 2) / 256);
        int y = c->radar_y + (int)((long)st->contact[i].y * (r - 2) / 256);
        gfx_fill(x - 1, y - 1, 2, 2, st->contact[i].colour, y0, y1);
    }
}

static void mfd(const RvRect __far *r, int which, const CockpitState *st, int y0, int y1)
{
    int k, y = r->y + 3;
    if (!r->w || y1 <= r->y || y0 >= r->y + r->h)
        return;
    if (st->mfd_draw)
        st->mfd_draw(which, r, y0, y1);
    for (k = 0; k < 4 && st->mfd_text[which][k]; k++) {
        if (y + gfx_font_height(FONT_BODY) > r->y + r->h)
            break;
        gfx_text(FONT_BODY, r->x + 4, y, st->mfd_text[which][k], RV_TEXT_AMBER, y0, y1);
        y += gfx_font_height(FONT_BODY) + 1;
    }
}

static void speed_fuel(const RvCockpit __far *c, const CockpitState *st, int y0, int y1)
{
    char s[24];
    const RvRect __far *r = &c->speed;
    if (r->w && y1 > r->y && y0 < r->y + r->h) {
        if (r->h >= gfx_font_height(FONT_BODY)) {
            sprintf(s, "%d", st->speed);
            gfx_text(FONT_BODY, r->x + 2, r->y + (r->h - gfx_font_height(FONT_BODY)) / 2, s, RV_TEXT_AMBER, y0, y1);
        } else {
            bar(r, st->max_speed ? (int)((long)st->speed * 100 / st->max_speed) : 0, y0, y1);
        }
    }
    r = &c->fuel;
    if (r->w >= 12 && y1 > r->y && y0 < r->y + r->h) {   /* (a lamp-sized one is the panel's fuel lamp) */
        if (r->h >= gfx_font_height(FONT_BODY)) {
            sprintf(s, "%d%%", st->fuel);
            gfx_text(FONT_BODY, r->x + 2, r->y + (r->h - gfx_font_height(FONT_BODY)) / 2, s, RV_TEXT_AMBER, y0, y1);
        } else {
            bar(r, st->fuel, y0, y1);
        }
    }
}

static void damage_layers(const RvCockpit __far *c, const CockpitState *st, int attach, int dx, int dy, int y0, int y1)
{
    int i;
    for (i = 0; i < RV_CP_LAYERS; i++)
        if (((st->damage >> i) & 1) && c->layer[i].sprite != 0xFFFF && c->layer[i].attach == attach)
            gfx_sprite(c->layer[i].sprite, dx, dy, y0, y1);
}

void cockpit_draw(int y0, int y1, const CockpitState *st, void (*world)(int y0, int y1))
{
    const RvCockpit __far *c = g_cp;
    int tdy, sdx, sdy;
    if (!c) {
        gfx_fill(0, 0, SCR_W, SCR_H, UI_BLACK, y0, y1);
        if (world)
            world(y0, y1);
        return;
    }
    gfx_image(c->base, 0, 0, y0, y1);
    if (world && y1 > c->open_y0 && y0 < c->open_y1)
        world(y0, y1);
    if (c->struts != 0xFFFF)
        gfx_sprite(c->struts, 0, 0, y0, y1);
    survival(&c->survival, st, y0, y1);
    speed_fuel(c, st, y0, y1);
    radar(c, st, y0, y1);
    mfd(&c->mfd[0], 0, st, y0, y1);
    mfd(&c->mfd[1], 1, st, y0, y1);
    panel_draw(st, y0, y1);             /* (the lamps, the dials, the readouts: panel.c) */
    damage_layers(c, st, 0, 0, 0, y0, y1);
    tdy = throttle_dy(st);
    gfx_sprite(c->throttle.sprite, 0, tdy, y0, y1);
    damage_layers(c, st, 1, 0, tdy, y0, y1);
    sdx = st->stick_x * STICK_DX;
    sdy = st->stick_y * STICK_DY;
    gfx_sprite(c->joystick.sprite, sdx, sdy, y0, y1);
    damage_layers(c, st, 2, sdx, sdy, y0, y1);
}
