/* sim.c: rvsim, a RetroVega flight with the game's logic and nothing else, on the host (docs/logic.md, "The host
 * harness").  The logic (src/logic.c) and the modules it shares run as they do in RV.EXE, built for a 32- or 64-bit
 * host or the browser: the mission's ships into their slots, then a step at a time the pilot's controls (the keys
 * and the mouse in the window, or a recording), logic_frame, and the checks of the rules the layer needs (host.c).
 * The window shows the world from the player's seat as wire boxes (each ship's hit box), lines (the shots) and dots
 * (the stars), with logic_hud drawn over it.  No sprites, no cockpit, no sound, no asteroid fields: the play only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#ifdef HOST_SDL
#include <SDL.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "collide.h"
#include "world.h"
#include "lightfield.h"
#include "shots.h"
#include "flight.h"
#include "panel.h"
#include "hud.h"
#include "logic.h"
#include "host.h"
#include "palette.h"

static const char k_usage[] =
    "usage: rvsim [options]\n"
    "  --mission N      fly mission N (RVGAME.DAT's index; default: a new campaign's first)\n"
    "  --ship T         the player's ship type (data/ships.csv's row)\n"
    "  --seed S         randomSeed at the start (default: the game's, from the mission)\n"
    "  --frames N       stop after N steps (0: until the flight ends; the default headless is 900, 30 s)\n"
    "  --headless       no window: as fast as it goes\n"
    "  --record FILE    write the controls, a step at a time\n"
    "  --replay FILE    fly them again (the mission, the ship and the seed come from the file)\n"
    "  --hash N         print the world's hash every N steps (and at the end): equal runs, equal hashes\n"
    "  --trace          log every thunk call\n"
    "  --check-src      run each do_damage again with src 0xFFFF and compare (the layer's replay rule)\n"
    "  --strict         stop (abort) at the first broken rule\n"
    "  --log FILE       the game's log (default: stderr)\n"
    "  --data DIR       where RVGAME.DAT and SHAPES.DAT are (default: beside rvsim)\n"
    "  --shot FILE      at the end, the picture as a PPM file\n"
    "  --scale N        the window's size, N x 640 x 480 (default 2)\n"
    "  --scene FILE     the ships of FILE instead of the mission's: `ship TYPE SIDE X Y Z YAW [PROFILE] [KPS]` a line\n"
    "                   (km from the player, which starts at the origin facing +z; YAW degrees; PROFILE the AI's,\n"
    "                   docs/ai.md; KPS its speed), `player KPS` the player's throttle\n"
    "  --shiptrace FILE the flight traced a step at a time, as the layer's WCNET_SHIPTRACE traces the old games\n"
    "                   (tests/wc1/README.md): a line a ship, the shots and the hits\n"
    "  --hunt S         the player faces the nearest enemy every S seconds with the trigger held (a test pilot)\n"
    "  --nodamage       the player takes no damage (playerCanBeDamaged 0: a long look at the enemies)\n"
    "keys: arrows steer, Q E roll, Space fires, Enter a missile, Tab afterburner, + - F S the throttle, A autopilot,\n"
    "      M mouse steering on/off (or hold the right button), P pause, N a step while paused, Esc ends\n";

static struct {
    const char *data, *record, *replay, *log, *shot;
    int32_t mission, ship;
    int seed_set;
    uint32_t seed, frames, hash_every;
    int headless, scale, frames_set;
    const char *scene, *shiptrace;
    int32_t hunt, speed;                /* --hunt's seconds (0 off); the scene's player throttle (-1 the mission's) */
    int nodamage;
} opt = { 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0 };

static RvMission g_m;
static uint16_t g_mission;
static uint32_t g_seed;                 /* randomSeed as the flight began (the recording's) */
static FILE *g_rec, *g_play;
static uint32_t g_steps;
static uint8_t g_autopilot;             /* the autopilot key this step (A): logic_autopilot before the step */
static int g_over;
#ifdef HOST_SDL
static int g_paused, g_step_once, g_mouse_steer;
#endif

/* --- the controls' recording: "RVSIMIN1", the mission, the ship, the seed, then 8 bytes a step: the PilotInput's
 * six (the missile byte's bit 1 the autopilot key) and the throttle (setSpeed[0] as the controls left it: the
 * engine's keys set it, not the logic) --- */

static const char k_magic[8] = { 'R', 'V', 'S', 'I', 'M', 'I', 'N', '1' };

static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }
static void put32(FILE *f, uint32_t v) { put16(f, (uint16_t)v); put16(f, (uint16_t)(v >> 16)); }
static int16_t get16(FILE *f, uint16_t *v)
{
    int a = fgetc(f), b = fgetc(f);
    *v = (uint16_t)(a | b << 8);
    return a >= 0 && b >= 0;
}
static int16_t get32(FILE *f, uint32_t *v)
{
    uint16_t lo = 0, hi = 0;
    int16_t ok = get16(f, &lo) && get16(f, &hi);
    *v = lo | (uint32_t)hi << 16;
    return ok;
}

static void record_step(void)
{
    fputc((uint8_t)g_pilot.steer_x, g_rec);
    fputc((uint8_t)g_pilot.steer_y, g_rec);
    fputc((uint8_t)g_pilot.roll, g_rec);
    fputc(g_pilot.guns, g_rec);
    fputc(g_pilot.missile | g_autopilot << 1, g_rec);
    fputc(g_pilot.afterburner, g_rec);
    put16(g_rec, (uint16_t)setSpeed[0]);
}

static int16_t replay_step(void)
{
    uint8_t b[6];
    uint16_t speed;
    if (fread(b, 1, 6, g_play) != 6 || !get16(g_play, &speed))
        return 0;
    g_pilot.steer_x = (int8_t)b[0];
    g_pilot.steer_y = (int8_t)b[1];
    g_pilot.roll = (int8_t)b[2];
    g_pilot.guns = b[3];
    g_pilot.missile = b[4] & 1;
    g_autopilot = (uint8_t)(b[4] >> 1 & 1);
    g_pilot.afterburner = b[5];
    setSpeed[0] = speed;
    return 1;
}

/* --- the window: the keys and the mouse, the picture --- */

static uint8_t c_terran, c_aera, c_rlaan, c_white, c_grey, c_dim, c_bolt, c_beam, c_ball, c_missile, c_star;

static uint8_t nearest(int r, int g, int b)
{
    int i, best = 0;
    long bd = 1L << 30;
    for (i = 0; i < 256; i++) {
        long dr = k_palette[i][0] - r, dg = k_palette[i][1] - g, db = k_palette[i][2] - b, d = dr * dr + dg * dg + db * db;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

static void colours(void)
{
    c_terran = nearest(40, 210, 60);
    c_aera = nearest(230, 50, 40);
    c_rlaan = nearest(170, 90, 240);
    c_white = nearest(230, 230, 240);
    c_grey = nearest(120, 120, 130);
    c_dim = nearest(45, 45, 60);
    c_bolt = nearest(255, 120, 40);
    c_beam = nearest(90, 220, 255);
    c_ball = nearest(250, 220, 60);
    c_missile = nearest(255, 255, 255);
    c_star = nearest(170, 170, 180);
}

#ifdef HOST_SDL
static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static uint32_t g_rgb[256];
static int g_mx = HOST_W / 2, g_my = HOST_H / 2, g_missile_key, g_right, g_autopilot_key;
static int16_t g_throttle_step;         /* a tap of + or -: a tenth of the top speed, as the game's */

static int window_open(void)
{
    int i;
#ifdef __EMSCRIPTEN__
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");   /* (the keys the view's only while it has the
                                           focus: the page's own fields keep theirs) */
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "rvsim: SDL: %s\n", SDL_GetError());
        return 0;
    }
    g_win = SDL_CreateWindow("rvsim", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, HOST_W * opt.scale,
                             480 * opt.scale, SDL_WINDOW_RESIZABLE);
    g_ren = g_win ? SDL_CreateRenderer(g_win, -1, 0) : 0;
    g_tex = g_ren ? SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, HOST_W, HOST_H) : 0;
    if (!g_tex) {
        fprintf(stderr, "rvsim: SDL: %s\n", SDL_GetError());
        return 0;
    }
    SDL_RenderSetLogicalSize(g_ren, HOST_W, 480);   /* (the game's 640 x 400 shown 4:3, as a VGA monitor shows it) */
    for (i = 0; i < 256; i++)
        g_rgb[i] = 0xFF000000u | (uint32_t)k_palette[i][0] << 16 | (uint32_t)k_palette[i][1] << 8 | k_palette[i][2];
    return 1;
}

/* the keys: the arrows steer at the full rate, else the pointer (with mouse steering on, or the right button held)
 * by its distance from the middle, as flight.c's flight_keys; the throttle is the engine's (setSpeed[0]) */
static void controls(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    const RvShipType *t = slot_type(0);
    int dx = (k[SDL_SCANCODE_RIGHT] != 0) - (k[SDL_SCANCODE_LEFT] != 0);
    int dy = (k[SDL_SCANCODE_DOWN] != 0) - (k[SDL_SCANCODE_UP] != 0);
    uint32_t top = t ? t->max_kps : 500;
    uint32_t buttons = SDL_GetMouseState(NULL, NULL);
    if (dx || dy) {
        g_pilot.steer_x = (int8_t)(dx * 9);
        g_pilot.steer_y = (int8_t)(dy * 9);
    } else if (g_mouse_steer || g_right) {
        int ox = g_mx - HOST_W / 2, oy = g_my - HOST_H / 2;
        g_pilot.steer_x = (int8_t)(ox > 16 ? (ox > 120 ? 9 : ox * 9 / 120) : ox < -16 ? (ox < -120 ? -9 : ox * 9 / 120) : 0);
        g_pilot.steer_y = (int8_t)(oy > 12 ? (oy > 80 ? 9 : oy * 9 / 80) : oy < -12 ? (oy < -80 ? -9 : oy * 9 / 80) : 0);
    } else
        g_pilot.steer_x = g_pilot.steer_y = 0;
    g_pilot.roll = (int8_t)((k[SDL_SCANCODE_E] != 0) * 9 - (k[SDL_SCANCODE_Q] != 0) * 9);
    g_pilot.guns = (uint8_t)(k[SDL_SCANCODE_SPACE] || (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)));
    g_pilot.missile = (uint8_t)g_missile_key;
    g_missile_key = 0;
    g_autopilot = (uint8_t)g_autopilot_key;
    g_autopilot_key = 0;
    g_pilot.afterburner = (uint8_t)(k[SDL_SCANCODE_TAB] != 0);
    if (g_throttle_step > 0)
        setSpeed[0] += top / 10;
    else if (g_throttle_step < 0)
        setSpeed[0] = setSpeed[0] > top / 10 ? setSpeed[0] - top / 10 : 0;
    g_throttle_step = 0;
    if (setSpeed[0] > top)              /* (then the held keys' steps: a step past the top, as flight_keys allows) */
        setSpeed[0] = top;
    if (k[SDL_SCANCODE_EQUALS] || k[SDL_SCANCODE_KP_PLUS])   /* (held: as the game's) */
        setSpeed[0] += 2;
    if ((k[SDL_SCANCODE_MINUS] || k[SDL_SCANCODE_KP_MINUS]) && setSpeed[0] >= 2)
        setSpeed[0] -= 2;
}

static void events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            g_over = 1;
        else if (e.type == SDL_MOUSEMOTION) {
            g_mx = e.motion.x;
            g_my = e.motion.y * HOST_H / 480;
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            if (e.button.button == SDL_BUTTON_RIGHT)
                g_right = e.type == SDL_MOUSEBUTTONDOWN;
        } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: missionStatus = 5; break;   /* (the game's exit) */
            case SDLK_RETURN: case SDLK_KP_ENTER: g_missile_key = 1; break;
            case SDLK_a: g_autopilot_key = 1; break;
            case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: g_throttle_step = 1; break;
            case SDLK_MINUS: case SDLK_KP_MINUS: g_throttle_step = -1; break;
            case SDLK_f: if (slot_type(0)) setSpeed[0] = slot_type(0)->max_kps; break;
            case SDLK_s: setSpeed[0] = 0; break;
            case SDLK_m: g_mouse_steer = !g_mouse_steer; break;
            case SDLK_p: g_paused = !g_paused; break;
            case SDLK_n: case SDLK_PERIOD: g_step_once = 1; break;
            default: break;
            }
        }
    }
}

#endif

/* --- the picture: the world through the player's camera (flight_camera), in doubles: it decides nothing --- */

typedef struct { double x, y, z; } V3;
static RvCamera g_cam;
#define NEAR_Z 256.0                    /* (a km: nearer is behind the eye) */

static V3 eye_of(double x, double y, double z)   /* a world point (positions) as the camera has it */
{
    V3 r, c;
    r.x = x - g_cam.pos.x;
    r.y = y - g_cam.pos.y;
    r.z = z - g_cam.pos.z;
    c.x = (r.x * g_cam.b.right.x + r.y * g_cam.b.right.y + r.z * g_cam.b.right.z) / 256;
    c.y = (r.x * g_cam.b.up.x + r.y * g_cam.b.up.y + r.z * g_cam.b.up.z) / 256;
    c.z = (r.x * g_cam.b.front.x + r.y * g_cam.b.front.y + r.z * g_cam.b.front.z) / 256;
    return c;
}

static void dot(int x, int y, uint8_t c)
{
    if (x >= 0 && x < HOST_W && y >= 0 && y < HOST_H)
        host_screen[y * HOST_W + x] = c;
}

static int outcode(double x, double y)
{
    return (x < 0) | (x > HOST_W - 1) << 1 | (y < 0) << 2 | (y > HOST_H - 1) << 3;
}

static void line2(double x0, double y0, double x1, double y1, uint8_t c)   /* clipped to the screen */
{
    int a = outcode(x0, y0), b = outcode(x1, y1), n, i;
    while (a | b) {
        double x, y;
        int o;
        if (a & b)
            return;
        o = a ? a : b;
        if (o & 1) { y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0); x = 0; }
        else if (o & 2) { y = y0 + (y1 - y0) * (HOST_W - 1 - x0) / (x1 - x0); x = HOST_W - 1; }
        else if (o & 4) { x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0); y = 0; }
        else { x = x0 + (x1 - x0) * (HOST_H - 1 - y0) / (y1 - y0); y = HOST_H - 1; }
        if (o == a) { x0 = x; y0 = y; a = outcode(x0, y0); }
        else { x1 = x; y1 = y; b = outcode(x1, y1); }
    }
    n = (int)(abs((int)(x1 - x0)) > abs((int)(y1 - y0)) ? abs((int)(x1 - x0)) : abs((int)(y1 - y0)));
    for (i = 0; i <= n; i++)
        dot((int)(x0 + (x1 - x0) * i / (n ? n : 1) + 0.5), (int)(y0 + (y1 - y0) * i / (n ? n : 1) + 0.5), c);
}

static int project(V3 p, double *sx, double *sy)
{
    if (p.z < NEAR_Z)
        return 0;
    *sx = g_cam.cx + p.x * g_cam.focal / p.z;
    *sy = g_cam.cy - p.y * g_cam.focal / p.z;
    return 1;
}

static void line3(V3 a, V3 b, uint8_t c)   /* a world segment, cut at the near plane */
{
    double ax, ay, bx, by;
    if (a.z < NEAR_Z && b.z < NEAR_Z)
        return;
    if (a.z < NEAR_Z || b.z < NEAR_Z) {
        V3 *n = a.z < NEAR_Z ? &a : &b, *f = a.z < NEAR_Z ? &b : &a;
        double t = (NEAR_Z - n->z) / (f->z - n->z);
        n->x += (f->x - n->x) * t;
        n->y += (f->y - n->y) * t;
        n->z = NEAR_Z;
    }
    project(a, &ax, &ay);
    project(b, &bx, &by);
    if (ax < -1e5 || ax > 1e5 || ay < -1e5 || ay > 1e5 || bx < -1e5 || bx > 1e5 || by < -1e5 || by > 1e5)
        return;
    line2(ax, ay, bx, by, c);
}

static V3 eye_lf(const LfVec *v) { return eye_of(v->x, v->y, v->z); }

static void draw_stars(void)
{
    static int16_t star[400][3];
    static int made;
    int i;
    if (!made) {                        /* (the sky's own stream: never game_rand's) */
        uint32_t r = 12345;
        for (i = 0; i < 400; i++) {
            int k;
            for (k = 0; k < 3; k++) {
                r = r * 1664525u + 1013904223u;
                star[i][k] = (int16_t)((int32_t)(r >> 16) - 32768);
            }
        }
        made = 1;
    }
    for (i = 0; i < 400; i++) {
        V3 d;
        double sx, sy;
        d = eye_of(g_cam.pos.x + star[i][0] * 1024.0, g_cam.pos.y + star[i][1] * 1024.0,
                   g_cam.pos.z + star[i][2] * 1024.0);
        if (project(d, &sx, &sy))
            dot((int)sx, (int)sy, c_star);
    }
}

static void draw_ships(void)
{
    int16_t s;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++) {
        uint8_t c = commAnimInfo[s] == SIDE_AERA ? c_aera : commAnimInfo[s] == SIDE_RLAAN ? c_rlaan : c_terran;
        LfVec corner[8], vital[13];
        V3 p, n;
        double sx, sy;
        int k;
        if (maybeEntityType[s] == ET_MISSILE) {   /* a missile: its last step's line */
            p = eye_of(gPositionVector[s].x, gPositionVector[s].y, gPositionVector[s].z);
            n = eye_of((double)gPositionVector[s].x - gVelocityVector[s].x * 2.0,
                       (double)gPositionVector[s].y - gVelocityVector[s].y * 2.0,
                       (double)gPositionVector[s].z - gVelocityVector[s].z * 2.0);
            line3(p, n, c_missile);
            continue;
        }
        if (!slot_is_ship(s))
            continue;
        if (shipAiState[s] == 9)        /* (dying: dimmed) */
            c = c_grey;
        if (collide_shape(s, corner, vital, 13) >= 0) {
            for (k = 0; k < 8; k++) {   /* its hit box's twelve edges: the corners one bit apart */
                if (!(k & 1)) line3(eye_lf(&corner[k]), eye_lf(&corner[k | 1]), c);
                if (!(k & 2)) line3(eye_lf(&corner[k]), eye_lf(&corner[k | 2]), c);
                if (!(k & 4)) line3(eye_lf(&corner[k]), eye_lf(&corner[k | 4]), c);
            }
        }
        /* its nose: a line from its middle along its front, twice its half box; and a mark that shows at any
         * distance */
        p = eye_of(gPositionVector[s].x, gPositionVector[s].y, gPositionVector[s].z);
        {
            double h = (double)collide_half(s) * 2 / 256;
            n = eye_of(gPositionVector[s].x + gOrientationFrontVector[s].x * h,
                       gPositionVector[s].y + gOrientationFrontVector[s].y * h,
                       gPositionVector[s].z + gOrientationFrontVector[s].z * h);
        }
        line3(p, n, c_white);
        if (project(p, &sx, &sy)) {
            line2(sx - 3, sy, sx, sy - 3, c);
            line2(sx, sy - 3, sx + 3, sy, c);
            line2(sx + 3, sy, sx, sy + 3, c);
            line2(sx, sy + 3, sx - 3, sy, c);
        }
    }
}

static void draw_shots(void)
{
    int16_t i, n = (int16_t)shots_count();
    for (i = 0; i < n; i++) {
        const Shot *s = shot(i);
        uint8_t c = s->look == RV_LOOK_BOLT ? c_bolt : s->look == RV_LOOK_BEAM ? c_beam : c_ball;
        V3 head = eye_of(s->x, s->y, s->z), tail;
        if (s->look == RV_LOOK_BOLT || s->look == RV_LOOK_BEAM)   /* (its drawn length: twice its half, x16) */
            tail = eye_of(s->x - s->hx * 32.0, s->y - s->hy * 32.0, s->z - s->hz * 32.0);
        else
            tail = eye_of((double)s->x - s->vx * 0.5, (double)s->y - s->vy * 0.5, (double)s->z - s->vz * 0.5);
        line3(head, tail, c);
    }
}

static void draw_radar(void)            /* the world from above the player, 16,000 km to the edge */
{
    const int x0 = HOST_W - 128, y0 = HOST_H - 128, r = 60;
    int16_t s;
    int y;
    for (y = y0; y <= y0 + 2 * r; y++)  /* (its own black: no stars in it) */
        memset(host_screen + y * HOST_W + x0, 0, 2 * r + 1);
    line2(x0, y0, x0 + 2 * r, y0, c_dim);
    line2(x0 + 2 * r, y0, x0 + 2 * r, y0 + 2 * r, c_dim);
    line2(x0 + 2 * r, y0 + 2 * r, x0, y0 + 2 * r, c_dim);
    line2(x0, y0 + 2 * r, x0, y0, c_dim);
    dot(x0 + r, y0 + r, c_white);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++) {
        V3 p;
        int x, y;
        if (!slot_is_ship(s) && maybeEntityType[s] != ET_MISSILE)
            continue;
        p = eye_of(gPositionVector[s].x, gPositionVector[s].y, gPositionVector[s].z);
        x = x0 + r + (int)(p.x / 256 * r / 16000);
        y = y0 + r - (int)(p.z / 256 * r / 16000);
        if (x < x0 || x > x0 + 2 * r || y < y0 || y > y0 + 2 * r)
            continue;
        dot(x, y, maybeEntityType[s] == ET_MISSILE ? c_missile
                  : commAnimInfo[s] == SIDE_AERA ? c_aera : commAnimInfo[s] == SIDE_RLAAN ? c_rlaan : c_terran);
        dot(x + 1, y, c_white);
    }
}

static void draw(void)                  /* into host_screen */
{
    memset(host_screen, 0, sizeof host_screen);
    flight_camera(&g_cam);
    draw_stars();
    draw_ships();
    draw_shots();
    draw_radar();
    line2(HOST_W / 2 - 6, HOST_H / 2, HOST_W / 2 - 2, HOST_H / 2, c_grey);   /* the nose */
    line2(HOST_W / 2 + 2, HOST_H / 2, HOST_W / 2 + 6, HOST_H / 2, c_grey);
    line2(HOST_W / 2, HOST_H / 2 - 6, HOST_W / 2, HOST_H / 2 - 2, c_grey);
    line2(HOST_W / 2, HOST_H / 2 + 2, HOST_W / 2, HOST_H / 2 + 6, c_grey);
    hud_band(0, HOST_H, NULL, 0, 0);    /* the logic's HUD: the whole screen one band (on DOS a band at a time) */
    logic_hud();
}

static void shot_ppm(const char *path)  /* host_screen as a PPM picture */
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", HOST_W, HOST_H);
    for (i = 0; i < HOST_W * HOST_H; i++)
        fwrite(k_palette[host_screen[i]], 1, 3, f);
    fclose(f);
}

#ifdef HOST_SDL
static void picture(void)
{
    static uint32_t px[HOST_W * HOST_H];
    int i;
    draw();
    for (i = 0; i < HOST_W * HOST_H; i++)
        px[i] = g_rgb[host_screen[i]];
    SDL_UpdateTexture(g_tex, NULL, px, HOST_W * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
    if (g_steps % 10 == 0 || g_paused) {
        char t[200];
        snprintf(t, sizeof t, "rvsim  step %lu  mission %u  %lu kps (set %lu)  shields %u/%u  armour %u %u %u %u  "
                 "shots %d%s%s", (unsigned long)g_steps, g_mission, (unsigned long)speedLo[0],
                 (unsigned long)setSpeed[0], SHIELD(0, 0), SHIELD(0, 1), ARMOR(0, 0), ARMOR(0, 1), ARMOR(0, 2),
                 ARMOR(0, 3), shots_count(), g_mouse_steer ? "  [mouse]" : "", g_paused ? "  [paused]" : "");
        SDL_SetWindowTitle(g_win, t);
    }
}
#endif

/* --- the flight --- */

static void fail(const char *what) __attribute__((noreturn));
static void load_scene(const char *path);
static void fail(const char *what)
{
    fprintf(stderr, "rvsim: %s\n", what);
    exit(2);
}

static void start(void)
{
    if (!camp_load_data("RVGAME.DAT"))
        fail("no RVGAME.DAT (--data DIR: build/hostNN/ has one)");
    camp_new();
    if (!collide_reserve())
        fail("no SHAPES.DAT (--data DIR)");
    rvPlayerCount = 1;
    rvNetRole = 0;
    g_mission = (uint16_t)(opt.mission >= 0 ? opt.mission : camp_mission_index());
    if (g_mission >= g_nmissions)
        fail("no such mission");
    if (opt.ship >= 0)
        g_camp.player_ship = (uint8_t)opt.ship;
    if (!ship_type(g_camp.player_ship))
        fail("no such ship type");
    camp_mission(g_mission, &g_m);
    camp_load_units(&g_m);
    /* the flight begins, as flight.c's flight_startup has it */
    memset(maybeEntityType, 0, RV_SLOTS * 2);
    missionStatus = 0;
    frameCounter = 0;
    rvCockpitDamage = 0;
    playerCanBeDamaged = 1;
    randomSeed = opt.seed_set ? opt.seed : UINT32_C(0x52560000) + (uint32_t)g_mission * 977 + g_camp.flown;
    memset(&g_sys, 0, sizeof g_sys);
    g_sys.emcon = EMCON_ACTIVE;
    g_sys.iff = 1;
    g_sys.armed = 1;
    g_sys.decoys = 6;
    g_sys.radar_range = 1;
    g_sys.target = -1;
    g_sys.fuel = 1000;
    g_sys.hull = 100;
    g_sys.lights = 1;
    g_sys.target_km = g_sys.nav_km = -1;
    g_seed = randomSeed;
    rv_log("rvsim: mission %u (%.16s), ship %u, seed %08lX", g_mission, g_m.id, g_camp.player_ship,
           (unsigned long)g_seed);
    logic_start(&g_m);
    shots_flight_start(0);              /* (the list, no draw pools: the picture is sim.c's) */
    if (opt.scene)
        load_scene(opt.scene);
    if (opt.nodamage)
        playerCanBeDamaged = 0;
    if (opt.shiptrace && (host_shiptrace = fopen(opt.shiptrace, "w")) == 0)
        fail("cannot write the trace file");
}

/* --scene: the mission's ships out, the file's in (world.h's ship_spawn, through the mission ship table so a line
 * may name an AI profile: byte 17 of the record, as logic.c reads it) */
static int16_t type_of(const char *name)
{
    uint16_t i;
    if (isdigit((unsigned char)name[0]))
        return (int16_t)strtol(name, 0, 0);
    for (i = 0; i < g_nshiptypes; i++)
        if (!strncmp(g_shiptypes[i].key, name, RV_KEY) && strlen(name) <= RV_KEY)
            return g_shiptypes[i].type;
    return -1;
}

static void load_scene(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[200], name[32];
    int16_t s, n = 1;
    if (!f)
        fail("no such scene file");
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        ship_clear(s);
    memset(navPointTable, 0, navPointTable_SIZE * 16);   /* (no nav points but the origin: the mission's navs would bring their ships) */
    gPositionVector[0].x = gPositionVector[0].y = gPositionVector[0].z = 0;
    ship_face(0, 0, 0);
    while (fgets(line, sizeof line, f)) {
        long side, x, y, z, yaw, profile = 0, kps = -1;
        int16_t type;
        if (sscanf(line, "player %ld", &kps) == 1) {
            opt.speed = (int32_t)kps;
            continue;
        }
        if (sscanf(line, "ship %31s %ld %ld %ld %ld %ld %ld %ld", name, &side, &x, &y, &z, &yaw, &profile, &kps) < 6)
            continue;
        if ((type = type_of(name)) < 0 || n >= 64)
            fail("scene: no such ship type, or too many ships");
        {
            uint8_t *rec = missionShipTable + missionShipTable_SIZE * n;
            memset(rec, 0, missionShipTable_SIZE);
            *(uint16_t *)rec = (uint16_t)type;
            *(uint16_t *)(rec + 2) = (uint16_t)side;
            *(uint16_t *)(rec + 4) = 0xFFFF;
            *(int16_t *)(rec + 10) = (int16_t)x;
            *(int16_t *)(rec + 12) = (int16_t)y;
            *(int16_t *)(rec + 14) = (int16_t)z;
            rec[16] = RV_PILOT_NONE;
            rec[17] = (uint8_t)profile;
            s = (int16_t)outerSpawnShipEntity((uint16_t)n, 0);
            if (s < 1 || s > RV_MAX_SHIP_SLOT)
                fail("scene: no slot for a ship");
            ship_face(s, (int16_t)(yaw * TURN / 360), 0);
            if (kps >= 0)
                setSpeed[s] = speedLo[s] = (uint32_t)kps;
            {
                int32_t step = KPS_STEP(speedLo[s]);
                const RvVec3 *F = &gOrientationFrontVector[s];
                gVelocityVector[s] = vec3(FIX_MUL(F->x, step), FIX_MUL(F->y, step), FIX_MUL(F->z, step));
            }
            n++;
        }
    }
    fclose(f);
    if (opt.speed >= 0) {
        int32_t step = KPS_STEP(opt.speed);
        setSpeed[0] = speedLo[0] = (uint32_t)opt.speed;
        gVelocityVector[0] = vec3(0, 0, step);
    }
    rv_log("rvsim: scene %s: %d ships, the player at %ld kps", path, n - 1, (long)speedLo[0]);
}

/* --shiptrace: the ships after a step, as the layer traces the old games (S frame slot entity type side pilot
 * x y z fx fy fz ux uy uz vx vy vz speed setspeed aistate aitimer missiletarget parent hull shf shr arf arr arl arrt) */
static void trace_step(void)
{
    int16_t s;
    for (s = 0; s < RV_SLOTS; s++) {
        if (maybeEntityType[s] != ET_SHIP && maybeEntityType[s] != ET_CAPSHIP && maybeEntityType[s] != ET_MISSILE)
            continue;
        fprintf(host_shiptrace, "S %u %d %u %u %u %u %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %lu %lu %u %u %u %u %u %u %u %u %u %u %u\n",
                frameCounter, s, maybeEntityType[s], gInstanceShipTypes[s], commAnimInfo[s], shipStateByte[s],
                (long)gPositionVector[s].x, (long)gPositionVector[s].y, (long)gPositionVector[s].z,
                (long)gOrientationFrontVector[s].x, (long)gOrientationFrontVector[s].y, (long)gOrientationFrontVector[s].z,
                (long)gOrientationUpVector[s].x, (long)gOrientationUpVector[s].y, (long)gOrientationUpVector[s].z,
                (long)gVelocityVector[s].x, (long)gVelocityVector[s].y, (long)gVelocityVector[s].z,
                (unsigned long)speedLo[s], (unsigned long)setSpeed[s], shipAiState[s], damageProperty[s], missileTarget[s],
                parentShipForShip[s], damagePoints[s], SHIELD(s, 0), SHIELD(s, 1), ARMOR(s, 0), ARMOR(s, 1), ARMOR(s, 2),
                ARMOR(s, 3));
    }
}

/* --hunt: the player's nose onto the nearest living enemy */
static void hunt_face(void)
{
    int16_t s, best = -1;
    int32_t bd = INT32_C(0x7FFFFFFF), d;
    RvVec3 v;
    int32_t horiz;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++) {
        if (!slot_is_ship(s) || commAnimInfo[s] == commAnimInfo[0] || shipAiState[s] == 9)
            continue;
        d = player_km(s);
        if (d < bd) {
            bd = d;
            best = s;
        }
    }
    if (best < 0 || bd < 1500)          /* (nearer than that it flies on past: a test pilot does not ram) */
        return;
    v = vec3(TO_KM(gPositionVector[best].x - gPositionVector[0].x), TO_KM(gPositionVector[best].y - gPositionVector[0].y),
             TO_KM(gPositionVector[best].z - gPositionVector[0].z));
    horiz = (int32_t)lf_sqrt((uint32_t)((v.x >> 4) * (v.x >> 4) + (v.z >> 4) * (v.z >> 4))) << 4;
    ship_face(0, lf_atan2(v.x, v.z), lf_atan2(v.y, horiz));
    g_sys.target = best;
}

/* a step; 0 when the flight is over */
static int step(void)
{
    if (g_play) {
        if (!replay_step())
            return 0;
    } else {
#ifdef HOST_SDL
        if (g_win)
            controls();
        else
#endif
        {   /* no hands on the controls: they still keep the throttle at most the top speed (flight_keys) */
            const RvShipType *t = slot_type(0);
            uint32_t top = t ? t->max_kps : 500;
            memset(&g_pilot, 0, sizeof g_pilot);
            g_autopilot = 0;
            if (setSpeed[0] > top)
                setSpeed[0] = top;
            if (opt.hunt) {             /* (the test pilot: onto the nearest enemy every so often, firing) */
                if (g_steps % (uint32_t)(opt.hunt * LOGIC_HZ) == 0)
                    hunt_face();
                g_pilot.guns = 1;
            }
        }
    }
    if (g_rec)
        record_step();
    g_sys.burning = g_pilot.afterburner;
    g_steps++;
    host_ticks = g_steps * 91 / 150;    /* (18.2 ticks a second at 30 steps) */
    if (g_autopilot)                    /* (RV.EXE runs the key's call between steps too: flight_autopilot) */
        logic_autopilot();
    frameCounter = (uint16_t)(frameCounter + 1);   /* (it wraps, as on DOS: the cast says so to clang's check) */
    logic_frame();
    if (host_shiptrace)
        trace_step();
    host_check_world();
    if (opt.hash_every && g_steps % opt.hash_every == 0)
        printf("step %lu hash %08lx\n", (unsigned long)g_steps, (unsigned long)host_hash());
    return !missionStatus && !g_over && (!opt.frames || g_steps < opt.frames);
}

static int g_finished;

static void finish(void)
{
    if (g_finished++)
        return;
    if (opt.shot) {
        draw();
        shot_ppm(opt.shot);
    }
    logic_end();
    printf("rvsim: %lu steps, missionStatus %u, hash %08lx, %lu rule reports\n", (unsigned long)g_steps,
           missionStatus, (unsigned long)host_hash(), (unsigned long)host_rules_broken);
    shots_flight_end();
    collide_flight_end();
    xms_free_level(XMS_RESIDENT);
    if (g_rec)
        fclose(g_rec);
    if (g_play)
        fclose(g_play);
    fflush(stdout);
    if (host_log && host_log != stderr)
        fclose(host_log);
    if (host_shiptrace)
        fclose(host_shiptrace);
}

#ifdef HOST_SDL
/* the window's pace: LOGIC_HZ steps a second, a picture after each */
static uint32_t g_due;

static int frame(void)                  /* 0 when it is over */
{
    uint32_t now = SDL_GetTicks();
    int go = 1;
    events();
    if (g_over || missionStatus)
        return 0;
    if (g_paused && !g_step_once) {
        picture();
        return 1;
    }
    if (!g_due)
        g_due = now;
    while (go && (int32_t)(now - g_due) >= 0) {   /* (catches up at most a few steps after a stall) */
        go = step();
        g_due += 1000 / LOGIC_HZ;
        if ((int32_t)(now - g_due) > 200)
            g_due = now;
        if (g_paused)
            break;
    }
    g_step_once = 0;
    picture();
    return go;
}

#ifdef __EMSCRIPTEN__
static void web_frame(void)
{
    if (!frame()) {
        finish();
        EM_ASM({ if (typeof rvsimOver === 'function') rvsimOver($0); }, missionStatus);
        emscripten_cancel_main_loop();
    }
}
#endif
#endif

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : 0;
#define VAL (v ? (i++, v) : (fail("a value is missing"), v))
        if (!strcmp(a, "--mission")) opt.mission = atoi(VAL);
        else if (!strcmp(a, "--ship")) opt.ship = atoi(VAL);
        else if (!strcmp(a, "--seed")) { opt.seed = (uint32_t)strtoul(VAL, 0, 0); opt.seed_set = 1; }
        else if (!strcmp(a, "--frames")) { opt.frames = (uint32_t)strtoul(VAL, 0, 0); opt.frames_set = 1; }
        else if (!strcmp(a, "--headless")) opt.headless = 1;
        else if (!strcmp(a, "--record")) opt.record = VAL;
        else if (!strcmp(a, "--replay")) opt.replay = VAL;
        else if (!strcmp(a, "--hash")) opt.hash_every = (uint32_t)strtoul(VAL, 0, 0);
        else if (!strcmp(a, "--trace")) host_trace = 1;
        else if (!strcmp(a, "--check-src")) host_check_src = 1;
        else if (!strcmp(a, "--strict")) host_strict = 1;
        else if (!strcmp(a, "--log")) opt.log = VAL;
        else if (!strcmp(a, "--data")) opt.data = VAL;
        else if (!strcmp(a, "--shot")) opt.shot = VAL;
        else if (!strcmp(a, "--scale")) opt.scale = atoi(VAL);
        else if (!strcmp(a, "--scene")) opt.scene = VAL;
        else if (!strcmp(a, "--shiptrace")) opt.shiptrace = VAL;
        else if (!strcmp(a, "--hunt")) opt.hunt = atoi(VAL);
        else if (!strcmp(a, "--nodamage")) opt.nodamage = 1;
        else {
            fputs(k_usage, strcmp(a, "--help") && strcmp(a, "-h") ? stderr : stdout);
            return strcmp(a, "--help") && strcmp(a, "-h") ? 2 : 0;
        }
#undef VAL
    }
#ifndef HOST_SDL
    opt.headless = 1;
#endif
    if (opt.headless && !opt.frames_set && !opt.replay)
        opt.frames = 900;
    /* the files named on the command line, opened before the move to the data's directory */
    if (opt.log && (host_log = fopen(opt.log, "w")) == 0)
        fail("cannot write the log");
    if (opt.record && (g_rec = fopen(opt.record, "wb")) == 0)
        fail("cannot write the recording");
    if (opt.replay) {
        char m[8];
        uint16_t mission, ship;
        uint32_t seed, spare;
        if ((g_play = fopen(opt.replay, "rb")) == 0)
            fail("cannot read the recording");
        if (fread(m, 1, 8, g_play) != 8 || memcmp(m, k_magic, 8) || !get16(g_play, &mission) || !get16(g_play, &ship)
            || !get32(g_play, &seed) || !get32(g_play, &spare))
            fail("not an rvsim recording");
        opt.mission = mission;
        opt.ship = ship;
        opt.seed = seed;
        opt.seed_set = 1;
    }
    if (!opt.data) {                    /* beside rvsim (host/build.sh puts the data there) */
        static char dir[1024];
        const char *slash = strrchr(argv[0], '/');
        if (slash && (size_t)(slash - argv[0]) < sizeof dir) {
            memcpy(dir, argv[0], (size_t)(slash - argv[0]));
            dir[slash - argv[0]] = 0;
            opt.data = dir;
        }
    }
    if (opt.data && chdir(opt.data))
        fail("no such data directory");
    colours();
    start();
    if (g_rec) {
        fwrite(k_magic, 1, 8, g_rec);
        put16(g_rec, g_mission);
        put16(g_rec, g_camp.player_ship);
        put32(g_rec, g_seed);
        put32(g_rec, 0);
    }
#ifdef HOST_SDL
    if (!opt.headless) {
        if (!window_open())
            return 2;
#ifdef __EMSCRIPTEN__
        emscripten_set_main_loop(web_frame, 0, 1);
        return 0;
#else
        while (frame())
            SDL_Delay(1);
        finish();
        SDL_Quit();
        return host_rules_broken ? 3 : 0;
#endif
    }
#endif
    while (step())
        ;
    finish();
    return host_rules_broken ? 3 : 0;
}
