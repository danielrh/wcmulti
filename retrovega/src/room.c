/* room.c: a room on screen (docs/engine.md): its picture, who sits where,
 * the actor, the hotspots, the pointer and its label.
 *
 * Nothing is drawn twice: changes mark rows dirty (the pointer's old and new
 * rows, a pilot turning, the actor's next frame), and scene_draw redraws
 * just those rows, back to front, then flips them.  The game draws its own
 * things (text, boards, menus) through the overlay hook, called for every
 * band drawn. */
#include <string.h>
#include "rv.h"
#include "room.h"

Scene g_scene;

static const RvRoom __far *room(void) { return dat_room(g_scene.room); }

static void dirty_asset(unsigned a)
{
    const RvAsset __far *as = dat_asset(a);
    vid_dirty(as->y, as->y + as->h);
}

static unsigned pose_asset(int seat, int which)
{
    const RvRoom __far *r = room();
    int i;
    for (i = 0; i < r->nposes; i++)
        if (r->pose[i].seat == seat && r->pose[i].pilot == g_scene.seat[seat])
            return which ? r->pose[i].face : r->pose[i].talk;
    return 0xFFFF;
}

static void dirty_pointer(void)
{
    vid_dirty(g_scene.py - 2, g_scene.py + 20);
}

void scene_enter(int room_id, const unsigned char *seats)
{
    const RvRoom __far *r;
    int i;
    memset(&g_scene, 0, sizeof g_scene);
    g_scene.room = room_id;
    r = room();
    for (i = 0; i < 3; i++) {
        g_scene.seat[i] = seats ? seats[i] : 0xFF;
        if (g_scene.seat[i] != 0xFF && pose_asset(i, 0) == 0xFFFF) {
            rv_log("room %s: no art of pilot %u in seat %d", r->key, g_scene.seat[i], i);
            g_scene.seat[i] = 0xFF;
        }
    }
    g_scene.hover_kind = HOVER_NONE;
    g_scene.actor_next = ticks();
    g_scene.glance_at = ticks() + 180;
    gfx_use_palette(r->palette);
    vid_dirty(0, SCR_H);
    roomEntered(room_id);
}

/* what is under the pointer */
static void find_hover(int x, int y, int *kind, int *index)
{
    const RvRoom __far *r = room();
    int i;
    for (i = 0; i < 3; i++) {
        unsigned a;
        if (g_scene.seat[i] == 0xFF)
            continue;
        a = pose_asset(i, g_scene.turn[i] >= 2);
        if (gfx_sprite_hit(a, 0, 0, x, y)) {
            *kind = HOVER_SEAT;
            *index = i;
            return;
        }
    }
    if (r->has_actor && x >= r->actor.x && y >= r->actor.y && x < r->actor.x + r->actor.w
        && y < r->actor.y + r->actor.h) {
        *kind = HOVER_ACTOR;
        *index = 0;
        return;
    }
    for (i = 0; i < r->nhot; i++) {
        const RvHot __far *h = &r->hot[i];
        if (x >= h->x && y >= h->y && x < h->x + h->w && y < h->y + h->h) {
            *kind = HOVER_HOT;
            *index = i;
            return;
        }
    }
    *kind = HOVER_NONE;
    *index = 0;
}

static void set_label(const char __far *s)
{
    if (_fstrcmp(g_scene.label, s) == 0)
        return;
    _fstrncpy(g_scene.label, s, sizeof g_scene.label - 1);
    vid_dirty(SCR_H - LABEL_H, SCR_H);
}

/* the pointer's centre on a hotspot, for Tab */
static void hot_centre(int kind, int index, int *x, int *y)
{
    const RvRoom __far *r = room();
    if (kind == HOVER_SEAT) {
        const RvAsset __far *as = dat_asset(pose_asset(index, 0));
        *x = as->x + as->w / 2;
        *y = as->y + as->h / 3;
    } else if (kind == HOVER_ACTOR) {
        *x = r->actor.x + r->actor.w / 2;
        *y = r->actor.y + r->actor.h / 3;
    } else {
        *x = r->hot[index].x + r->hot[index].w / 2;
        *y = r->hot[index].y + r->hot[index].h / 2;
    }
}

/* Tab and Shift+Tab: the next target to the right (or left), wrapping round */
static void tab_move(int dir)
{
    const RvRoom __far *r = room();
    int best = -1, bkind = 0, first = -1, fkind = 0, i, k;
    long bkey = 0, fkey = 0, here;
    /* targets in order of their centres, left to right, then top to bottom */
    if (g_scene.hover_kind != HOVER_NONE) {
        int x, y;
        hot_centre(g_scene.hover_kind, g_scene.hover_index, &x, &y);
        here = (long)x * 1024 + y;
    } else {
        here = (long)g_scene.px * 1024 + g_scene.py;
    }
    for (k = HOVER_SEAT; k <= HOVER_HOT; k++) {
        int n = k == HOVER_SEAT ? 3 : k == HOVER_ACTOR ? r->has_actor : r->nhot;
        for (i = 0; i < n; i++) {
            int x, y;
            long key;
            if (k == HOVER_SEAT && g_scene.seat[i] == 0xFF)
                continue;
            hot_centre(k, i, &x, &y);
            key = ((long)x * 1024 + y) * dir;
            if (first < 0 || key < fkey)
                first = i, fkind = k, fkey = key;     /* where the round starts again */
            if (key > here * dir && (best < 0 || key < bkey))
                best = i, bkind = k, bkey = key;
        }
    }
    if (best < 0)
        best = first, bkind = fkind;
    if (best >= 0) {
        int x, y;
        hot_centre(bkind, best, &x, &y);
        rv_log("tab: to %d,%d", x, y);
        in_warp(x, y);
        dirty_pointer();
        g_scene.px = x;
        g_scene.py = y;
        dirty_pointer();
    }
}

int scene_update(const Input *in, SceneAction *act)
{
    const RvRoom __far *r = room();
    int kind, index, i;
    unsigned long now = ticks();
    act->kind = ACTION_NONE;
    if (g_scene.no_room) {
        if (in->x != g_scene.px || in->y != g_scene.py) {
            dirty_pointer();
            g_scene.px = in->x;
            g_scene.py = in->y;
            dirty_pointer();
        }
        if (in->clicked || in->key == KEY_ENTER)
            act->kind = ACTION_HOT, act->index = -1, act->action = RV_ACT_NONE, act->arg = 0;
        return act->kind != ACTION_NONE;
    }
    if (in->key == KEY_TAB || in->key == KEY_BACKTAB)
        tab_move(in->key == KEY_TAB ? 1 : -1);
    else if (in->x != g_scene.px || in->y != g_scene.py) {
        dirty_pointer();
        g_scene.px = in->x;
        g_scene.py = in->y;
        dirty_pointer();
    }
    find_hover(g_scene.px, g_scene.py, &kind, &index);
    if (kind != g_scene.hover_kind || index != g_scene.hover_index) {
        g_scene.hover_kind = kind;
        g_scene.hover_index = index;
        g_scene.pointer = kind == HOVER_HOT && r->hot[index].action == RV_ACT_GOTO;
        if (kind == HOVER_HOT)
            set_label(r->hot[index].label);
        else if (kind == HOVER_SEAT)
            set_label(g_people_names[g_scene.seat[index]]);
        else if (kind == HOVER_ACTOR)
            set_label(g_people_names[r->actor.person]);
        else
            set_label("");
        dirty_pointer();
    }
    /* pilots turn towards the pointer, a step every two ticks */
    if (now >= g_scene.turn_next) {
        g_scene.turn_next = now + 2;
        for (i = 0; i < 3; i++) {
            int want = (kind == HOVER_SEAT && index == i) || g_scene.facing == i + 1 ? 2 : 0;
            if (g_scene.seat[i] == 0xFF || g_scene.turn[i] == want)
                continue;
            g_scene.turn[i] += g_scene.turn[i] < want ? 1 : -1;
            dirty_asset(pose_asset(i, 0));
            dirty_asset(pose_asset(i, 1));
        }
    }
    /* the actor: its idle loop, the hover frame, now and then a glance */
    if (r->has_actor) {
        const RvActor __far *a = &r->actor;
        int frame = g_scene.actor_frame;
        if (kind == HOVER_ACTOR || g_scene.facing == 9) {
            frame = a->hover;
        } else if (now >= g_scene.glance_at) {
            frame = a->glance;
            if (now >= g_scene.glance_at + 27) {
                g_scene.glance_at = now + 160 + (now % 90);
                g_scene.actor_next = now;
            }
        } else if (now >= g_scene.actor_next) {
            g_scene.actor_step = (g_scene.actor_step + 1) % a->nidle;
            frame = a->idle[g_scene.actor_step][0];
            g_scene.actor_next = now + a->idle[g_scene.actor_step][1];
        } else if (frame == a->hover || frame == a->glance) {
            frame = a->idle[g_scene.actor_step][0];
        }
        if (frame != g_scene.actor_frame) {
            g_scene.actor_frame = frame;
            vid_dirty(a->y, a->y + a->h);
        }
    }
    if (in->clicked || in->key == KEY_ENTER) {
        act->kind = kind == HOVER_HOT ? ACTION_HOT : kind == HOVER_SEAT ? ACTION_SEAT
                  : kind == HOVER_ACTOR ? ACTION_ACTOR : ACTION_NONE;
        act->index = index;
        if (kind == HOVER_HOT) {
            act->action = r->hot[index].action;
            act->arg = r->hot[index].arg;
        } else if (kind == HOVER_SEAT) {
            act->arg = g_scene.seat[index];
        } else if (kind == HOVER_ACTOR) {
            act->arg = r->actor.person;
        }
    }
    return act->kind != ACTION_NONE;
}

static void draw_pointer(int y0, int y1)
{
    if (!g_scene.hide_pointer) {
        const RvDatHeader __far *h = dat_header();
        gfx_sprite(g_scene.pointer ? h->ptr_door : h->ptr_arrow, g_scene.px, g_scene.py, y0, y1);
    }
}

static void draw_band(int y0, int y1)
{
    const RvRoom __far *r = room();
    int i;
    if (g_scene.full) {
        g_scene.full(y0, y1);
        draw_pointer(y0, y1);
        return;
    }
    gfx_image(r->back, 0, 0, y0, y1);
    if (g_scene.underlay)
        g_scene.underlay(y0, y1);
    for (i = 0; i < 3; i++) {
        if (g_scene.seat[i] == 0xFF)
            continue;
        if (g_scene.turn[i] == 0)
            gfx_sprite(pose_asset(i, 0), 0, 0, y0, y1);
        else if (g_scene.turn[i] == 2)
            gfx_sprite(pose_asset(i, 1), 0, 0, y0, y1);
        else {
            gfx_sprite(pose_asset(i, 0), 0, 0, y0, y1);
            gfx_sprite_dither(pose_asset(i, 1), 0, 0, y0, y1, 1);
        }
    }
    if (r->has_actor)
        gfx_image(r->actor.frame0 + g_scene.actor_frame, AT_ASSET, AT_ASSET, y0, y1);
    if (g_scene.overlay)
        g_scene.overlay(y0, y1);
    if (g_scene.label[0] && y1 > SCR_H - LABEL_H) {
        int w = gfx_text_width(FONT_BODY, g_scene.label);
        gfx_shade(0, SCR_H - LABEL_H, SCR_W, LABEL_H, y0, y1);
        gfx_text(FONT_BODY, (SCR_W - w) / 2, SCR_H - LABEL_H + 3, g_scene.label, RV_TEXT_AMBER | 0x10, y0, y1);
    }
    draw_pointer(y0, y1);
}

void scene_draw(void)
{
    int y0, y1;
    while (vid_dirty_band(&y0, &y1)) {
        draw_band(y0, y1);
        vid_flip();
    }
}
