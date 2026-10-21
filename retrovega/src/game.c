/* game.c: the first slice of the game (docs/engine.md).
 *
 * The player walks the carrier's rooms (bar, lounge, quarters, locker),
 * talks to whoever sits there, saves at the bunk, and goes through the
 * bar's hatch to the briefing room for the next mission: the XO speaks
 * over the situation display, then the launch deck, Kit and the fighter,
 * and the flight (flight.c: for now the drawing benchmark, 20 seconds in the
 * cockpit; the mission is still won 80% of the time at random).  The
 * debrief, the campaign's next move, and at the end "You win" or "You lose". */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "room.h"
#include "campaign.h"
#include "lipsync.h"
#include "music.h"
#include "flight.h"
#include "game.h"

enum { MODE_ROOM, MODE_TALK, MODE_BRIEF, MODE_XOCLOSE, MODE_DECK, MODE_DEBRIEF, MODE_BUNK,
       MODE_NAME, MODE_LOG, MODE_MESSAGE, MODE_ENDING, MODE_BOARD, MODE_QUIT };

static int g_mode, g_back_mode;
static RvMission __far g_m;
static unsigned long g_now;
static char g_msg[400];
static int g_won;
static int g_base;                          /* the ship the player lives on: 0 the carrier */
static int change_base(int base);
static void mission_load_assets(void);
static void mission_free_assets(void);
int g_winrate = 80;                         /* percent: a launch's chance, until there is flight */

/* the speech in progress: shapes, when it began, the text */
#define MAX_STEPS 1600
static unsigned char __far g_lip[MAX_STEPS];
static int g_nlip;
static const char __far *g_said;
static int g_speaker;                       /* people.csv row */
static int g_mouth = -1, g_eyes = -1, g_gesture, g_shown_chars = -1;

static const char *ship_name(int type)
{
    static char name[RV_NAME];
    const RvShipType __far *t = ship_type(type);
    if (!t)
        return "fighter";
    _fstrncpy(name, t->name, sizeof name - 1);
    return name;
}

static unsigned rnd(void)
{
    g_camp.seed = g_camp.seed * 1103515245UL + 12345UL;
    return (unsigned)(g_camp.seed >> 16) & 0x7FFF;
}

/* The talk speed, the bunk menu's choice (kept in RVSETUP.CFG): a percentage
 * of the first pace, lipsync.c's 100 ms a shape.  The user found that pace
 * too slow to read; "normal" is twice it, and "slow" is it. */
static const unsigned g_talk_pcts[] = { 100, 200, 300, 400 };
static const char *g_talk_names[] = { "slow", "normal", "fast", "very fast" };
static int g_talk_speed = 1;
#define SETUP_FILE "RVSETUP.CFG"

static void setup_load(void)
{
    unsigned char b[8];
    FILE *f = fopen(SETUP_FILE, "rb");
    if (!f)
        return;
    if (fread(b, 1, 8, f) == 8 && !memcmp(b, "RVCFG1", 6) && b[6] < RV_COUNT(g_talk_pcts))
        g_talk_speed = b[6];
    fclose(f);
}

static void setup_save(void)
{
    char b[8];
    FILE *f = fopen(SETUP_FILE, "wb");
    memcpy(b, "RVCFG1", 6);
    b[6] = (char)g_talk_speed;
    b[7] = 0;
    if (f) {
        fwrite(b, 1, 8, f);
        fclose(f);
    }
    rv_log("setup: talk speed %s", g_talk_names[g_talk_speed]);
}

/* speech runs on the music clock (240 Hz): fine enough for a shape every 25 ms */
static unsigned long g_speech_t0;

static int speaking_step(void)
{
    /* clock ticks x 1000/240 ms x pct/100, in LIP_STEP_MS steps */
    return (int)((clock240() - g_speech_t0) * g_talk_pcts[g_talk_speed] / (24UL * LIP_STEP_MS));
}

static void say(int person, const char __far *text)
{
    g_speaker = person;
    g_said = text;
    g_nlip = lip_steps(text, g_lip, MAX_STEPS);
    g_speech_t0 = clock240();
    rv_log("talk: %d shapes, %lu ms at %s", g_nlip, (unsigned long)g_nlip * LIP_STEP_MS * 100 / g_talk_pcts[g_talk_speed],
           g_talk_names[g_talk_speed]);
    g_mouth = g_eyes = g_shown_chars = -1;
    g_gesture = 0;
}

static int speech_done(void) { return speaking_step() >= g_nlip; }

static int mouth_now(void)
{
    int s = speaking_step();
    return s < g_nlip ? g_lip[s] : 0;
}

/* --- panels and messages --- */

static void panel(int x, int y, int w, int h, int y0, int y1)
{
    gfx_fill(x, y, w, h, UI_PANEL, y0, y1);
    gfx_frame(x, y, w, h, UI_PANEL_EDGE, y0, y1);
    gfx_frame(x + 1, y + 1, w - 2, h - 2, UI_BLACK, y0, y1);
}

static void message_overlay(int y0, int y1)
{
    panel(110, 120, 420, 150, y0, y1);
    gfx_wrap(FONT_BODY, 126, 134, 388, g_msg, RV_TEXT_WHITE, 4, y0, y1);
    gfx_text(FONT_BODY, 126, 248, "click to close", RV_TEXT_GREY, y0, y1);
}

static void room_overlay(int y0, int y1);

static void message(const char *text)
{
    strncpy(g_msg, text, sizeof g_msg - 1);
    g_back_mode = g_mode;
    g_mode = MODE_MESSAGE;
    g_scene.overlay = message_overlay;
    vid_dirty(110, 280);
}

/* --- rooms --- */

static unsigned long g_talk_at;
static int g_talk_line = -1;

static void talk_overlay(int y0, int y1)
{
    const RvTalk __far *t;
    char line[96];
    if (g_talk_line < 0)
        return;
    t = &g_m.talk[g_talk_line];
    sprintf(line, "%.10s: %.60Fs", g_pilots[t->speaker].callsign, t->text);
    gfx_text(FONT_BODY, 16, SCR_H - LABEL_H - 22, line, RV_TEXT_WHITE | 0x10, y0, y1);
}

/* --- the tally board: Tally's chalk --- */

/* the pilots, most splashes first */
static int tally_order(int *order)
{
    int i, k, n = 0;
    for (i = 0; i < (int)g_npilots; i++)
        if (g_pilots[i].role == RV_ROLE_PILOT)
            order[n++] = i;
    for (i = 0; i < n; i++)
        for (k = i + 1; k < n; k++)
            if (g_camp.pilot[order[k]].kills > g_camp.pilot[order[i]].kills) {
                int t = order[i]; order[i] = order[k]; order[k] = t;
            }
    return n;
}

/* the hand's wobble: -range .. range, the same for the same seed and step */
static int wobble(unsigned seed, int k, int range)
{
    unsigned h = seed * 0x9E37u + (unsigned)k * 0x85EBu;
    h ^= h >> 7;
    h *= 0x2C1Bu;
    h ^= h >> 9;
    return (int)(h % (unsigned)(2 * range + 1)) - range;
}

/* a line of chalk too small to read: a hand's ups and downs from the
 * baseline y, w pixels long, x-height xh, a tall stroke now and then; a
 * heading (two words) breaks after three fifths */
static void scribble(int x, int y, int w, int xh, int words, unsigned seed, int y0, int y1)
{
    int k, px = x, py = y, gap = words > 1 ? x + w * 3 / 5 : x + w;
    for (k = 1; px < x + w; k++) {
        int nx = px + 1 + (wobble(seed, k, 1) >= 0);
        int ny = (k & 1) ? y - xh - (wobble(seed, k + 50, 2) == 2 ? 2 : 0) : y + (wobble(seed, k + 90, 1) > 0);
        gfx_chalk(px, py, nx, ny, 1, RV_TEXT_WHITE, y0, y1);
        px = nx;
        py = ny;
        if (px >= gap && px < x + w - 4) {      /* (a word's end) */
            px += 4;
            py = y;
            gap = x + w;
        }
    }
}

/* n tally marks from x: four strokes h tall and `gap` apart, the fifth across them */
static void tally_marks(int x, int y, int h, int gap, int width, unsigned n, unsigned seed, int y0, int y1)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        int m = (int)(i % 5), a = wobble(seed, (int)i, 1), b = wobble(seed, (int)i + 100, 1);
        if (m < 4)
            gfx_chalk(x + m * gap + a, y + b, x + m * gap - a, y + h + b, width, RV_TEXT_WHITE, y0, y1);
        else {
            gfx_chalk(x - gap / 2, y + h * 3 / 4 + a, x + 3 * gap + gap / 2, y + h / 4 + b, width, RV_TEXT_WHITE, y0, y1);
            x += 6 * gap;
        }
    }
}

/* in the bar: a few scribbles, a heading and the top four with their marks
 * (the user's call: the board's text was out of keeping; click it to read it) */
static void chalkboard(int y0, int y1)
{
    const RvRoom __far *r = dat_room(g_scene.room);
    int b, i, order[RV_PILOTS], n;
    for (b = 0; b < 4; b++) {
        const RvBoard __far *bd = &r->board[b];
        int cx = bd->x + bd->w / 2;
        if (bd->w == 0 || bd->kind != 0 || y1 <= bd->y || y0 >= bd->y + bd->h)
            continue;
        n = tally_order(order);
        scribble(cx - 20, bd->y + 9, 40, 3, 2, 1, y0, y1);
        gfx_chalk(cx - 23, bd->y + 12, cx + 22, bd->y + 11, 1, RV_TEXT_WHITE, y0, y1);
        for (i = 0; i < 4 && i < n; i++) {
            unsigned k = g_camp.pilot[order[i]].kills;
            scribble(bd->x + 8, bd->y + 22 + i * 8, 10 + (int)strlen(g_pilots[order[i]].callsign) * 2, 2, 1,
                     7 + order[i], y0, y1);
            tally_marks(bd->x + 42, bd->y + 18 + i * 8, 4, 2, 1, k < 15 ? k : 15, 31 + order[i], y0, y1);
        }
    }
}

/* the board up close: every pilot, in Tally's hand (or the title font, if
 * there was no room for the hand) */
static int g_hand = FONT_CHALK;

static void board_full(int y0, int y1)
{
    const RvDatHeader __far *h = dat_header();
    int sx = h->chalk_slate[0], sy = h->chalk_slate[1], sw = h->chalk_slate[2];
    int order[RV_PILOTS], n = tally_order(order), i, w;
    gfx_image(h->chalk_back, 0, 0, y0, y1);
    w = gfx_text_width(g_hand, "Splash Count");
    gfx_text(g_hand, sx + (sw - w) / 2, sy + 2, "Splash Count", RV_TEXT_WHITE | TEXT_CHALK, y0, y1);
    gfx_chalk(sx + (sw - w) / 2 - 8, sy + 41, sx + (sw + w) / 2 + 8, sy + 39, 3, RV_TEXT_WHITE, y0, y1);
    for (i = 0; i < n && i < 8; i++) {
        int ry = sy + 46 + i * 29;
        unsigned k = g_camp.pilot[order[i]].kills;
        char num[8];
        gfx_text(g_hand, sx + 28, ry, g_pilots[order[i]].callsign, RV_TEXT_WHITE | TEXT_CHALK, y0, y1);
        tally_marks(sx + 176, ry + 9, 22, 7, 3, k < 30 ? k : 30, 31 + order[i], y0, y1);
        sprintf(num, "%u", k);
        gfx_text(g_hand, sx + sw - 28 - gfx_text_width(g_hand, num), ry, num, RV_TEXT_WHITE | TEXT_CHALK, y0, y1);
    }
    gfx_text(FONT_BODY, 14, SCR_H - 18, "click to close", RV_TEXT_GREY | 0x10, y0, y1);
}

static void open_board(void)
{
    vid_fade(0);
    g_hand = gfx_font_chalk(1) ? FONT_CHALK : FONT_TITLE;
    gfx_use_palette(dat_header()->chalk_pal);
    g_scene.full = board_full;
    g_scene.no_room = 1;
    g_mode = MODE_BOARD;
    rv_log("board: up close%s", g_hand == FONT_CHALK ? "" : " (no room for the chalk hand: the title font)");
    vid_dirty(0, SCR_H);
    scene_draw();
    vid_fade(64);
}

static void close_board(void)
{
    g_scene.full = 0;
    g_scene.no_room = 0;
    vid_fade(0);
    gfx_font_chalk(0);
    gfx_use_palette(dat_room(g_scene.room)->palette);
    g_mode = MODE_ROOM;
    vid_dirty(0, SCR_H);
    scene_draw();
    vid_fade(64);
}

static void room_overlay(int y0, int y1)
{
    if (g_scene.room == RV_R_BAR)
        chalkboard(y0, y1);
    talk_overlay(y0, y1);
}

/* each room's song: the quarters and the locker, the lounge, the bar (and the rest) */
static int room_song(int room)
{
    return room == RV_R_QUARTERS || room == RV_R_LOCKER ? RV_MUSIC_QUARTERS
         : room == RV_R_LOUNGE ? RV_MUSIC_LOUNGE : RV_MUSIC_BAR;
}

static void enter_room(int room)
{
    unsigned char seats[3] = { 0xFF, 0xFF, 0xFF };
    if (room == RV_R_BAR || room == RV_R_LOUNGE)
        _fmemcpy(seats, g_m.seat[room == RV_R_BAR ? RV_ROOM_BAR : RV_ROOM_LOUNGE], 3);
    vid_fade(0);
    scene_enter(room, seats);
    g_scene.overlay = room_overlay;
    g_mode = MODE_ROOM;
    g_talk_line = -1;
    g_talk_at = 0;
    music_cue(room_song(room), 250);
    scene_draw();
    vid_fade(64);
}

/* the table talk: this room's lines in turn, as subtitles */
static void ambient_talk(void)
{
    int i, room = g_scene.room == RV_R_BAR ? RV_ROOM_BAR : g_scene.room == RV_R_LOUNGE ? RV_ROOM_LOUNGE : -1;
    if (room < 0 || g_now < g_talk_at)
        return;
    for (i = 1; i <= (int)g_m.ntalk; i++) {
        int k = (g_talk_line + i) % (g_m.ntalk ? g_m.ntalk : 1);
        if (g_m.ntalk && g_m.talk[k].room == room) {
            g_talk_line = k;
            /* a second, then as long as reading it takes at the talk speed (8 letters a second at "slow") */
            g_talk_at = g_now + 18 + (unsigned long)_fstrlen(g_m.talk[k].text) * 227 / g_talk_pcts[g_talk_speed];
            vid_dirty(SCR_H - LABEL_H - 24, SCR_H - LABEL_H);
            return;
        }
    }
}

/* --- close-ups: a person speaking --- */

#define FACE_X 24
#define FACE_Y 62

static int g_close_room, g_close_back;      /* the close-up's room, and the mode it goes back to */

/* the speech so far; of a long one (a briefing), only the sentence being said */
static const char *said_so_far(void)
{
    static char shown[RV_BRIEFING];
    int n = _fstrlen(g_said), k = speech_done() ? n : (int)((long)n * speaking_step() / (g_nlip ? g_nlip : 1)), from = 0;
    if (n > 240)
        for (from = k - 1; from > 0; from--)
            if (g_said[from] == ' ' && (g_said[from - 1] == '.' || g_said[from - 1] == '!' || g_said[from - 1] == '?')) {
                from++;
                break;
            }
    if (from < 0 || from > k)
        from = 0;
    _fstrncpy(shown, g_said + from, k - from);
    shown[k - from] = 0;
    return shown;
}

/* a close-up: the speaker's face over the room, blurred (the XO's too: her
 * gesture sheet twitched at this size; the user's call, 2026-10-21) */
static int eyes_now(void)
{
    return (g_now / 4) % 17 == 0 ? 2 : (g_now / 4) % 17 == 1 ? 1 : 0;   /* a blink every 4 s */
}

static void talk_full(int y0, int y1)
{
    const RvRoom __far *r = dat_room(g_close_room);
    const RvPerson __far *p = dat_person(g_speaker);
    int mouth = mouth_now(), eyes = eyes_now();
    char title[40];
    gfx_image(r->closeup_back, 0, 0, y0, y1);
    gfx_sprite(p->face, FACE_X, FACE_Y, y0, y1);
    gfx_image(p->eyes0 + eyes, FACE_X + dat_asset(p->eyes0)->x, FACE_Y + dat_asset(p->eyes0)->y, y0, y1);
    gfx_image(p->mouth0 + mouth, FACE_X + dat_asset(p->mouth0)->x, FACE_Y + dat_asset(p->mouth0)->y, y0, y1);
    if (g_pilots[g_speaker].role == RV_ROLE_XO) {        /* (no callsign: her name and rank) */
        int w = gfx_text_width(FONT_TITLE, g_pilots[g_speaker].name);
        gfx_text(FONT_TITLE, w > SCR_W - 412 ? SCR_W - 12 - w : 400, 130, g_pilots[g_speaker].name, RV_TEXT_AMBER, y0, y1);
        gfx_text(FONT_BODY, 400, 160, "Executive Officer", RV_TEXT_WHITE, y0, y1);
    } else {
        sprintf(title, "\"%.10s\"", g_pilots[g_speaker].callsign);
        gfx_text(FONT_TITLE, 400, 130, title, RV_TEXT_AMBER, y0, y1);
        gfx_text(FONT_BODY, 400, 160, g_pilots[g_speaker].name, RV_TEXT_WHITE, y0, y1);
    }
    gfx_wrap(FONT_BODY, 400, 196, 220, said_so_far(), RV_TEXT_WHITE, 4, y0, y1);
}

/* what moves in a close-up: the mouth, the eyes, the words */
static void face_tick(void)
{
    static int said = -1;
    int step = speech_done() ? g_nlip : speaking_step();
    if (mouth_now() != g_mouth || eyes_now() != g_eyes || step != said) {
        g_mouth = mouth_now();
        g_eyes = eyes_now();
        said = step;
        vid_dirty(FACE_Y, SCR_H);
    }
}

/* the close-up of whoever is speaking (g_speaker), in this room */
static int show_face(void)
{
    const RvRoom __far *r = dat_room(g_scene.room);
    if (r->closeup_part == 0xFFFF || g_speaker < 0)
        return 0;
    g_close_room = g_scene.room;
    g_close_back = g_mode;
    vid_fade(0);
    gfx_use_part(r->closeup_part, 1);
    gfx_use_part(dat_person(g_speaker)->palpart, 0);
    g_scene.full = talk_full;
    g_scene.no_room = 1;
    vid_dirty(0, SCR_H);
    scene_draw();
    vid_fade(64);
    return 1;
}

static void open_talk(int person, const char __far *line)
{
    if (dat_room(g_scene.room)->closeup_part == 0xFFFF)
        return;
    say(person, line);
    if (!show_face())
        return;
    g_mode = MODE_TALK;
    music_cue(room_song(g_close_room), 150);     /* (the room's own: the same song goes on) */
}

static void close_talk(void)
{
    g_scene.full = 0;
    g_scene.no_room = 0;
    vid_fade(0);
    gfx_use_palette(dat_room(g_scene.room)->palette);
    g_mode = g_close_back;
    vid_dirty(0, SCR_H);
    scene_draw();
    vid_fade(64);
}

/* a pilot's line: theirs in this room's talk if they have one, else their own */
static const char __far *line_of(int person)
{
    int i;
    for (i = 0; i < (int)g_m.ntalk; i++)
        if (g_m.talk[i].speaker == person)
            return g_m.talk[i].text;
    return g_pilots[person].line;
}

/* --- the briefing and the debrief: the XO at the lectern --- */

static int g_stage, g_xo_close;
static unsigned long g_stage_at;

/* the people.csv row of whoever has this role (the XO), or -1 */
static int person_of(int role)
{
    int i;
    for (i = 0; i < (int)g_npilots; i++)
        if (g_pilots[i].role == role)
            return i;
    return -1;
}

static void situation_display(int y0, int y1)
{
    const RvRoom __far *r = dat_room(RV_R_BRIEFING);
    const RvBoard __far *b = 0;
    int i, x0, z0, x1, z1, mx, my, mw, mh;
    long sx, sz;
    for (i = 0; i < 4; i++)
        if (r->board[i].kind == 2 && r->board[i].w)
            b = &r->board[i];
    if (!b || y1 <= b->y || y0 >= b->y + b->h)
        return;
    gfx_fill(b->x, b->y, b->w, b->h, UI_PANEL, y0, y1);
    gfx_frame(b->x, b->y, b->w, b->h, UI_HILITE, y0, y1);
    /* fit the carrier (0, 0), the nav points and the fields */
    x0 = x1 = z0 = z1 = 0;
    for (i = 0; i < g_m.nnav; i++) {
        if (g_m.nav[i].x < x0) x0 = g_m.nav[i].x;
        if (g_m.nav[i].x > x1) x1 = g_m.nav[i].x;
        if (g_m.nav[i].z < z0) z0 = g_m.nav[i].z;
        if (g_m.nav[i].z > z1) z1 = g_m.nav[i].z;
    }
    for (i = 0; i < g_m.nfield; i++) {
        const RvField __far *f = &g_m.field[i];
        if (f->x - f->radius < x0) x0 = f->x - f->radius;
        if (f->x + f->radius > x1) x1 = f->x + f->radius;
        if (f->z - f->radius < z0) z0 = f->z - f->radius;
        if (f->z + f->radius > z1) z1 = f->z + f->radius;
    }
    mx = b->x + 14; my = b->y + 14; mw = b->w - 28; mh = b->h - 28;
    sx = x1 - x0 + 1; sz = z1 - z0 + 1;
    if (sx * mh < sz * mw)
        sx = sz * mw / mh;
    else
        sz = sx * mh / mw;
#define MAPX(v) (mx + (int)(((long)(v) - x0) * mw / sx))
#define MAPY(v) (my + mh - (int)(((long)(v) - z0) * mh / sz))
#define MAPR(v) ((int)((long)(v) * mw / sx))
    for (i = 1; i < 4; i++) {
        gfx_line(b->x + 1, b->y + i * b->h / 4, b->x + b->w - 2, b->y + i * b->h / 4, UI_PANEL_EDGE, 0, y0, y1);
        gfx_line(b->x + i * b->w / 4, b->y + 1, b->x + i * b->w / 4, b->y + b->h - 2, UI_PANEL_EDGE, 0, y0, y1);
    }
    for (i = 0; i < g_m.nfield; i++) {
        const RvField __far *f = &g_m.field[i];
        int c = f->type == RV_FIELD_MINE ? UI_RED : UI_GREY4, rr = MAPR(f->radius);
        gfx_circle(MAPX(f->x), MAPY(f->z), rr < 3 ? 3 : rr, c, 1, y0, y1);
        gfx_text(FONT_BODY, MAPX(f->x) - 8, MAPY(f->z) - 6, f->type == RV_FIELD_MINE ? "MINES" : "DEBRIS",
                 RV_TEXT_GREY, y0, y1);
    }
    {
        int px = MAPX(0), py = MAPY(0);
        for (i = 0; i <= g_m.nnav; i++) {
            int nx = i < g_m.nnav ? MAPX(g_m.nav[i].x) : MAPX(0), ny = i < g_m.nnav ? MAPY(g_m.nav[i].z) : MAPY(0);
            gfx_line(px, py, nx, ny, UI_HILITE, 3, y0, y1);
            px = nx; py = ny;
        }
    }
    gfx_line(MAPX(0) - 4, MAPY(0) + 3, MAPX(0), MAPY(0) - 4, UI_WHITE, 0, y0, y1);
    gfx_line(MAPX(0), MAPY(0) - 4, MAPX(0) + 4, MAPY(0) + 3, UI_WHITE, 0, y0, y1);
    gfx_line(MAPX(0) - 4, MAPY(0) + 3, MAPX(0) + 4, MAPY(0) + 3, UI_WHITE, 0, y0, y1);
    for (i = 0; i < g_m.nnav; i++) {
        char s[8];
        int k, foes = 0;
        for (k = 0; k < g_m.nship; k++)
            if (g_m.ship[k].nav == g_m.nav[i].id && g_m.ship[k].side == 1)
                foes += g_m.ship[k].count;
        gfx_circle(MAPX(g_m.nav[i].x), MAPY(g_m.nav[i].z), 4, UI_AMBER4, 0, y0, y1);
        sprintf(s, "NAV %u", g_m.nav[i].id);
        gfx_text(FONT_BODY, MAPX(g_m.nav[i].x) + 7, MAPY(g_m.nav[i].z) - 7, s, RV_TEXT_AMBER, y0, y1);
        for (k = 0; k < foes && k < 6; k++)
            gfx_fill(MAPX(g_m.nav[i].x) + 7 + k * 5, MAPY(g_m.nav[i].z) + 8, 3, 3, UI_RED, y0, y1);
    }
#undef MAPX
#undef MAPY
#undef MAPR
}

static void xo_underlay(int y0, int y1)
{
    const RvSpeaker __far *s = &dat_room(RV_R_BRIEFING)->speaker;
    int mouth = s->mouth_of_viseme[mouth_now()];
    gfx_sprite(s->cell0 + g_gesture * s->nmouths + mouth, 0, 0, y0, y1);
    if (g_stage > 0 && s->stage[g_stage - 1] != 0xFFFF)
        gfx_sprite(s->stage[g_stage - 1], 0, 0, y0, y1);
}

static void brief_overlay(int y0, int y1)
{
    static char shown[RV_BRIEFING];
    char head[80];
    int n = _fstrlen(g_said), k = speech_done() ? n : (int)((long)n * speaking_step() / (g_nlip ? g_nlip : 1));
    if (g_mode == MODE_BRIEF)
        situation_display(y0, y1);
    sprintf(head, "%s  //  %.40Fs", g_mode == MODE_BRIEF ? "BRIEFING" : g_won ? "DEBRIEF: WON" : "DEBRIEF: LOST",
            (char __far *)g_m.title);
    gfx_text(FONT_TITLE, 14, 6, head, RV_TEXT_AMBER | 0x10, y0, y1);
    if (y1 > 290) {
        gfx_shade(0, 292, SCR_W, 86, y0, y1);
        _fstrncpy(shown, g_said, k);
        shown[k] = 0;
        gfx_wrap(FONT_BODY, 14, 298, SCR_W - 28, shown, RV_TEXT_WHITE, 3, y0, y1);
    }
}

static void start_briefing(int debrief)
{
    vid_fade(0);
    scene_enter(RV_R_BRIEFING, 0);
    g_scene.underlay = xo_underlay;
    g_scene.overlay = brief_overlay;
    g_mode = debrief ? MODE_DEBRIEF : MODE_BRIEF;
    g_stage = 0;
    g_xo_close = 0;
    say(person_of(RV_ROLE_XO), debrief ? (g_won ? g_m.win : g_m.lose) : g_m.briefing);
    if (!debrief)
        briefingStarted();
    music_cue(debrief ? (g_won ? RV_MUSIC_WIN : RV_MUSIC_LOSE) : RV_MUSIC_BRIEFING, 250);
    scene_draw();
    vid_fade(64);
}

/* the gesture changes with each sentence: hands, then open and raised in turn */
static void brief_tick(void)
{
    int step = speaking_step(), chars, i, sentences = 0, g;
    int n = _fstrlen(g_said);
    chars = speech_done() ? n : (int)((long)n * step / (g_nlip ? g_nlip : 1));
    for (i = 0; i < chars; i++)
        if (g_said[i] == '.' || g_said[i] == '!' || g_said[i] == '?')
            sentences++;
    g = sentences == 0 ? 0 : 1 + (sentences - 1) % 2;
    if (speech_done())
        g = 0;
    if (g_xo_close) {                           /* (her close-up: face_tick) */
        g_gesture = g;
        return;
    }
    if (g != g_gesture || mouth_now() != g_mouth) {
        const RvSpeaker __far *s = &dat_room(RV_R_BRIEFING)->speaker;
        const RvAsset __far *a = dat_asset(s->cell0);
        g_gesture = g;
        g_mouth = mouth_now();
        vid_dirty(a->y, a->y + a->h);
    }
    if (chars != g_shown_chars) {
        g_shown_chars = chars;
        vid_dirty(292, 378);
    }
}

/* --- the deck: Kit and the fighter --- */

static int g_clip, g_clip_frame, g_deck_ship;
static unsigned long g_clip_at;

static void deck_underlay(int y0, int y1)
{
    const RvDeck __far *d = &dat_room(RV_R_DECK)->deck;
    gfx_image(d->ship[g_deck_ship].plate, 0, 0, y0, y1);
    gfx_sprite(d->front, 0, 0, y0, y1);
    gfx_sprite(d->clip_first[g_clip] + g_clip_frame, 0, 0, y0, y1);
}

static void deck_overlay(int y0, int y1)
{
    char s[64];
    sprintf(s, "%.40Fs  //  your fighter: %s", (char __far *)g_m.title, ship_name(g_camp.player_ship));
    gfx_text(FONT_BODY, 14, 8, s, RV_TEXT_AMBER | 0x10, y0, y1);
}

static void enter_deck(void)
{
    const RvDeck __far *d = &dat_room(RV_R_DECK)->deck;
    int i;
    vid_fade(0);
    scene_enter(RV_R_DECK, 0);
    g_deck_ship = 0;
    for (i = 0; i < d->nships; i++)
        if (d->ship[i].ship_type == g_camp.player_ship)
            g_deck_ship = i;
    g_scene.underlay = deck_underlay;
    g_scene.overlay = deck_overlay;
    g_clip = g_clip_frame = 0;
    g_clip_at = g_now;
    g_mode = MODE_DECK;
    music_cue(RV_MUSIC_LAUNCH, 250);
    scene_draw();
    vid_fade(64);
}

static void deck_tick(void)
{
    const RvDeck __far *d = &dat_room(RV_R_DECK)->deck;
    if (g_now < g_clip_at)
        return;
    g_clip_at = g_now + d->clip_ticks[g_clip];
    if (++g_clip_frame >= d->clip_n[g_clip]) {
        g_clip_frame = 0;
        g_clip = (g_clip + 1) % RV_CLIPS;
    }
    vid_dirty(d->patch_y, d->patch_y + d->patch_h);
}

static void customize(void)
{
    const RvDeck __far *d = &dat_room(RV_R_DECK)->deck;
    char s[64];
    g_deck_ship = (g_deck_ship + 1) % d->nships;
    g_camp.player_ship = d->ship[g_deck_ship].ship_type;
    g_clip = 2;                           /* the ready nod */
    g_clip_frame = 0;
    vid_dirty(0, SCR_H);
    sprintf(s, "Kit: the %s it is", ship_name(g_camp.player_ship));
    strncpy(g_scene.label, s, sizeof g_scene.label - 1);
}

/* --- the flight --- */

static void launch(void)
{
    mission_load_assets();
    missionStarting();
    g_scene.label[0] = 0;
    flight_fly(&g_m);
    g_won = (int)(rnd() % 100) < g_winrate;        /* until there is combat: the roll */
    missionEnded();
    mission_free_assets();
    rv_log("mission %.16Fs %s", (char __far *)g_m.id, g_won ? "won" : "lost");
    g_now = ticks();
    start_briefing(1);
}

/* --- the bunks --- */

static int g_bunk_sel = -1;
int g_can_quit = 1;                     /* RV.EXE /noquit clears it: no Q in the bunk menu (the page) */
static int g_quit_asked;
static RvSave __far g_bunk_info[BUNKS];
static int g_bunk_full[BUNKS];
static char g_name[24];

/* the bunk menu's row of talk speeds: where each choice is, for drawing and clicking */
#define SPEED_Y 292
#define SPEED_LABEL "TALK SPEED (T)"
static int speed_x(int i, int *w)
{
    int k, x = 110 + gfx_text_width(FONT_BODY, SPEED_LABEL) + 20;
    for (k = 0; k < i; k++)
        x += gfx_text_width(FONT_BODY, g_talk_names[k]) + 22;
    *w = gfx_text_width(FONT_BODY, g_talk_names[i]);
    return x;
}

static void talk_speed_row(int y0, int y1)
{
    int i;
    gfx_text(FONT_BODY, 110, SPEED_Y, SPEED_LABEL, RV_TEXT_AMBER, y0, y1);
    for (i = 0; i < (int)RV_COUNT(g_talk_pcts); i++) {
        int w, x = speed_x(i, &w);
        if (i == g_talk_speed)
            gfx_fill(x - 6, SPEED_Y - 3, w + 12, 20, UI_PANEL_EDGE, y0, y1);
        gfx_text(FONT_BODY, x, SPEED_Y, g_talk_names[i], i == g_talk_speed ? RV_TEXT_WHITE : RV_TEXT_GREY, y0, y1);
    }
}

static void bunk_overlay(int y0, int y1)
{
    int i;
    panel(90, 40, 460, 300, y0, y1);
    gfx_text(FONT_TITLE, 106, 50, "YOUR BUNK", RV_TEXT_AMBER, y0, y1);
    for (i = 0; i < BUNKS; i++) {
        char s[80];
        int y = 86 + i * 26;
        if (g_bunk_full[i])
            sprintf(s, "%d  %.22Fs   layer %u, mission %u", i + 1, (char __far *)g_bunk_info[i].name,
                    (unsigned)g_nodes[g_bunk_info[i].node].layer, (unsigned)g_bunk_info[i].mission + 1);
        else
            sprintf(s, "%d  (empty)", i + 1);
        if (i == g_bunk_sel)
            gfx_fill(100, y - 3, 440, 22, UI_PANEL_EDGE, y0, y1);
        gfx_text(FONT_BODY, 110, y, s, i == g_bunk_sel ? RV_TEXT_WHITE : RV_TEXT_GREY, y0, y1);
    }
    if (g_mode != MODE_NAME)
        talk_speed_row(y0, y1);
    gfx_text(FONT_BODY, 110, 318, g_bunk_sel < 0 ? (g_can_quit ? "pick a bunk; Esc to close; Q quits to DOS" : "pick a bunk; Esc to close")
             : g_mode == MODE_NAME ? "name it, then Enter"
             : g_can_quit ? "S save here    L load    Q quit    Esc back" : "S save here    L load    Esc back", RV_TEXT_AMBER, y0, y1);
    if (g_mode == MODE_NAME) {
        char s[40];
        sprintf(s, "name: %s_", g_name);
        gfx_fill(104, SPEED_Y - 4, 432, 22, UI_BLACK, y0, y1);      /* (in the talk speeds' place) */
        gfx_text(FONT_BODY, 110, SPEED_Y, s, RV_TEXT_WHITE, y0, y1);
    }
}

static void open_bunks(void)
{
    int i;
    for (i = 0; i < BUNKS; i++)
        g_bunk_full[i] = save_read(i, &g_bunk_info[i]);
    g_bunk_sel = -1;
    g_mode = MODE_BUNK;
    g_scene.overlay = bunk_overlay;
    vid_dirty(30, 350);
}

static void close_bunks(void)
{
    g_mode = MODE_ROOM;
    g_scene.overlay = room_overlay;
    vid_dirty(30, 350);
}

static void bunk_input(const Input *in)
{
    int row = (in->y - 83) / 26;
    if (in->key == KEY_ESC) {
        if (g_bunk_sel >= 0) g_bunk_sel = -1; else close_bunks();
        vid_dirty(30, 350);
        return;
    }
    if (g_can_quit && in->key == KEY_CHAR && (in->ch == 'q' || in->ch == 'Q') && g_mode != MODE_NAME) {
        g_quit_asked = 1;               /* the one way out of a DOS install: from the bunk, deliberately */
        return;
    }
    /* the talk speed: T for the next, or a click on one */
    if ((in->key == KEY_CHAR && (in->ch == 't' || in->ch == 'T')) || (in->clicked && in->y >= SPEED_Y - 4 && in->y < SPEED_Y + 18)) {
        int i, w, pick = -1;
        if (!in->clicked)
            pick = (g_talk_speed + 1) % (int)RV_COUNT(g_talk_pcts);
        for (i = 0; in->clicked && i < (int)RV_COUNT(g_talk_pcts); i++) {
            int x = speed_x(i, &w);
            if (in->x >= x - 8 && in->x < x + w + 8)
                pick = i;
        }
        if (pick >= 0) {
            g_talk_speed = pick;
            setup_save();
            vid_dirty(30, 350);
        }
        return;
    }
    if (in->key == KEY_UP && g_bunk_sel > 0) g_bunk_sel--, vid_dirty(30, 350);
    if (in->key == KEY_DOWN && g_bunk_sel < BUNKS - 1) g_bunk_sel++, vid_dirty(30, 350);
    if (in->key == KEY_DOWN && g_bunk_sel < 0) g_bunk_sel = 0, vid_dirty(30, 350);
    if (in->clicked && in->x > 100 && in->x < 540 && row >= 0 && row < BUNKS && in->y >= 83) {
        g_bunk_sel = row;
        vid_dirty(30, 350);
    }
    if (g_bunk_sel < 0)
        return;
    if (in->key == KEY_CHAR && (in->ch == 's' || in->ch == 'S')) {
        /* an empty bunk starts with no name; a used one offers its own to keep */
        _fstrcpy(g_name, g_bunk_full[g_bunk_sel] ? g_bunk_info[g_bunk_sel].name : (char __far *)"");
        g_mode = MODE_NAME;
        vid_dirty(30, 350);
    } else if (in->key == KEY_CHAR && (in->ch == 'l' || in->ch == 'L') && g_bunk_full[g_bunk_sel]) {
        if (save_load(g_bunk_sel)) {
            if (g_camp.base != g_base)
                change_base(g_camp.base);
            camp_mission(camp_mission_index(), &g_m);
            camp_load_units(&g_m);
            close_bunks();
            enter_room(RV_R_QUARTERS);
            sprintf(g_msg, "Bunk %d loaded: %s. Layer %u, mission %u.", g_bunk_sel + 1, g_camp.name,
                    g_nodes[g_camp.node].layer, g_camp.mission + 1);
            message(g_msg);
        }
    }
}

static void name_input(const Input *in)
{
    int n = strlen(g_name);
    if (in->key == KEY_ESC) {
        g_mode = MODE_BUNK;
    } else if (in->key == KEY_BACKSPACE && n) {
        g_name[n - 1] = 0;
    } else if (in->key == KEY_CHAR && n < (int)sizeof g_name - 1 && in->ch >= 32 && in->ch < 127) {
        g_name[n] = (char)in->ch;
        g_name[n + 1] = 0;
    } else if (in->key == KEY_ENTER) {
        if (!n)
            sprintf(g_name, "Bunk %d", g_bunk_sel + 1);
        save_write(g_bunk_sel, g_name);
        g_bunk_full[g_bunk_sel] = save_read(g_bunk_sel, &g_bunk_info[g_bunk_sel]);
        g_mode = MODE_BUNK;
    } else {
        return;
    }
    vid_dirty(30, 350);
}

/* --- the campaign log (the desk) --- */

static void log_overlay(int y0, int y1)
{
    int i, k;
    char s[80];
    panel(40, 30, 560, 330, y0, y1);
    gfx_text(FONT_TITLE, 56, 40, "CAMPAIGN LOG", RV_TEXT_AMBER, y0, y1);
    /* the lattice: s at the top, v and l at the bottom */
    for (i = 0; i < (int)g_nnodes; i++) {
        int x = 200 + g_nodes[i].col * 40, y = 84 + (g_nodes[i].layer - 1) * 38;
        int targets[2], t;
        targets[0] = g_nodes[i].good;
        targets[1] = g_nodes[i].bad;
        for (t = 0; t < 2; t++) {
            int tx, ty;
            if (targets[t] == RV_NODE_GOOD) { tx = 160; ty = 84 + 6 * 38; }
            else if (targets[t] == RV_NODE_BAD) { tx = 240; ty = 84 + 6 * 38; }
            else { tx = 200 + g_nodes[targets[t]].col * 40; ty = 84 + (g_nodes[targets[t]].layer - 1) * 38; }
            gfx_line(x, y, tx, ty, t ? UI_RED : UI_HILITE, 2, y0, y1);
        }
    }
    for (i = 0; i < (int)g_nnodes; i++) {
        int x = 200 + g_nodes[i].col * 40, y = 84 + (g_nodes[i].layer - 1) * 38;
        gfx_fill(x - 4, y - 4, 9, 9, i == g_camp.node ? UI_AMBER4 : UI_GREY3, y0, y1);
    }
    gfx_text(FONT_BODY, 152, 84 + 6 * 38 + 6, "V", RV_TEXT_WHITE, y0, y1);
    gfx_text(FONT_BODY, 234, 84 + 6 * 38 + 6, "L", RV_TEXT_WHITE, y0, y1);
    sprintf(s, "Layer %u, mission %u of 4.  Flown: %u.  Trend: %+d", g_nodes[g_camp.node].layer,
            g_camp.mission + 1, g_camp.flown, g_camp.trend);
    gfx_text(FONT_BODY, 300, 84, s, RV_TEXT_WHITE, y0, y1);
    gfx_text(FONT_BODY, 300, 110, "SPLASH COUNT", RV_TEXT_AMBER, y0, y1);
    for (i = 0, k = 0; i < (int)g_npilots; i++) {
        if (g_pilots[i].role != RV_ROLE_PILOT)
            continue;
        sprintf(s, "%-10.10s %3u kills  %2u missions", g_pilots[i].callsign, g_camp.pilot[i].kills,
                g_camp.pilot[i].missions);
        gfx_text(FONT_BODY, 300, 130 + k * 18, s, RV_TEXT_GREY, y0, y1);
        k++;
    }
    gfx_text(FONT_BODY, 56, 336, "click to close", RV_TEXT_GREY, y0, y1);
}

/* --- endings --- */

static void ending_full(int y0, int y1)
{
    char s[80];
    gfx_fill(0, 0, SCR_W, SCR_H, UI_BLACK, y0, y1);
    gfx_text(FONT_TITLE, 250, 150, g_camp.ending == ENDING_GOOD ? "YOU WIN" : "YOU LOSE",
             g_camp.ending == ENDING_GOOD ? RV_TEXT_AMBER : RV_TEXT_WHITE, y0, y1);
    sprintf(s, "%u missions flown.  Press a key.", g_camp.flown);
    gfx_text(FONT_BODY, 210, 200, s, RV_TEXT_GREY, y0, y1);
}

/* --- bases: the ship the player lives on --- */

/* unload this base's rooms and load another's (the music plays on) */
static int change_base(int base)
{
    rv_log("base %d -> %d: %s", g_base, base, dat_base_file(base));
    vid_fade(0);
    dat_close();
    if (!dat_open(dat_base_file(base))) {
        rv_log("base %d: no %s, back to the carrier", base, dat_base_file(base));
        base = 0;
        if (!dat_open(dat_base_file(0)))
            rv_fail(__FILE__, __LINE__, "no RV.DAT", 0, 0);
    }
    gfx_font_load();
    g_base = base;
    g_camp.base = (unsigned char)base;
    baseChanged(base);
    return base;
}

/* --- missions: their own XMS level, freed when they end --- */

static void mission_load_assets(void)
{
    /* the mission's ships, sounds and cockpit come here with flight; the level
     * is taken and given back already, so loading order is right from the start */
    xms_free_level(XMS_MISSION);
    rv_log("mission assets: %lu KB of XMS free", xms_free_kb());
}

static void mission_free_assets(void)
{
    xms_free_level(XMS_MISSION);
}

/* --- the next mission --- */

static void next_mission(void)
{
    camp_mission(camp_mission_index(), &g_m);
    camp_load_units(&g_m);
}

static void hot_action(const SceneAction *a)
{
    switch (a->action) {
    case RV_ACT_GOTO:
        if (a->arg == RV_R_BRIEFING)
            start_briefing(0);
        else
            enter_room(a->arg);
        break;
    case RV_ACT_BUNK: open_bunks(); break;
    case RV_ACT_LOG:
        g_mode = MODE_LOG;
        g_scene.overlay = log_overlay;
        vid_dirty(20, 370);
        break;
    case RV_ACT_SCORES: open_board(); break;
    case RV_ACT_SIMULATOR: message("The simulator is down for refit. Tally's record stands, for now."); break;
    case RV_ACT_MEDALS: message("Your medal case. Medals and ribbons go here as you earn them."); break;
    case RV_ACT_LETTERS: message("Letters from home, read until the folds wore through."); break;
    case RV_ACT_PHOTOS: message("Photographs from before the war."); break;
    case RV_ACT_KILLS: message("Your flight record: kills and missions, one tally mark at a time."); break;
    case RV_ACT_CLOSEUP:
        if (show_face()) {
            g_xo_close = 1;
            g_mode = MODE_XOCLOSE;
        }
        break;
    case RV_ACT_CUSTOMIZE: customize(); break;
    case RV_ACT_LAUNCH: launch(); break;
    }
}

int game_run(void)
{
    Input in;
    SceneAction act;
    g_now = ticks();
    setup_load();
    camp_new();
    next_mission();
    enter_room(RV_R_QUARTERS);
    message("Wildcard, welcome aboard the TCS Vigilance. Your bunk keeps your saves. "
            "The briefing room is through the hatch in the bar.");
    while (g_mode != MODE_QUIT) {
        rv_halt();                      /* once a music tick at most (1/240 s): the host idles between */
        g_now = ticks();
        in_steer(g_mode != MODE_BUNK && g_mode != MODE_NAME);     /* those menus take the arrows */
        in_poll(&in);
        music_poll();
        if (in.key == KEY_F12)
            shot_save("RVSHOT");
        /* Esc never quits (the user's rule, 2026-10-20: it is the browser's key, for leaving full screen);
         * a test script's quit, and the bunk menu's Q where RV.EXE runs without /noquit (a DOS install:
         * the page passes it, where the player would just reload), are the only ways out */
        if ((in.key == KEY_ESC && in.ch == 'q' && script_active()) || g_quit_asked)
            break;
        scene_update(&in, &act);
        switch (g_mode) {
        case MODE_ROOM:
            ambient_talk();
            if (act.kind == ACTION_HOT)
                hot_action(&act);
            else if (act.kind == ACTION_SEAT || act.kind == ACTION_ACTOR)
                open_talk(act.arg, line_of(act.arg));
            break;
        case MODE_MESSAGE:
            if (in.clicked || in.key == KEY_ENTER || in.key == KEY_ESC) {
                g_mode = g_back_mode;
                g_scene.overlay = room_overlay;
                vid_dirty(110, 280);
            }
            break;
        case MODE_TALK:
            face_tick();
            if (in.clicked || in.key == KEY_ENTER || in.key == KEY_ESC)
                close_talk();
            break;
        case MODE_BRIEF:
        case MODE_DEBRIEF:
            brief_tick();
            if (act.kind == ACTION_HOT && act.action == RV_ACT_CLOSEUP) {
                hot_action(&act);
            } else if (speech_done() || in.key == KEY_ESC || (act.kind == ACTION_HOT && act.action == RV_ACT_GOTO)) {
                if (!g_stage_at)
                    g_stage_at = g_now + 18;
                if (g_now >= g_stage_at || in.key == KEY_ESC) {
                    if (g_mode == MODE_BRIEF && g_stage < 2) {
                        g_stage++;
                        g_stage_at = g_now + 7;
                        vid_dirty(100, SCR_H);
                    } else {
                        int brief = g_mode == MODE_BRIEF;
                        g_stage_at = 0;
                        if (brief) {
                            enter_deck();
                        } else if (camp_resolve(g_won), g_camp.ending) {
                            g_scene.full = ending_full;
                            g_scene.no_room = 1;
                            g_mode = MODE_ENDING;
                            music_cue(g_camp.ending == ENDING_GOOD ? RV_MUSIC_WIN : RV_MUSIC_LOSE, 300);
                            vid_dirty(0, SCR_H);
                            rv_log("ending: %s after %u missions", g_camp.ending == ENDING_GOOD ? "victory" : "loss",
                                   g_camp.flown);
                            if (script_active()) {
                                scene_draw();
                                shot_save("ENDING");
                            }
                        } else {
                            next_mission();
                            enter_room(RV_R_BAR);
                        }
                    }
                }
            }
            break;
        case MODE_XOCLOSE:
            brief_tick();
            face_tick();
            if (in.clicked || in.key == KEY_ENTER || in.key == KEY_ESC) {
                g_xo_close = 0;
                close_talk();                       /* (back to the briefing or the debrief) */
            }
            break;
        case MODE_BOARD:
            if (in.clicked || in.key == KEY_ENTER || in.key == KEY_ESC)
                close_board();
            break;
        case MODE_DECK:
            deck_tick();
            if (act.kind == ACTION_HOT)
                hot_action(&act);
            break;
        case MODE_BUNK:
            bunk_input(&in);
            break;
        case MODE_NAME:
            name_input(&in);
            break;
        case MODE_LOG:
            if (in.clicked || in.key == KEY_ESC || in.key == KEY_ENTER) {
                g_mode = MODE_ROOM;
                g_scene.overlay = room_overlay;
                vid_dirty(20, 370);
            }
            break;
        case MODE_ENDING:
            if (in.key != KEY_NONE || in.clicked)
                g_mode = MODE_QUIT;
            break;
        }
        scene_draw();
    }
    return 1;
}
