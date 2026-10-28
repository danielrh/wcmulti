/* flight.c: the flight (docs/engine.md, "Flight").
 *
 * The loop itself is rvflight.inc, at RVHOOK's fixed sites; it calls here:
 * flight_startup once, then flight_keys, flight_frame and flight_music every
 * time round.  A frame is one simulation step (frameCounter++): every slot's
 * physics (ship_physics), then the picture: the cockpit's state is gathered
 * (shields, armour, speed, the radar's contacts, the joystick's way), the
 * rows that change are marked, and the world is drawn through cockpit_draw.
 *
 * The benchmark (this first flight): one view of the first enemy type is
 * drawn N times inside the opening, N moving up while the frame rate stays
 * above 30 a second and down when it falls under: N is the drawing budget
 * of this machine at this cycle count.  It shows on the left MFD and in the
 * fixed block (rvBenchCount, rvFps).  After 20 seconds the flight lands and
 * the mission's outcome is rolled as before. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "rv.h"
#include "rvhooks.h"
#include "room.h"
#include "campaign.h"
#include "music.h"
#include "ships.h"
#include "cockpit.h"
#include "bolts.h"
#include "flames.h"
#include "comms.h"
#include "panel.h"
#include "balls.h"
#include "rocks.h"
#include "missiles.h"
#include "sky.h"
#include "collide.h"
#include "score.h"
#include "flight.h"
#include "world.h"
#include "shots.h"
#include "hud.h"
#include "logic.h"

int g_flight_seconds = 20;              /* the benchmark flight's length (RV.EXE /flight N) */
int g_flight_timed = 0;                 /* /flight N given: the flight lands by itself after N seconds (the tests);
                                           else it lands when the player calls the carrier (C) */
/* the test aids' switches.  Their code (the demos, the benchmarks, the self-tests) is in the test build only,
 * RVT.EXE (build.sh compiles the modules that have it again with -dRV_TESTS: docs/engine.md, "The test build");
 * the game's RV.EXE keeps the switches, never set, so the flight reads the same either way. */
int g_bench_wanted = 0;                 /* RV.EXE /bench N: that many copies, no adapting; 0 none (the default: the
                                           copies tiled the sky for the user); -1 adapt, the measurement */
int g_record = 0;                       /* RV.EXE /record N: a screenshot (Rnnnnn.BMP) every N frames of flight */
int g_capital = 0;                      /* RV.EXE /capital: the carrier in the flight (a test aid) */
int g_sky = 0;                          /* RV.EXE /sky: three ship views painted on the sky, to roll and turn round (a test aid) */
/* the sky test scene's sprites: the enemy type's views, painted on the sky at these directions and sizes */
static const struct { int v; LfVec dir; int scale; } g_sky_sprites[3] = {
    { 10, { 0, 1500, 4096 }, 256 }, { 1, { -3000, 900, 2500 }, 192 }, { 18, { 3500, -600, 2200 }, 128 }
};
int g_zoom = 0;                         /* RV.EXE /zoom: one ship ahead, rolling, swinging in and out (a test aid) */
int g_lasers = 0;                       /* RV.EXE /lasers N: the laser benchmark with N far bolts (-1: as many as hold
                                           30 fps), two big ships near drawn exactly, small ones far, the near bolts */
int g_near_bolts = 12;                  /* RV.EXE /near N: the bolts kept near the camera in it */
int g_far_ships = 16;                   /* RV.EXE /farships N: its small far ships */
int g_big_ships = 2;                    /* RV.EXE /bigships N: its big near ships, 0 .. 2 */
int g_bolt_demo = 0;                    /* RV.EXE /boltdemo: the player's own guns' bolts (a test aid) */
int g_flames_demo = 0;                  /* RV.EXE /flames: six ships turning every way, flames on (a test aid) */
int g_boxes = 0;                        /* RV.EXE /boxes: every ship's box bracketed (flight_ent_box's check) */
int g_collide_demo = 0;                 /* RV.EXE /collidedemo: two fighters flown into each other (collide.c's check) */
int g_hull_demo = 0;                    /* RV.EXE /hulldemo: a turning fighter shot through by a grid of rays */
int g_flame_test = 0;                   /* RV.EXE /flametest: the flames' self-test (with the flames scene's types) */
int g_comm_demo = 0;                    /* RV.EXE /commdemo: the pilots on the comm in turn, each VDU by turns */
int g_test_ship = 0;                    /* RV.EXE /ship T: the player flies ship type T (hex) whatever the deck chose, + 1 */
int g_hull_type = -1;                   /* RV.EXE /hulltype N: its ship's type (decimal; an asymmetric one checks the
                                           silhouettes' left and right) */
int g_balls = 0;                        /* RV.EXE /balls N: balls in the laser benchmark, N far ones (-1: as many as hold
                                           30 fps) and g_near_balls near */
int g_near_balls = 12;                  /* RV.EXE /nearballs N: the balls kept near the camera in it */
int g_ball_demo = 0;                    /* RV.EXE /balldemo: the four kinds of ball flying at the eye (a test aid) */
int g_missile_demo = 0;                 /* RV.EXE /missiledemo: six missiles seen every way (a test aid) */
int g_sky_demo = 0;                     /* RV.EXE /skydemo: the sky's far things as the camera rolls (a test aid) */
int g_field = 0;                        /* RV.EXE /field D: the flight starts in an asteroid field of D rocks per
                                           (10,000 km)^3 (a stand-in until the nav points have their hazards) */
#define SKY_FRAMES 450                  /* its length: fifteen seconds at 30 fps */
static int __far g_bl_cap;              /* the balls' pool (0: none): the far ones are drawn behind the ships */
#ifdef RV_TESTS
static unsigned char __far *__far g_bl_life;   /* the balls' frames left each */
static int __far g_bl_far, __far g_bl_n, __far g_bl_frames;   /* (far: DGROUP is nearly full) */
static long __far g_bl_sum;
static unsigned long __far g_t_bl_step;
#endif
#define FLAMES_FRAMES 450               /* its recording's length (/record): fifteen seconds at 30 fps */
static int g_flames_frames;
#define DEMO_BOLTS 11
#define DEMO_FRAMES 300                 /* ten seconds at 30 fps */
#define DEMO_SPEED (27L * 64 / BOLT_HALF16)   /* positions per sixteenth of a km of half axis: 6.75 km a frame */
#define DEMO_METRE 32                   /* km a metre */
#ifdef RV_TESTS
static int g_demo_frame;                /* its frames so far */
#endif
/* the hybrid: the biggest ships on screen are drawn exactly (turned and scaled
 * at draw time, sixteen cycles a pixel) while their pixels fit this budget
 * a frame and they are big enough for the steps to show, and every ship the
 * sets have no turned sprites for at its size, at half height; the rest are
 * copies from the pre-rendered sets (RV.EXE /onfly N sets the budget) */
long g_onfly_px = 32000L;
#define ONFLY_MIN_SCALE 100
#define PLAN_MAX 16
typedef struct {
    unsigned char slot, exact, set;     /* exact: 0, or the detail (1, 2, 4) */
    long depth;
    int scale;
    FlameSet fl;                        /* its engines' flames this frame */
    LfPlace p;                          /* where it is, in the exact draw's lattice (ships_size_of) */
} PlanEntry;
static PlanEntry __far g_plan[PLAN_MAX];   /* (far: DGROUP is full) */
#define EXACT_HOLD 45                   /* frames a ship demoted from the exact draw waits before it may be promoted */
#define EXACT_MAX 2                     /* the plan's first exact ships, at the budget's detail (the user asked for a
                                           cap: five ran the flames scene at 20 fps); beyond them only the ships the sets
                                           cannot show turned are drawn exactly, at half height */
static unsigned char __far g_was_exact[RV_MAX_SHIP_SLOT + 1];   /* drawn exactly last frame */
static unsigned char __far g_hold[RV_MAX_SHIP_SLOT + 1];   /* frames left before a demoted ship may be drawn exactly */
static int g_nplan;
#define FLIGHT_TICKS ((unsigned long)g_flight_seconds * 240)   /* ... on the 240 Hz clock */
#define FPS_WINDOW 8                    /* frames the rate is measured over */
#define FPS_WANT LOGIC_HZ
#define BENCH_MAX 900
#define STARS 2000                      /* over the whole sky: the opening sees a twentieth of it */

/* rvflight.inc */
unsigned short __far __cdecl rv_flight_run(void);
unsigned short __far __cdecl rv_hangar(void);
void __far __cdecl rv_autopilot_key(void);
void __far __cdecl rv_cockpit_pointer(void (__far __cdecl *draw)(void));

static const RvMission __far *g_m;
static unsigned long g_t0, g_frame_t[FPS_WINDOW];
static int g_fps10;                     /* frames a second x 10 */
static int g_set_enemy = -1, g_set_wing = -1;
static const RvShipType __far *g_my_type;
static CockpitState g_st;
static char g_mfd[2][4][28];
static int g_steer_x, g_steer_y, g_steer_roll;   /* -9..9 */
static int g_keyboard_steer;
static int g_exit;
static unsigned long g_damage_at;
static int g_damage_next;

#ifdef RV_TESTS
/* the benchmark's sprites: screen positions drifting about the opening */
static struct { short x, y, dx, dy; } __far g_bench[BENCH_MAX];
static int g_nbench, g_bench_view = 17, g_bench_w, g_bench_h, g_tv_w, g_tv_h;
static long g_bench_sum;                /* the count summed over the settled half, for the result */
static int g_bench_frames;
#endif
static unsigned long g_vseed = 12345;   /* visual randomness: outside the ABI's stream */

static unsigned vrnd(void)
{
    g_vseed = g_vseed * 1103515245UL + 12345UL;
    return (unsigned)(g_vseed >> 16) & 0x7FFF;
}

/* --- the simulation: the game's logic (logic.c) a step, then the test scenes' scripted ships --- */

/* degrees a second to the turn per frame (1024 a turn, 20 frames a second) */
static int turn_step(int dps, int steer)
{
    return (int)((long)dps * 1024 / 360 / 20 * steer / 9);
}

/* a test scene's ship (frozen to the game's logic: CTL_FROZEN) moved as its scene wants: 1 when it was one */
static int test_motion(int s)
{
    if (g_lasers && s >= 1 && s <= 2) {
        /* the laser benchmark's two big ships: in place, rolling and turning (so they are drawn exactly, turned
         * and scaled every frame).  (An `if (s == 0) ... else` here, the player rolling in the first arm, was
         * miscompiled by -ox: slot 0 ran the else arm's turns.) */
        vec_turn(&gOrientationRightVector[s], &gOrientationUpVector[s], turn_step(40, s == 1 ? 9 : -6));
        vec_turn(&gOrientationFrontVector[s], &gOrientationRightVector[s], turn_step(15, 9));
        ship_orthonormalise(s);
        return 1;
    }
    if (g_flames_demo && s >= 1) {
        /* the flames scene: in place, each yawing at its own rate, the odd ones pitching too, so every view of
         * their engines comes round */
        vec_turn(&gOrientationFrontVector[s], &gOrientationRightVector[s], turn_step(20 + 7 * s, (s & 2) ? 9 : -9));
        if (s & 1)
            vec_turn(&gOrientationFrontVector[s], &gOrientationUpVector[s], turn_step(11, 9));
        ship_orthonormalise(s);
        return 1;
    }
    if (g_zoom && s == 1) {
        /* the zoom scene's ship, 1,200 .. 6,000 km away on a twelve-second swing: /zoom 1 rolls it
         * at 60 degrees a second (the lightfield's view switches as the camera walks round its
         * nose); /zoom 2 yaws it slowly instead, for the player to roll round; /zoom 3 the same
         * swinging 3,000 .. 15,000 km, through the sets' sizes (the player's roll on far ships) */
        long d = g_zoom == 3 ? 256L * 9000 + ((lf_cos((int)(frameCounter * 1024L / 240 % 1024)) * 6000L) >> 14) * 256
                             : 256L * 3600 + ((lf_cos((int)(frameCounter * 1024L / 240 % 1024)) * 2400L) >> 14) * 256;
        if (g_zoom == 1)
            vec_turn(&gOrientationRightVector[s], &gOrientationUpVector[s], turn_step(60, 9));
        else
            vec_turn(&gOrientationFrontVector[s], &gOrientationRightVector[s], turn_step(12, 9));
        gPositionVector[s].z = gPositionVector[0].z + d;
        return 1;
    }
    return 0;
}

static void sim_step(void)
{
    int s;
    frameCounter++;
    logic_frame();
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (rvController[s] == CTL_FROZEN && slot_is_ship(s))
            test_motion(s);
}

/* --- the picture --- */

/* the stars are directions of the sky (the world frame): each frame they are
 * put through the camera (lf_sky), so they turn with every axis of the
 * player's own rotation, as the ships do, and the ships sit still against
 * them */
static struct { short x, y, z, c; } __far g_star[STARS];   /* a direction of length 4096, and a colour */
static struct { short x, y, c; } __far g_star_at[STARS];   /* the ones on screen this frame */
static int g_star_n;
int __far __cdecl rv_stars_place(const short __far *stars, int n, const long __far *basis, int cx, int cy, int rows,
                                 short __far *out);
static const LfVec g_world_up = { 0, 256, 0 };
static int g_cam_cy = SCR_H / 2;        /* the camera's centre row: the opening's middle (ships_set_camera's) */
static unsigned long g_star_place_ticks, g_star_draw_ticks;   /* what the sky costs (logged when the flight ends) */

static void stars_init(void)
{
    int i;
    for (i = 0; i < STARS; i++) {
        /* a point of the unit sphere: one of the cube, inside the ball, pushed out to length 4096 */
        long x, y, z, n;
        unsigned long r;
        do {
            x = (long)(vrnd() % 8192) - 4096;
            y = (long)(vrnd() % 8192) - 4096;
            z = (long)(vrnd() % 8192) - 4096;
            n = x * x + y * y + z * z;
        } while (n < 1024L * 1024 || n > 4096L * 4096);
        r = lf_sqrt((unsigned long)n);
        g_star[i].x = (short)(x * 4096 / (long)r);
        g_star[i].y = (short)(y * 4096 / (long)r);
        g_star[i].z = (short)(z * 4096 / (long)r);
        g_star[i].c = (i & 3) ? UI_GREY4 : UI_GREY6;
    }
    g_star_n = 0;
}

static void basis_of(int s, LfBasis *b);

/* once a frame: the stars on screen, as a compact list (rv_stars_place,
 * lowlevel.asm: a star needs no turn, and C around three 32-bit products a
 * star cost 8 ms a frame) */
static void stars_place(void)
{
    LfBasis cam;
    long basis[9];
    unsigned long t0 = clock240();
    basis_of(0, &cam);
    basis[0] = cam.front.x; basis[1] = cam.front.y; basis[2] = cam.front.z;
    basis[3] = cam.right.x; basis[4] = cam.right.y; basis[5] = cam.right.z;
    basis[6] = cam.up.x; basis[7] = cam.up.y; basis[8] = cam.up.z;
    g_star_n = rv_stars_place((const short __far *)g_star, STARS, (const long __far *)basis, SCR_W / 2, g_cam_cy, SCR_H,
                              (short __far *)g_star_at);
    g_star_place_ticks += clock240() - t0;
}

static void draw_stars(int y0, int y1)
{
    int i, ey0, rows;
    const short __far *ext = cockpit_extents(&ey0, &rows);
    unsigned long t0 = clock240();
    for (i = 0; i < g_star_n; i++) {
        int x = g_star_at[i].x, y = g_star_at[i].y, k = y - ey0;
        if (y < y0 || y >= y1)
            continue;
        if (ext && (k < 0 || k >= rows || x < ext[2 * k] || x >= ext[2 * k + 1]))
            continue;
        vid_row(y)[x] = (unsigned char)g_star_at[i].c;
    }
    g_star_draw_ticks += clock240() - t0;
}

/* the camera: the player's own frame; a ship's basis as the lightfield wants it */
static void basis_of(int s, LfBasis *b)
{
    b->front = *(const LfVec *)&gOrientationFrontVector[s];
    b->right = *(const LfVec *)&gOrientationRightVector[s];
    b->up = *(const LfVec *)&gOrientationUpVector[s];
}

/* how big a ship shows: full size at 2,500 km, smaller beyond, up to twice
 * full size closer in.  Nothing physical: the ABI's units are WC's (256 = 1
 * km, a kps moves 12.8 units a frame, so a fighter crosses 250 km a second),
 * where a dogfight spans thousands of km and a fighter is drawn big at a
 * couple of thousand, and that is the scale the flight keeps. */
#define FULL_SIZE_UNITS (256L * 2500)
static int scale_of(const LfVec *rel)
{
    long x = rel->x / 64, y = rel->y / 64, z = rel->z / 64, k = 64, d, sc;
    while (x > 37000 || x < -37000 || y > 37000 || y < -37000 || z > 37000 || z < -37000) {
        x /= 4;                         /* (the squares' sum must fit 32 bits: it wrapped past 16,384 km, */
        y /= 4;                         /*  a ship there drawn too big; /flametest found it) */
        z /= 4;
        k *= 4;
    }
    d = (long)lf_sqrt((unsigned long)(x * x) + (unsigned long)(y * y) + (unsigned long)(z * z)) * k;
    sc = d ? FULL_SIZE_UNITS * 256 / d : 512;
    return (int)(sc > 512 ? 512 : sc);
}

static void rel_of(int s, LfVec *rel)
{
    rel->x = gPositionVector[s].x - gPositionVector[0].x;
    rel->y = gPositionVector[s].y - gPositionVector[0].y;
    rel->z = gPositionVector[s].z - gPositionVector[0].z;
}

static unsigned long g_t_flames, g_t_flames_set;   /* what the engines' flames cost: in all, and set up */
static unsigned long g_t_plan;          /* the frame's plan (which ships, how drawn, their views, the flames set up) */

/* a ship's afterburner: faster than its type's top speed (the flames scene: every other ship) */
static int burning(int s)
{
    const RvShipType __far *t = slot_type(s);
    if (g_flames_demo)
        return (s & 1) == 0;
    return t && speedLo[s] > (unsigned long)t->max_kps;
}

/* how the plan has a ship drawn (SHIP_AS_*) */
static int plan_how(int i)
{
    return g_plan[i].exact ? SHIP_AS_EXACT : SHIP_AS_SETS;
}

/* once a frame: which ships show, how big, which are drawn exactly, in what order */
static void plan_draws(void)
{
    LfBasis cam, ship;
    LfVec rel;
    LfPlace p;
    long px[PLAN_MAX], budget = g_onfly_px;
    int s, i, k, n_exact, order[PLAN_MAX];
    g_nplan = 0;
    basis_of(0, &cam);
    for (s = 1; s <= RV_MAX_SHIP_SLOT && g_nplan < PLAN_MAX; s++) {
        int set;
        if (maybeEntityType[s] != 0xC && maybeEntityType[s] != 0xD)
            continue;
        set = ships_find(gInstanceShipTypes[s]);
        if (set < 0)
            continue;
        rel_of(s, &rel);
        basis_of(s, &ship);
        g_plan[g_nplan].scale = scale_of(&rel);
        px[g_nplan] = ships_size_of(set, &rel, &cam, &ship, g_plan[g_nplan].scale, &p);
        if (!p.on_screen)
            continue;
        g_plan[g_nplan].slot = (unsigned char)s;
        g_plan[g_nplan].set = (unsigned char)set;
        g_plan[g_nplan].depth = p.depth;
        g_plan[g_nplan].p = p;
        g_plan[g_nplan].exact = 0;
        g_nplan++;
    }
    /* the exact ones: the biggest first, while the budget holds; one too big
     * for what is left is drawn at half or quarter detail (rows copied, then
     * 2 x 1 blocks) rather than from the sets or slower.  At most EXACT_MAX,
     * and steady: the ships drawn exactly last frame are taken first, and a
     * ship demoted waits EXACT_HOLD frames before it may be promoted (the
     * user's rule: on the budget's line a ship went back and forth, its fine
     * and coarse views in turn) */
    for (n_exact = 0; n_exact < EXACT_MAX; n_exact++) {
        int best = -1, detail;
        for (i = 0; i < g_nplan; i++) {
            int was = g_was_exact[g_plan[i].slot], bw = best >= 0 ? g_was_exact[g_plan[best].slot] : 0;
            if (!g_plan[i].exact && !g_hold[g_plan[i].slot] && g_plan[i].scale >= ONFLY_MIN_SCALE && px[i] > 0
                && px[i] <= budget * 4 && (best < 0 || was > bw || (was == bw && px[i] > px[best])))
                best = i;
        }
        if (best < 0)
            break;
        detail = px[best] <= budget ? 1 : px[best] <= budget * 2 ? 2 : 4;
        g_plan[best].exact = (unsigned char)detail;
        budget -= px[best] / detail;
        order[n_exact] = best;
    }
    if (g_res == 2)                     /* the block mode: every ship turned exactly from its ladder, as blocks */
        for (i = 0; i < g_nplan; i++)
            g_plan[i].exact = 4;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++) {
        int now = 0;
        for (i = 0; i < n_exact; i++)
            now |= g_plan[order[i]].slot == s;
        if (g_was_exact[s] && !now)
            g_hold[s] = EXACT_HOLD;     /* demoted: held off the first two a while */
        else if (g_hold[s])
            g_hold[s]--;
        g_was_exact[s] = (unsigned char)now;
    }
    /* the rest near enough that their sets have no turned sprites (or whose roll steps did not fit): exactly all
     * the same, by their true roll, at half height (the user, 2026-10-21: unrolled near fighters look strange;
     * beyond the first two, each odd row the one above copied down), or at a quarter (2 x 1 blocks too) once the
     * budget is spent.  Their views are resident (no stream) and their dense copies kept in XMS */
    for (i = 0; i < g_nplan; i++)
        if (!g_plan[i].exact && ships_turn_wanted(g_plan[i].set, g_plan[i].scale)) {
            g_plan[i].exact = (unsigned char)(px[i] / 2 <= budget ? 2 : 4);
            budget -= px[i] / g_plan[i].exact;
        }
    /* back to front */
    for (i = 1; i < g_nplan; i++)
        for (k = i; k > 0 && g_plan[k].depth > g_plan[k - 1].depth; k--) {
            PlanEntry t = g_plan[k];
            g_plan[k] = g_plan[k - 1];
            g_plan[k - 1] = t;
        }
    /* the engines' flames, as each ship's sprite will be drawn: the nearest first (the frame's room is limited) */
    {
        unsigned long t0 = clock240();
        flames_frame();
        for (i = g_nplan - 1; i >= 0; i--) {
            int nb, nh, sl = g_plan[i].slot;
            rel_of(sl, &rel);
            basis_of(sl, &ship);
            /* the view and the turn its picture will have (an exact one's true roll, a set's roll step, the far
             * sizes' none): the flames line up with the ship as drawn */
            ships_drawn(g_plan[i].set, plan_how(i), &rel, &cam, &ship, g_plan[i].scale, &p, &nb, &nh);
            flames_add(slot_type(sl), &p, nb, nh, g_plan[i].scale, burning(sl), sl, &g_plan[i].fl);
        }
        g_t_flames += clock240() - t0;
        g_t_flames_set += clock240() - t0;
    }
}

#ifdef RV_TESTS
static void lasers_draw_far(int y0, int y1, const short __far *ext, int ey0, int rows);
#endif
static unsigned long g_t_exact, g_t_sets;   /* what the slots' ships cost to draw: exactly, and from the sets */
static unsigned long g_t_draw, g_t_wait;   /* what a frame costs to draw, and the wait for the next */
static unsigned long g_world_calls;     /* draw_world's calls: the bands a frame draws the world in */

/* --- where a ship is on a screen (flight.h): targeting, its lock, the cockpit's animations --- */

void flight_camera(RvCamera *c)
{
    const RvCockpit __far *ck = cockpit_record();
    c->pos = *(const LfVec *)&gPositionVector[0];
    basis_of(0, &c->b);
    c->cx = SCR_W / 2;
    c->cy = (short)g_cam_cy;
    c->focal = 400;
    c->vx0 = 0;
    c->vx1 = SCR_W;
    c->vy0 = ck ? ck->open_y0 : 0;
    c->vy1 = ck ? ck->open_y1 : SCR_H;
}

int16_t flight_ent_box(int16_t slot, const RvCamera *cam, EntBox *o)
{
    const RvShipType __far *t;
    RvCamera main_cam;
    LfVec rel;
    LfBasis ship;
    long rx, ry, rz, ax, ay;
    int set, sc, i, box[4], how;
    _fmemset(o, 0, sizeof *o);
    if (slot < 0 || slot > RV_MAX_SHIP_SLOT || (maybeEntityType[slot] != 0xC && maybeEntityType[slot] != 0xD))
        return 0;
    rel.x = gPositionVector[slot].x - cam->pos.x;
    rel.y = gPositionVector[slot].y - cam->pos.y;
    rel.z = gPositionVector[slot].z - cam->pos.z;
    rx = rel.x >> 4;                    /* sixteenths of a km (exact to 131,072 km) */
    ry = rel.y >> 4;
    rz = rel.z >> 4;
    o->X = (rx * cam->b.right.x + ry * cam->b.right.y + rz * cam->b.right.z) / 256;
    o->Y = (rx * cam->b.up.x + ry * cam->b.up.y + rz * cam->b.up.z) / 256;
    o->Z = (rx * cam->b.front.x + ry * cam->b.front.y + rz * cam->b.front.z) / 256;
    for (ax = o->X, ay = o->Y; ax > 65535 || ax < -65535 || ay > 65535 || ay < -65535; ax /= 2, ay /= 2)
        ;                               /* (lf_atan2's products overflow past 2^17) */
    o->dir = (short)lf_atan2(ax, ay);
    if (o->Z < 16)                      /* (within a km of its plane: as behind) */
        return 1;
    o->ahead = 1;
    ax = cam->cx + o->X * cam->focal / o->Z;
    ay = cam->cy - o->Y * cam->focal / o->Z;
    o->sx = (short)(ax < -16000 ? -16000 : ax > 16000 ? 16000 : ax);
    o->sy = (short)(ay < -16000 ? -16000 : ay > 16000 ? 16000 : ay);
    sc = (int)((long)scale_of(&rel) * cam->focal / 400);   /* (the sprites' size is the flight's, at its focal length) */
    t = slot_type(slot);
    o->half = (short)(t ? (long)t->lod0_px * sc / 512 : 4);
    if (o->half < 2)
        o->half = 2;
    /* drawn exactly this frame: the frame's plan, if this is the camera it was made for */
    set = ships_find(gInstanceShipTypes[slot]);
    how = set >= 0 && ships_turn_wanted(set, sc) ? SHIP_AS_EXACT : SHIP_AS_SETS;
    flight_camera(&main_cam);
    if (!_fmemcmp(&main_cam, cam, sizeof main_cam))
        for (i = 0; i < g_nplan; i++)
            if (g_plan[i].slot == slot) {
                o->exact = g_plan[i].exact != 0;
                how = plan_how(i);
            }
    basis_of(slot, &ship);
    if (set >= 0 && ships_box(set, &rel, &cam->b, &ship, sc, how, cam->cx, cam->cy, cam->focal, box)) {
        o->x0 = (short)box[0];
        o->y0 = (short)box[1];
        o->x1 = (short)box[2];
        o->y1 = (short)box[3];
    } else {
        o->x0 = o->sx - o->half;
        o->y0 = o->sy - o->half;
        o->x1 = o->sx + o->half;
        o->y1 = o->sy + o->half;
    }
    o->visible = o->x1 > cam->vx0 && o->x0 < cam->vx1 && o->y1 > cam->vy0 && o->y0 < cam->vy1;
    return 1;
}

#ifdef RV_TESTS
/* the /boxes check: brackets at each box's corners (the sprite's cyan, the steady square amber), clipped to the band
 * and the opening as the world is (hud.c) */
static void boxes_draw(int y0, int ey0)
{
    static const char __far s_box[] = "boxes: frame %u slot %d at %d,%d: box %d,%d - %d,%d, half %d, way %d, %s%s";
    RvCamera cam;
    EntBox b;
    int run0, run1, s;
    if (!vid_run(&run0, &run1))
        return;
    flight_camera(&cam);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++) {
        if (!flight_ent_box(s, &cam, &b))
            continue;
        if (y0 <= ey0 && (frameCounter % 100 == 1 || (g_record && frameCounter % 30 == 0)))
            rv_logf(s_box, frameCounter, s, b.sx, b.sy, b.x0, b.y0, b.x1, b.y1, b.half, b.dir,
                    b.visible ? "visible" : b.ahead ? "ahead, off the view" : "behind", b.exact ? ", exact" : "");
        if (!b.visible)
            continue;
        hud_brackets(b.sx - b.half, b.sy - b.half, b.sx + b.half + 1, b.sy + b.half + 1, UI_AMBER3);
        hud_brackets(b.x0, b.y0, b.x1, b.y1, UI_HILITE);
    }
}

/* --- the collision demo (RV.EXE /collidedemo): two fighters flown into each other ---
 *
 * The user's (2026-10-21): ship to ship collisions, coarse then fine, a
 * vector back that rectifies the intersection and shoves the ships apart.
 * Three rounds of four seconds: slot 1 (the enemy's type) flies right and
 * slot 2 (the player's) left, 2,600 km ahead, 14 km a frame each; slot 1
 * level with slot 2, then 120 km above it (their boxes overlap: a hit only if
 * a vital is in the other's box), then 700 km (inside each other's bounding
 * spheres, nothing in a box).  A hit's push is shared, half each.  Drawn
 * over them: their hit boxes (red while they hit) and vitals (the core
 * white, the engines violet, the rest yellow). */
#define CD_ROUNDS 3
#define CD_FRAMES 120
static const short __far g_cd_up[CD_ROUNDS] = { 0, 120, 700 };
static LfVec __far g_cd_pos[2];
static unsigned char __far g_cd_hit;
static int __far g_cd_coarse[CD_ROUNDS], __far g_cd_hits[CD_ROUNDS];

static void collide_demo_step(void)
{
    static const char __far s_hit[] = "collide: frame %u round %d: a hit %ld km deep, the push %ld,%ld,%ld km";
    int r = g_demo_frame / CD_FRAMES, t = g_demo_frame % CD_FRAMES, i;
    LfVec push;
    int32_t depth;
    if (r >= CD_ROUNDS) {
        missionStatus = 1;              /* landed */
        return;
    }
    if (t == 0) {
        g_cd_pos[0].x = -256L * 1500;
        g_cd_pos[0].y = 256L * g_cd_up[r];
        g_cd_pos[0].z = 256L * 2600;
        g_cd_pos[1].x = 256L * 1500;
        g_cd_pos[1].y = 0;
        g_cd_pos[1].z = 256L * 2600;
    } else {
        g_cd_pos[0].x += 256L * 14;
        g_cd_pos[1].x -= 256L * 14;
    }
    ship_face(1, 256, 0);
    ship_face(2, 768, 0);
    for (i = 0; i < 2; i++)
        *(LfVec *)&gPositionVector[1 + i] = g_cd_pos[i];
    g_cd_hit = 0;
    if (collide_coarse(1, 2))
        g_cd_coarse[r]++;
    if (collide_ships(1, 2, &push, &depth)) {
        if (g_cd_hits[r]++ < 4)
            rv_logf(s_hit, frameCounter, r, depth / 256, push.x / 256, push.y / 256, push.z / 256);
        g_cd_hit = 1;
        g_cd_pos[0].x -= push.x / 2;    /* (half each) */
        g_cd_pos[0].y -= push.y / 2;
        g_cd_pos[0].z -= push.z / 2;
        g_cd_pos[1].x += push.x - push.x / 2;
        g_cd_pos[1].y += push.y - push.y / 2;
        g_cd_pos[1].z += push.z - push.z / 2;
        for (i = 0; i < 2; i++)
            *(LfVec *)&gPositionVector[1 + i] = g_cd_pos[i];
    }
    g_demo_frame++;
}

static void collide_report(void)
{
    static const char __far s_r[] = "collide: round %d (slot 1 %d km above): in each other's spheres %d frames, a hit %d";
    int r;
    for (r = 0; r < CD_ROUNDS; r++)
        rv_logf(s_r, r, g_cd_up[r], g_cd_coarse[r], g_cd_hits[r]);
}

/* a world point on the player's screen */
static int to_screen(const LfVec *w, int *sx, int *sy)
{
    LfBasis cam;
    long rx = (w->x - gPositionVector[0].x) >> 4, ry = (w->y - gPositionVector[0].y) >> 4,
         rz = (w->z - gPositionVector[0].z) >> 4, X, Y, Z;
    basis_of(0, &cam);
    X = (rx * cam.right.x + ry * cam.right.y + rz * cam.right.z) / 256;
    Y = (rx * cam.up.x + ry * cam.up.y + rz * cam.up.z) / 256;
    Z = (rx * cam.front.x + ry * cam.front.y + rz * cam.front.z) / 256;
    if (Z < 16)
        return 0;
    X = SCR_W / 2 + X * 400 / Z;
    Y = g_cam_cy - Y * 400 / Z;
    *sx = (int)(X < -4000 ? -4000 : X > 4000 ? 4000 : X);
    *sy = (int)(Y < -4000 ? -4000 : Y > 4000 ? 4000 : Y);
    return 1;
}

static void collide_overlay(void)
{
    LfVec c[8], v[16];
    int sx[8], sy[8], on[8], s, k, b, n, x, y;
    if (!vid_run(&k, &b))
        return;
    for (s = 1; s <= 2; s++) {
        n = collide_shape(s, c, v, 16);
        for (k = 0; k < 8; k++)
            on[k] = to_screen(&c[k], &sx[k], &sy[k]);
        for (k = 0; k < 8; k++)
            for (b = 1; b < 8; b <<= 1)
                if (!(k & b) && on[k] && on[k | b])
                    hud_line(sx[k], sy[k], sx[k | b], sy[k | b], g_cd_hit ? UI_RED : UI_GREY5);
        for (k = 0; k < n; k++)
            if (to_screen(&v[k], &x, &y)) {
                int col = k == 0 ? UI_WHITE : k <= slot_type(s)->nengines ? 159 : UI_AMBER4, j;
                for (j = -1; j <= 1; j++)
                    hud_run(x - 1, x + 2, y + j, col);
            }
    }
}

/* --- the flames' self-test (RV.EXE /flametest, in the flames scene) ---
 * The user (2026-10-21): an engine's flame must line up exactly with its ship as drawn, by the same means for the
 * ships drawn exactly and those from the sets, at every distance.  Each of the scene's six types, at distances
 * from twice full size to past the sets' smallest, in 40 orientations (every eighth a yaw only: an exact ship's
 * unturned copy), is drawn as the flight draws it (into an empty band: the choices only), exactly (every other
 * one at half height) and from the sets.  Checked: the view
 * and turn ships_drawn gives the flames are the ones the draw used (ships_last_drawn); and each flame's baked way
 * is within half a pixel, at its tail, of the way the drawn picture's tail points, worked out here from the
 * drawn view and turn (lf_view_rot, lf_sprite_point), not by flames.c.  Beside it, how far off the way before
 * was (the true roll on every picture, ways baked every 11.25 degrees). */
#define FT_DISTS 10
#define FT_TURNS 40
#define FT_TOL 530                      /* milli-pixels at a tail: half a pixel (the ways' steps), and the ways' grain
                                           (16384ths of a turn: 0.03 px on a 70 px flame) */
static const short __far g_ft_km[FT_DISTS] = { 1250, 1800, 2500, 3500, 5000, 7000, 10000, 14000, 20000, 28000 };

/* how far (milli-pixels) the tail of a flame len16 long (x16) pointing way w (16384ths) is from where it would be
 * pointing the way (x, y) points */
static long ft_miss(int w, long x, long y, int len16)
{
    long c, sn, e, dx, dy;
    flames_way_vec(w, &c, &sn);
    while (x > 16384 || x < -16384 || y > 16384 || y < -16384) {
        x /= 2;
        y /= 2;
    }
    e = (long)lf_sqrt((unsigned long)(x * x + y * y));
    if (!e)
        return 0;
    dx = c - x * 16384 / e;             /* (the two ways' unit vectors apart, 16384 = 1.0) */
    dy = sn - y * 16384 / e;
    return (long)lf_sqrt((unsigned long)(dx * dx + dy * dy)) * len16 / 262;   /* (x16: 16 x 16384 / 1000 = 262) */
}

/* the way the tail of a picture of view v (lattice nb, nh) turned by roll points on screen, 16384 = 1.0 */
static void ft_tail(int v, int roll, int nb, int nh, int32_t *x, int32_t *y)
{
    LfViewRot rot;
    int32_t z;
    lf_view_rot(v, nb, nh, &rot);
    lf_sprite_point(&rot, roll, 0, 0, -16384, x, y, &z);
}

/* the self-test's picture (FLAMEST.BMP): eight ships turned every way, as the flight draws them and their flames,
 * a dashed white line along each picture's axis (where its tail points) and a red one where its flames pointed
 * before.  Top: from the sets at their roll steps (5,000 km); below, at a size the sets have no roll steps for
 * (3,000 km), drawn exactly: two at half height (the plan's beyond its first two), two in full */
static void ft_sheet(void)
{
    static const unsigned char types[8] = { 0x02, 0x04, 0x08, 0x0A, 0x02, 0x09, 0x04, 0x06 };
    static const short turn[8][3] = { { 300, 40, 90 }, { 700, -60, 200 }, { 230, 100, 330 }, { 800, 20, 140 },
                                      { 280, -30, 110 }, { 740, 50, 300 }, { 250, 70, 170 }, { 760, -40, 60 } };
    LfBasis cam, ship[8];
    LfVec rel[8];
    LfPlace p[8];
    FlameSet fs[8];
    int set[8], sc[8], ax[8][4], how[8], c, y0, y1, y;
    basis_of(0, &cam);
    flames_frame();
    for (c = 0; c < 8; c++) {
        RvVec3 f = *(const RvVec3 *)&cam.front, r = *(const RvVec3 *)&cam.right, u = *(const RvVec3 *)&cam.up;
        long km = c < 4 ? 5000 : 3000; int32_t ox, oy, tx, ty;
        int dx = 80 + 160 * (c % 4) - SCR_W / 2, dy = g_cam_cy - (c < 4 ? 100 : 290), nb, nh, ex = c >= 6, a;
        set[c] = ships_find(types[c]);
        if (set[c] < 0)
            return;
        vec_turn(&f, &r, turn[c][0]);
        vec_turn(&f, &u, turn[c][1]);
        vec_turn(&r, &u, turn[c][2]);
        ship[c].front = *(LfVec *)&f;
        ship[c].right = *(LfVec *)&r;
        ship[c].up = *(LfVec *)&u;
        rel[c].x = cam.front.x * km + (cam.right.x * km * dx + cam.up.x * km * dy) / 400;
        rel[c].y = cam.front.y * km + (cam.right.y * km * dx + cam.up.y * km * dy) / 400;
        rel[c].z = cam.front.z * km + (cam.right.z * km * dx + cam.up.z * km * dy) / 400;
        sc[c] = scale_of(&rel[c]);
        how[c] = c < 4 ? SHIP_AS_SETS : SHIP_AS_EXACT;
        ships_drawn(set[c], how[c], &rel[c], &cam, &ship[c], sc[c], &p[c], &nb, &nh);
        flames_add(ship_type(types[c]), &p[c], nb, nh, sc[c], c & 1, c, &fs[c]);
        ft_tail(p[c].view, p[c].roll, nb, nh, &tx, &ty);
        ships_lattice(set[c], ex, &nb, &nh);   /* (before: the true roll in the placement's lattice, 11.25 degrees) */
        {
            LfPlace p0;
            lf_place(&rel[c], &cam, &ship[c], SCR_W / 2, g_cam_cy, 400, nb, nh, &p0);
            ft_tail(p0.view, p0.roll, nb, nh, &ox, &oy);
        }
        a = lf_atan2(oy, ox);
        a = (a + 16) / 32 * 32;
        ax[c][0] = (int)(tx * 70 / 16384);
        ax[c][1] = (int)(ty * 70 / 16384);
        ox = (long)lf_sqrt((unsigned long)((ox / 4) * (ox / 4) + (oy / 4) * (oy / 4))) * 4;   /* (its length, its way) */
        ax[c][2] = (int)(lf_cos(a) * ox / 16384 * 70 / 16384);
        ax[c][3] = (int)(lf_sin(a) * ox / 16384 * 70 / 16384);
    }
    vid_dirty(0, SCR_H);
    while (vid_dirty_band(&y0, &y1)) {
        for (y = y0; y < y1; y++)
            _fmemset(vid_row(y), 0, SCR_W);
        for (c = 0; c < 8; c++) {
            if (!fs[c].front)
                flames_draw(&fs[c], y0, y1, 0, 0, 0);
            ships_draw_as(set[c], how[c], c == 4 || c == 5 ? 2 : 1, &rel[c], &cam, &ship[c], sc[c], y0, y1, 0, SCR_W,
                          0, 0, 0);
            if (fs[c].front)
                flames_draw(&fs[c], y0, y1, 0, 0, 0);
            gfx_line(p[c].sx, p[c].sy, p[c].sx + ax[c][2], p[c].sy + ax[c][3], UI_RED, 0, y0, y1);
            gfx_line(p[c].sx, p[c].sy, p[c].sx + ax[c][0], p[c].sy + ax[c][1], UI_WHITE, 1, y0, y1);
        }
        vid_flip();
    }
    shot_save("FLAMEST");
}

static void flame_test(void)
{
    static const unsigned char types[6] = { 0x08, 0x02, 0x04, 0x0A, 0x09, 0x06 };
    static const char __far s_row[] = "flametest: %5d km (scale %3d): %3d drawn (exact %d, unturned %d; sets: roll "
        "steps %d, unturned %d), %d pose mismatches; %4d flames: worst %ld.%03ld px off at the tail, "
        "mean %ld.%03ld, %d over a pixel (before: worst %ld.%03ld, mean %ld.%03ld, %d over a pixel)";
    static const char __far s_bad[] = "flametest: MISMATCH type %02X %d km turn %d exact %d: drawn path %d view %d "
        "roll %d, the flames' path %d view %d roll %d";
    static const char __far s_end[] = "flametest: %Fs: %ld draws, %ld pose mismatches, %ld flames, the worst %ld.%03ld "
        "px off at the tail (before: %ld.%03ld)";
    static const char __far s_ok[] = "OK", s_fail[] = "FAILED";
    LfBasis cam;
    long all_draws = 0, all_bad = 0, all_flames = 0, all_worst = 0, all_before = 0;
    int di;
    basis_of(0, &cam);
    for (di = 0; di < FT_DISTS; di++) {
        long km = g_ft_km[di], worst = 0, sum = 0, bworst = 0, bsum = 0;
        int n[24], bad = 0, flames = 0, drawn_n = 0, sc = 0, ti, o, ex, i, over = 0, bover = 0;
        memset(n, 0, sizeof n);
        for (ti = 0; ti < 6; ti++) {
            int set = ships_find(types[ti]);
            const RvShipType __far *t = ship_type(types[ti]);
            const ShipSet __far *ss;
            if (set < 0 || !t)
                continue;
            ss = ships_set(set);
            for (o = 0; o < FT_TURNS; o++) {
                RvVec3 f = *(const RvVec3 *)&cam.front, r = *(const RvVec3 *)&cam.right, u = *(const RvVec3 *)&cam.up;
                LfBasis ship;
                LfVec rel;
                if (o % 8 == 0) {
                    vec_turn(&f, &r, o * 37);
                } else {
                    vec_turn(&f, &r, o * 97 + 13);
                    vec_turn(&f, &u, o * 61 + ti * 29);
                    vec_turn(&r, &u, o * 151 + ti * 71);
                }
                ship.front = *(LfVec *)&f;
                ship.right = *(LfVec *)&r;
                ship.up = *(LfVec *)&u;
                {                       /* (off the middle of the screen, but the yaws only dead ahead) */
                    int dx = o % 8 ? o % 5 - 2 : 0, dy = o % 8 ? o / 5 % 3 - 1 : 0;
                    rel.x = cam.front.x * km + (cam.right.x * km * dx + cam.up.x * km * dy) / 12;
                    rel.y = cam.front.y * km + (cam.right.y * km * dx + cam.up.y * km * dy) / 12;
                    rel.z = cam.front.z * km + (cam.right.z * km * dx + cam.up.z * km * dy) / 12;
                }
                sc = scale_of(&rel);
                for (ex = 0; ex < 2; ex++) {    /* (SHIP_AS_SETS, _EXACT) */
                    LfPlace p, p0;
                    FlameSet fs;
                    int path, dv, dr, pp, nb, nh, lnb, lnh, k;
                    int32_t tx, ty, ox, oy, oz;
                    ships_draw_as(set, ex, o & 1 ? 2 : 1, &rel, &cam, &ship, sc, 0, 0, 0, SCR_W, 0, 0, 0);
                    path = ships_last_drawn(&dv, &dr);
                    pp = ships_drawn(set, ex, &rel, &cam, &ship, sc, &p, &nb, &nh);
                    if (pp != path || (path && (p.view != dv || ((p.roll - dr) & 1023)))) {
                        if (bad++ < 4)
                            rv_logf(s_bad, types[ti], (int)km, o, ex, path, dv, dr, pp, p.view, p.roll);
                    }
                    if (!path)
                        continue;
                    drawn_n++;
                    n[path < 24 ? path : 0]++;
                    /* the drawn picture's tail, from what the draw used */
                    lnb = path >= SHIP_PATH_EXACT_BLIT || !ss->rvs ? ss->nb0 : ss->nb1;
                    lnh = path >= SHIP_PATH_EXACT_BLIT || !ss->rvs ? ss->nh0 : ss->nh1;
                    ft_tail(dv, dr, lnb, lnh, &tx, &ty);
                    /* the flames as the flight makes them */
                    flames_frame();
                    flames_add(t, &p, nb, nh, sc, o & 1, 1, &fs);
                    /* before: the true roll in the placement's lattice, the way to the nearest 11.25 degrees */
                    ships_lattice(set, ex == SHIP_AS_EXACT, &nb, &nh);
                    lf_place(&rel, &cam, &ship, SCR_W / 2, g_cam_cy, 400, nb, nh, &p0);
                    ft_tail(p0.view, p0.roll, nb, nh, &ox, &oy);
                    {
                        long ax = ox < 0 ? -ox : ox, ay = oy < 0 ? -oy : oy;
                        int a = (lf_atan2(ay, ax) + 16) / 32 * 32 * 16;
                        if (a > 4096) a = 4096;
                        oz = ox < 0 ? (oy < 0 ? 8192 + a : 8192 - a) : oy < 0 ? (16384 - a) & 16383 : a;
                    }
                    for (k = fs.first; k < fs.first + fs.n; k++) {
                        int w, len16;
                        long m, mb;
                        if (!flames_way(k, &w, &len16))
                            continue;   /* (end on: no way shows) */
                        m = ft_miss(w, tx, ty, len16);
                        mb = ft_miss((int)oz, tx, ty, len16);
                        flames++;
                        sum += m;
                        bsum += mb;
                        if (m > worst) worst = m;
                        if (mb > bworst) bworst = mb;
                        over += m > 1000;
                        bover += mb > 1000;
                    }
                }
            }
        }
        for (i = 1; i <= 4; i++)
            n[0] += n[i];               /* (the sets' roll steps, flipped or not) */
        rv_logf(s_row, (int)km, sc, drawn_n, n[SHIP_PATH_EXACT], n[SHIP_PATH_EXACT_BLIT], n[0],
                n[SHIP_PATH_UNTURNED] + n[SHIP_PATH_SCALED], bad, flames, worst / 1000, worst % 1000,
                flames ? sum / flames / 1000 : 0L, flames ? sum / flames % 1000 : 0L, over, bworst / 1000, bworst % 1000,
                flames ? bsum / flames / 1000 : 0L, flames ? bsum / flames % 1000 : 0L, bover);
        all_draws += drawn_n;
        all_bad += bad;
        all_flames += flames;
        if (worst > all_worst) all_worst = worst;
        if (bworst > all_before) all_before = bworst;
    }
    rv_logf(s_end, all_bad || all_worst > FT_TOL || !all_flames ? s_fail : s_ok, all_draws, all_bad, all_flames,
            all_worst / 1000, all_worst % 1000, all_before / 1000, all_before % 1000);
    ft_sheet();
    flames_frame();
}

#endif /* RV_TESTS */

/* a flight's first comm, three seconds out: the first of the mission's people with comm art (its talk's speakers,
 * the squadron's pilots) says their own line on the left VDU (until the missions have comm lines of their own) */
static void comm_launch(void)
{
    int i;
    if (!g_m || g_sys.muted)
        return;
    for (i = 0; i < (int)g_m->ntalk; i++)
        if (comms_say(comms_voice(g_m->talk[i].speaker), 0, (const char __far *)g_pilots[g_m->talk[i].speaker].line))
            return;
}

/* the aliens' hail: the first Aera or Rlaan fighter to come within HAIL_KM hails the player on the right VDU, once
 * a flight, after the launch's comm (its pilot the side's voice for that ship: data/aliens.csv; until the missions
 * script their own) */
#define HAIL_KM 3000L
static unsigned char __far g_hailed;

static void comm_hail(void)
{
    static const char __far s_hail[] = "flight: frame %u: slot %d (type %02X, side %d) hails, voice %d";
    int s, v;
    if (!g_m || g_hailed || g_sys.muted || frameCounter <= 90 || (frameCounter & 7) || comms_on(0) || comms_on(1))
        return;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++) {
        long dx, dy, dz;
        if (maybeEntityType[s] != 0xC || commAnimInfo[s] < 1 || commAnimInfo[s] > 2)
            continue;
        dx = (gPositionVector[s].x - gPositionVector[0].x) / 256;     /* (km) */
        dy = (gPositionVector[s].y - gPositionVector[0].y) / 256;
        dz = (gPositionVector[s].z - gPositionVector[0].z) / 256;
        if (dx > HAIL_KM || dx < -HAIL_KM || dy > HAIL_KM || dy < -HAIL_KM || dz > HAIL_KM || dz < -HAIL_KM
            || dx * dx + dy * dy + dz * dz > HAIL_KM * HAIL_KM)
            continue;
        v = comms_voice_for(commAnimInfo[s], gInstanceShipTypes[s]);
        g_hailed = 1;
        rv_logf(s_hail, frameCounter, s, gInstanceShipTypes[s], commAnimInfo[s], v);
        comms_say(v, 1, NULL);
        return;
    }
}

#ifdef RV_TESTS
/* --- the comm demo (RV.EXE /commdemo): what a comm costs (in the log), then every voice says its own line, the
 * aliens first, on each VDU by turns --- */
static void comm_demo_next(void)
{
    static int voice = -1, vdu = 1;
    if (voice == -1) {                  /* (first the bench: its draw is timed in the frames after) */
        voice = -2;
        comms_bench();
        return;
    }
    if (!comms_voices())
        return;
    if (voice < 0)                      /* (from the first alien: the newest faces first) */
        voice = comms_voice_for(1, -1) > 0 ? comms_voice_for(1, -1) : 0;
    else
        voice = (voice + 1) % comms_voices();
    comms_say(voice, vdu, NULL);
    vdu ^= 1;
}

/* --- the hull demo (RV.EXE /hulldemo): a turning fighter shot through by a grid of rays ---
 *
 * The user's pick (2026-10-21): the ships' three-view silhouette hulls,
 * "useful especially for point blaster shots".  Slot 1 (the enemy's type)
 * 1,800 km ahead, turning every way (yaw, pitch and roll at their own rates);
 * each frame a grid of 24 x 16 rays from the eye through its box on the
 * screen (flight_ent_box), each a segment from the eye to past the ship
 * against its hull (collide_segment, as a bolt's last move would be): a hit
 * red where it went in, a miss a grey dot.  The red should fill the sprite (a
 * three-view hull is a little fuller than the ship where it is hollow from
 * every side).  Ten seconds. */
#define HD_GX 24
#define HD_GY 16
static unsigned char __far g_hd_hit[HD_GY][HD_GX];
static short __far g_hd_x0, __far g_hd_y0, __far g_hd_dx, __far g_hd_dy;
static unsigned long __far g_hd_ticks, __far g_hd_tests, __far g_hd_hits, __far g_hd_bolt_us100;

/* what a bolt's last move (75 km, a frame's) against the ship costs: random ones through its box, timed */
static void hull_bolt_time(void)
{
    static unsigned long __far seed = 99;
    long H = collide_half(1);
    unsigned long t0 = clock240(), dt, it = 0;
    LfVec a, b;
    do {
        int k;
        long d[3];
        for (k = 0; k < 3; k++) {
            seed = seed * 1103515245UL + 12345UL;
            d[k] = (long)((seed >> 8) % 2001) - 1000;
        }
        seed = seed * 1103515245UL + 12345UL;
        a.x = gPositionVector[1].x + (long)((seed >> 4) % 1001 - 500) * H / 500;
        seed = seed * 1103515245UL + 12345UL;
        a.y = gPositionVector[1].y + (long)((seed >> 4) % 1001 - 500) * H / 1500;
        seed = seed * 1103515245UL + 12345UL;
        a.z = gPositionVector[1].z + (long)((seed >> 4) % 1001 - 500) * H / 500;
        b.x = a.x + d[0] * (75L * 256) / 1000;   /* (75 km at most on an axis) */
        b.y = a.y + d[1] * (75L * 256) / 1000;
        b.z = a.z + d[2] * (75L * 256) / 1000;
        collide_segment(1, &a, &b, 0, 0);
        it++;
    } while ((dt = clock240() - t0) < 120);
    g_hd_bolt_us100 = dt * 416667UL / it;    /* (hundredths of a microsecond each) */
}

static void hull_demo_step(void)
{
    RvCamera cam;
    EntBox b;
    LfVec from, to, hit;
    long k;
    int gx, gy;
    int16_t t;
    unsigned long t0;
    gPositionVector[1].x = 0;
    gPositionVector[1].y = 0;
    gPositionVector[1].z = 256L * 1800;
    if (g_demo_frame == 1)
        hull_bolt_time();
    ship_face(1, (int)(g_demo_frame * 1024L / 300 % 1024), (int)((long)lf_sin((int)(g_demo_frame * 1024L / 200)) * 200 >> 14));
    vec_turn(&gOrientationRightVector[1], &gOrientationUpVector[1], (int)(g_demo_frame * 1024L / 450 % 1024));
    ship_orthonormalise(1);
    _fmemset(g_hd_hit, 0, sizeof g_hd_hit);
    flight_camera(&cam);
    if (flight_ent_box(1, &cam, &b) && b.visible) {
        g_hd_dx = (short)((b.x1 - b.x0) / HD_GX + 1);
        g_hd_dy = (short)((b.y1 - b.y0) / HD_GY + 1);
        g_hd_x0 = b.x0 + g_hd_dx / 2;
        g_hd_y0 = b.y0 + g_hd_dy / 2;
        from = cam.pos;
        k = (b.Z * 16 + collide_half(1) * 2) / 400;   /* (positions a pixel off the axis, past the ship) */
        t0 = clock240();
        for (gy = 0; gy < HD_GY; gy++)
            for (gx = 0; gx < HD_GX; gx++) {
                long px = g_hd_x0 + gx * g_hd_dx - cam.cx, py = cam.cy - (g_hd_y0 + gy * g_hd_dy);
                to.x = from.x + (cam.b.front.x * 400L + cam.b.right.x * px + cam.b.up.x * py) / 256 * k;
                to.y = from.y + (cam.b.front.y * 400L + cam.b.right.y * px + cam.b.up.y * py) / 256 * k;
                to.z = from.z + (cam.b.front.z * 400L + cam.b.right.z * px + cam.b.up.z * py) / 256 * k;
                if (collide_segment(1, &from, &to, &hit, &t)) {
                    g_hd_hit[gy][gx] = 1;
                    g_hd_hits++;
                }
                g_hd_tests++;
            }
        g_hd_ticks += clock240() - t0;
        if (g_record) {                 /* (the grid and its hits, for checking them against the picture) */
            static const char __far s_g[] = "hullgrid: %u %d %d %d %d %s";
            char hex[HD_GX * HD_GY / 4 + 1];
            for (gy = 0, k = 0; gy < HD_GY; gy++)
                for (gx = 0; gx < HD_GX; gx += 4, k++)
                    hex[k] = "0123456789ABCDEF"[g_hd_hit[gy][gx] | g_hd_hit[gy][gx + 1] << 1 | g_hd_hit[gy][gx + 2] << 2
                                                | g_hd_hit[gy][gx + 3] << 3];
            hex[k] = 0;
            rv_logf(s_g, frameCounter, g_hd_x0, g_hd_y0, g_hd_dx, g_hd_dy, hex);
        }
    }
    if (++g_demo_frame >= DEMO_FRAMES)
        missionStatus = 1;              /* landed */
}

static void hull_report(void)
{
    static const char __far s_r[] = "hull: %lu rays from the eye through its box (%lu hits) took %lu.%02lu ms a frame, "
        "%lu us each; a bolt's 75 km move through its box %lu.%02lu us";
    unsigned long f = g_demo_frame ? g_demo_frame : 1, us = g_hd_tests ? g_hd_ticks * 4167UL / g_hd_tests : 0;
    rv_logf(s_r, g_hd_tests, g_hd_hits, g_hd_ticks * 1000 / 240 / f, g_hd_ticks * 100000 / 240 / f % 100, us,
            g_hd_bolt_us100 / 100, g_hd_bolt_us100 % 100);
}

static void hull_overlay(void)
{
    int gx, gy;
    if (g_record && !(frameCounter & 1))
        return;                         /* (recorded: every other frame clean, to check the hits against) */
    for (gy = 0; gy < HD_GY; gy++)
        for (gx = 0; gx < HD_GX; gx++) {
            int x = g_hd_x0 + gx * g_hd_dx, y = g_hd_y0 + gy * g_hd_dy;
            if (g_hd_hit[gy][gx]) {
                hud_run(x - 1, x + 1, y - 1, UI_RED);
                hud_run(x - 1, x + 1, y, UI_RED);
            } else {
                hud_run(x, x + 1, y, UI_GREY3);
            }
        }
}

#endif /* RV_TESTS */

static void draw_world(int y0, int y1)
{
    int ey0, rows, i;
    const short __far *ext = cockpit_extents(&ey0, &rows);
#ifdef RV_TESTS
    const ShipSet __far *bench = g_set_enemy >= 0 ? ships_set(g_set_enemy) : 0;
#endif
    LfBasis cam, ship;
    int path;
    unsigned long t0;
    g_world_calls++;
    hud_band(y0, y1, ext, ey0, rows);
    sky_draw(0, y0, y1, ext, ey0, rows);   /* the far things behind the stars (nebulae, galaxies), the stars, those
                                           over them (planets) */
    draw_stars(y0, y1);
    sky_draw(1, y0, y1, ext, ey0, rows);
#ifdef RV_TESTS
    if (g_lasers)
        lasers_draw_far(y0, y1, ext, ey0, rows);
#endif
    shots_draw_far(y0, y1, ext, ey0, rows);   /* the far shots: lines and a pixel or a few */
    rocks_draw_small(y0, y1, ext, ey0, rows);   /* the far rocks: a few pixels each */
    rocks_draw_begin();
    if (g_bl_cap)                       /* the far balls: a pixel or a few */
        balls_draw_far(y0, y1, ext, ey0, rows);
    if (g_sky && g_set_enemy >= 0) {
        basis_of(0, &cam);
        for (i = 0; i < 3; i++)
            ships_draw_sky(g_set_enemy, g_sky_sprites[i].v, &g_sky_sprites[i].dir, &g_world_up, &cam,
                           g_sky_sprites[i].scale, y0, y1, 0, SCR_W, ext, ey0, rows);
    }
    /* the ships, as the frame's plan has them: the nearest exactly, the rest from the sets */
    basis_of(0, &cam);
    for (i = 0; i < g_nplan; i++) {
        LfVec rel;
        int s = g_plan[i].slot, set = g_plan[i].set;
        rel_of(s, &rel);
        basis_of(s, &ship);
        rocks_draw_to(g_plan[i].depth, y0, y1, ext, ey0, rows);   /* the big rocks behind it */
        t0 = clock240();
        if (!g_plan[i].fl.front)        /* flames pointing away: behind the hull */
            flames_draw(&g_plan[i].fl, y0, y1, ext, ey0, rows);
        g_t_flames += clock240() - t0;
        t0 = clock240();
        /* (an exact one whose full-size views did not fit: from the sets) */
        path = ships_draw_as(set, plan_how(i), g_plan[i].exact ? g_plan[i].exact : 1, &rel, &cam, &ship,
                             g_plan[i].scale, y0, y1, 0, SCR_W, ext, ey0, rows);
        if (path == SHIP_PATH_EXACT_BLIT || path == SHIP_PATH_EXACT)
            g_t_exact += clock240() - t0;
        else
            g_t_sets += clock240() - t0;
        if (g_plan[i].fl.front) {       /* flames toward the camera: over it */
            t0 = clock240();
            flames_draw(&g_plan[i].fl, y0, y1, ext, ey0, rows);
            g_t_flames += clock240() - t0;
        }
        if (y0 <= ey0 && (frameCounter % 100 == 1 || (g_record && frameCounter % g_record == 0))) {
            /* now and then, where each ship was put (tests/ships.txt reads it) */
            LfPlace p;
            int nb, nh;
            ships_drawn(set, plan_how(i), &rel, &cam, &ship, g_plan[i].scale, &p, &nb, &nh);
            rv_log("lightfield: frame %u slot %d type %02X at %ld,%ld,%ld: view %d roll %d scale %d at %d,%d path %d",
                   frameCounter, s, gInstanceShipTypes[s], rel.x, rel.y, rel.z, p.view, p.roll, g_plan[i].scale, p.sx, p.sy, path);
        }
    }
    rocks_draw_rest(y0, y1, ext, ey0, rows);   /* the big rocks before every ship */
    if (g_lasers)                       /* the near bolts last: nearest of all */
        bolts_draw_near(y0, y1, ext, ey0, rows);
    if (g_bl_cap)
        balls_draw_near(y0, y1, ext, ey0, rows);
    shots_draw_near(y0, y1, ext, ey0, rows);
    missiles_draw(y0, y1, ext, ey0, rows);
    logic_hud();                        /* the game's own marks over the view */
#ifdef RV_TESTS
    if (g_boxes)
        boxes_draw(y0, ey0);
    if (g_collide_demo)
        collide_overlay();
    if (g_hull_demo)
        hull_overlay();
    /* the benchmark's copies */
    if (bench)
        for (i = 0; i < g_nbench; i++)
            ships_blit_size(bench, 1, g_bench_view, g_bench[i].x, g_bench[i].y, y0, y1, 0, SCR_W, ext, ey0, rows);
#endif
}

static int g_ptr_y0, g_ptr_y1;

static void __far __cdecl draw_pointer_body(void)
{
    /* the steering cross where the pointer is, in mouse mode */
    int x = g_scene.px, y = g_scene.py;
    if (g_keyboard_steer || g_bolt_demo || g_ball_demo || g_missile_demo || g_sky_demo || g_collide_demo || g_hull_demo)   /* (the demo's middle bolt is where the cross would be) */
        return;
    gfx_fill(x - 5, y, 11, 1, UI_WHITE, g_ptr_y0, g_ptr_y1);
    gfx_fill(x, y - 5, 1, 11, UI_WHITE, g_ptr_y0, g_ptr_y1);
}

static void flight_draw(int y0, int y1)
{
    cockpit_draw(y0, y1, &g_st, draw_world);
    g_ptr_y0 = y0;
    g_ptr_y1 = y1;
    rv_cockpit_pointer(draw_pointer_body);
}

static void mfd_draw(int which, const RvRect __far *r, int y0, int y1)
{
    /* a comm on this VDU, else on the right one the target's view, clipped to the glass */
    const ShipSet __far *set;
    int si = g_sys.target >= 0 ? ships_find(gInstanceShipTypes[g_sys.target]) : g_set_enemy;
    if (comms_draw(which, r, y0, y1))
        return;
    if (which != 1 || si < 0 || (rvCockpitDamage >> 1 & 1))   /* (the target's view; none with the MFD damaged) */
        return;
    set = ships_set(si);
    if (y1 <= r->y || y0 >= r->y + r->h)
        return;
    ships_blit_size(set, 1, 10, r->x + r->w / 2, r->y + r->h / 2, y0 > r->y ? y0 : r->y,
                    y1 < r->y + r->h ? y1 : r->y + r->h, r->x, r->x + r->w, 0, 0, 0);
}

#ifdef RV_TESTS
static int lasers_far(void);
#endif

/* --- the ship's systems (panel.h): what the cockpit's controls show and set ---
 *
 * The user's table (data/panels.json, 2026-10-22): EMCON, the sensor mode, the IFF and master arm are switches whose
 * flags combat will read (the lock rules already do); the radio tunes to whoever talks (WING 267, the carrier 315,
 * GUARD 243 for an alien's hail) and its frequency knobs step there; the afterburner (Tab) burns fuel and heat; the
 * signature is what an enemy will find you by.  Keys: K EMCON, V sensor, I IFF, U comms mute, N nav point, M master
 * arm, T next target, Y the nearest enemy, L hold the lock, G radar range, H the survival display's page, X a decoy,
 * C call the carrier to land, Tab the afterburner (held), Ctrl+E twice eject; a click on a control works it. */
static const unsigned char __far g_freq[3][3] = { { 2, 6, 7 }, { 3, 1, 5 }, { 2, 4, 3 } };   /* MHz: WING, CARRIER, GUARD */
static unsigned long __far g_eject_at;  /* Ctrl+E (or the handle) once: when; a second within 2 s ejects */
static int __far g_heat10;              /* heat x 10, following the engine with a lag */
static int __far g_last_side = -1;      /* who talked last frame */
static unsigned __far g_shield_was;     /* the shields last frame (a drop flickers the lights) */
static int __far g_flicker;             /* frames of flicker left */

/* C calls the carrier to land (the user's call, 2026-10-23: no more landing when a timer ran out): the player's
 * own voice asks on the left VDU, and the flight ends when the line is said (at once with the comms muted) */
static unsigned char __far g_landing;   /* 1 the call made, 2 and on the VDU */
static unsigned long __far g_land_at;   /* when to land, if the call shows no face */

static void call_carrier(void)
{
    static const char __far s_call[] = "Vigilance, Wildcard. Requesting clearance to land.";
    int i;
    if (g_landing)
        return;
    g_landing = 1;
    g_land_at = clock240() + 240;
    rv_log("flight: C: the player calls the carrier to land");
    if (g_sys.muted)
        return;
    for (i = 0; i < (int)g_npilots; i++)
        if (g_pilots[i].role == RV_ROLE_HERO && comms_say(comms_voice(i), 0, s_call)) {
            g_landing = 2;              /* (on the VDU: landing when it is said) */
            return;
        }
}

static void systems_start(void)
{
    _fmemset(&g_sys, 0, sizeof g_sys);
    g_landing = 0;
    g_sys.emcon = EMCON_ACTIVE;
    g_sys.iff = 1;
    g_sys.armed = 1;                    /* (out of the launch tube: armed) */
    g_sys.decoys = 6;
    g_sys.radar_range = 1;
    g_sys.chan = CHAN_WING;
    _fmemcpy(g_sys.digit, g_freq[CHAN_WING], 3);
    g_sys.target = -1;
    g_sys.fuel = 1000;
    g_sys.hull = 100;
    g_sys.lights = 1;
    g_sys.target_km = g_sys.nav_km = -1;
    g_heat10 = 0;
    g_last_side = -1;
    g_eject_at = 0;
    g_flicker = 0;
}

static int is_ship(int s)
{
    return s > 0 && s <= RV_MAX_SHIP_SLOT && (maybeEntityType[s] == 0xC || maybeEntityType[s] == 0xD);
}

/* the target: the next ship after the one chosen (then none), or the nearest enemy */
static void target_next(void)
{
    int s;
    for (s = g_sys.target + 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (is_ship(s)) {
            g_sys.target = s;
            return;
        }
    g_sys.target = -1;
}

static void target_nearest(void)
{
    int s, best = -1;
    long d, bd = 0;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (is_ship(s) && commAnimInfo[s] != 0 && ((d = player_km(s)) < bd || best < 0)) {
            best = s;
            bd = d;
        }
    if (best >= 0)
        g_sys.target = best;
}

static void eject_pull(void)
{
    unsigned long now = clock240();
    if (g_eject_at && now - g_eject_at < 480) {
        rv_log("flight: ejected");
        missionStatus = 2;
    }
    g_eject_at = now;
}

/* a control worked (a key's or a click's): its function and argument (RV_PF_*) */
static void system_action(int fn, int arg)
{
    switch (fn) {
    case RV_PF_EMCON_LAMP:
        if (arg < 3) { g_sys.emcon = (unsigned char)arg; break; }
        /* (fall through: the STEALTH lamp cycles) */
    case RV_PF_EMCON_KNOB: case RV_PF_EM_LAMP:
        g_sys.emcon = (unsigned char)((g_sys.emcon + 2) % 3);
        break;
    case RV_PF_SENSOR_LAMP: g_sys.sensor = (unsigned char)arg; break;
    case RV_PF_IFF_LAMP: case RV_PF_IFF_KNOB: g_sys.iff ^= 1; break;
    case RV_PF_COM_LAMP: case RV_PF_COM_ON: case RV_PF_VOL_KNOB: g_sys.muted ^= 1; break;
    case RV_PF_NAV_LAMP: case RV_PF_NAV_KNOB: case RV_PF_AUX_NAV:
        if (g_m && g_m->nnav) g_sys.nav = (unsigned char)((g_sys.nav + 1) % g_m->nnav);
        break;
    case RV_PF_TARGET_LAMP: case RV_PF_TARGET_ID: case RV_PF_RANGE_BAR: target_next(); break;
    case RV_PF_LOCK: g_sys.hold ^= 1; break;
    case RV_PF_ARM_LAMP: case RV_PF_ARM_KNOB: g_sys.armed ^= 1; break;
    case RV_PF_AUTO_LAMP:
        if (arg == 0) rv_autopilot_key();
        else if (arg == 1) g_sys.match ^= 1;
        else if (arg == 2) g_sys.autotarget ^= 1;
        else g_sys.autodecoy ^= 1;
        break;
    case RV_PF_DEFENCE: if (g_sys.decoys) g_sys.decoys--; break;
    case RV_PF_SURV_PAGE: g_sys.surv_page = (unsigned char)arg; break;
    case RV_PF_RADAR_KNOB: case RV_PF_RANGE_LADDER: g_sys.radar_range = (unsigned char)((g_sys.radar_range + 1) % 3); break;
    case RV_PF_RADIO_CHAN: g_sys.chan = (unsigned char)arg; break;
    case RV_PF_CHAN_KNOB: case RV_PF_RADIO_DIGIT: case RV_PF_RADIO_LIT:
        g_sys.chan = (unsigned char)((g_sys.chan + 1) % 3);
        break;
    }
}

/* once a frame, before the cockpit's state: the systems from the slots, the keys held, the comms */
static void systems_tick(void)
{
    const RvShipType __far *t = g_my_type;
    int side = comms_side(), k, s;
    unsigned shield = SHIELD(0, 0) + SHIELD(0, 1);
    /* the radio: tuned to whoever starts talking; the knobs a notch every third frame toward its frequency */
    g_sys.talking = side >= 0;
    if (side >= 0 && side != g_last_side)
        g_sys.chan = side ? CHAN_GUARD : CHAN_WING;
    g_last_side = side;
    if (frameCounter % 3 == 0)
        for (k = 0; k < 3; k++)
            if (g_sys.digit[k] != g_freq[g_sys.chan][k])
                g_sys.digit[k] += g_sys.digit[k] < g_freq[g_sys.chan][k] ? 1 : -1;
    /* the target: gone ones dropped; the nearest enemy at launch, and kept so with AUTO */
    if (g_sys.target >= 0 && !is_ship(g_sys.target))
        g_sys.target = -1;
    if ((frameCounter == 2 && g_sys.target < 0) || (g_sys.autotarget && frameCounter % 30 == 0))
        target_nearest();
    g_sys.target_km = -1;
    g_sys.target_hot = g_sys.lock = 0;
    g_sys.target_name[0] = 0;
    if ((s = g_sys.target) >= 0) {
        const RvShipType __far *tt = slot_type(s);
        RvVec3 d;
        long z, len;
        g_sys.target_km = player_km(s);
        if (tt)
            _fstrncpy(g_sys.target_name, tt->name, sizeof g_sys.target_name - 1);
        d.x = (gPositionVector[s].x - gPositionVector[0].x) / 16;
        d.y = (gPositionVector[s].y - gPositionVector[0].y) / 16;
        d.z = (gPositionVector[s].z - gPositionVector[0].z) / 16;
        z = vec_dot(&d, &gOrientationFrontVector[0]) / 256;
        len = (long)lf_sqrt((unsigned long)((d.x / 16) * (d.x / 16) + (d.y / 16) * (d.y / 16) + (d.z / 16) * (d.z / 16))) * 16;
        /* hot: its engines toward us (we are behind it) or its afterburner lit */
        g_sys.target_hot = vec_dot(&d, &gOrientationFrontVector[s]) > 0 || burning(s);
        /* the lock (until missiles: the target held in the gunsight, 11 degrees, 6,000 km): radar-guided needs the
           radar on (EMCON ACTIVE) and the IFF; infrared a hot target and not EMCON OFF */
        if (len > 0 && z * 100 / len > 98 && g_sys.target_km < 6000)
            g_sys.lock = g_sys.sensor == SENSOR_RADAR ? g_sys.emcon == EMCON_ACTIVE && g_sys.iff
                                                      : g_sys.emcon != EMCON_OFF && g_sys.target_hot;
        if (g_sys.match && t)
            setSpeed[0] = speedLo[s] < t->max_kps ? speedLo[s] : t->max_kps;
    }
    /* the afterburner: Tab held, while there is fuel (a third of a second's worth a frame... 11 s in all) */
    g_sys.burning = in_key_down(0x0F) && g_sys.fuel > 0 && t;
    rvAfterburner[0] = g_sys.burning;
    if (g_sys.burning)
        g_sys.fuel -= 3;
    if (g_sys.fuel < 0)
        g_sys.fuel = 0;
    /* heat follows the engine; the signature is EMCON's, the heat's and the radar's */
    k = t && t->max_kps ? (int)((long)speedLo[0] * 50 / t->max_kps) : 0;
    if (k > 50) k = 50;
    k = (k + (g_sys.burning ? 50 : 0)) * 10;
    g_heat10 += (k - g_heat10) / 20;
    g_sys.heat = g_heat10 / 10;
    g_sys.signature = (g_sys.emcon == EMCON_ACTIVE ? 50 : g_sys.emcon == EMCON_PASSIVE ? 20 : 5) + g_sys.heat / 2;
    if (g_sys.signature > 100) g_sys.signature = 100;
    {   /* (the HEAT and SIGNATURE displays take theirs every 5 s: a sensor's sweep, the user's pick) */
        static unsigned long __far taken;
        if (frameCounter < 3 || clock240() - taken >= 5 * 240) {
            g_sys.heat_shown = g_sys.heat;
            g_sys.sig_shown = g_sys.signature;
            taken = clock240();
        }
    }
    g_sys.hull = t && t->hull ? 100 - (int)((long)damagePoints[0] * 100 / t->hull) : 100;
    if (g_sys.hull < 0) g_sys.hull = 0;
    g_sys.warn = rvCockpitDamage != 0 || (SHIELD_MAX(0, 0) && SHIELD(0, 0) * 4 < SHIELD_MAX(0, 0))
                 || (SHIELD_MAX(0, 1) && SHIELD(0, 1) * 4 < SHIELD_MAX(0, 1));
    /* the running lights: off under EMCON OFF or with the power plant damaged; a hit flickers them */
    if (shield < g_shield_was)
        g_flicker = 8;
    g_shield_was = shield;
    if (g_flicker)
        g_flicker--;
    g_sys.lights = g_sys.emcon != EMCON_OFF && !(rvCockpitDamage >> 9 & 1) && !(g_flicker & 2);
    /* the nav point: how far; the autopilot when no enemy is within 8,000 km */
    g_sys.nav_km = -1;
    if (g_m && g_sys.nav < g_m->nnav) {
        long dx = labs((long)g_m->nav[g_sys.nav].x - gPositionVector[0].x / 256);
        long dz = labs((long)g_m->nav[g_sys.nav].z - gPositionVector[0].z / 256);
        g_sys.nav_km = (dx > dz ? dx : dz) + (dx > dz ? dz : dx) / 2;
    }
    g_sys.autopilot_ready = g_m && g_m->nnav;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (is_ship(s) && commAnimInfo[s] == 1 && player_km(s) < 8000)
            g_sys.autopilot_ready = 0;
}

/* the keys of the systems (a character typed): 1 if it was one */
static int system_key(int ch)
{
    if (ch >= 'A' && ch <= 'Z')
        ch += 32;
    switch (ch) {
    case 'k': system_action(RV_PF_EMCON_KNOB, 0); return 1;
    case 'v': g_sys.sensor ^= 1; return 1;
    case 'i': system_action(RV_PF_IFF_KNOB, 0); return 1;
    case 'c': call_carrier(); return 1;
    case 'u': system_action(RV_PF_VOL_KNOB, 0); return 1;
    case 'n': system_action(RV_PF_NAV_KNOB, 0); return 1;
    case 'm': system_action(RV_PF_ARM_KNOB, 0); return 1;
    case 't': target_next(); return 1;
    case 'y': target_nearest(); return 1;
    case 'l': system_action(RV_PF_LOCK, 0); return 1;
    case 'g': system_action(RV_PF_RADAR_KNOB, 0); return 1;
    case 'h': g_sys.surv_page = (unsigned char)((g_sys.surv_page + 1) % 3); return 1;
    case 'x': system_action(RV_PF_DEFENCE, 0); return 1;
    }
    return 0;
}

/* the cockpit's state from the slots */
static void gather(void)
{
    const RvCockpit __far *c = cockpit_record();
    int s, n = 0, i;
    systems_tick();
    g_st.shield_f = SHIELD_MAX(0, 0) ? (int)(SHIELD(0, 0) * 100L / SHIELD_MAX(0, 0)) : 0;
    g_st.shield_r = SHIELD_MAX(0, 1) ? (int)(SHIELD(0, 1) * 100L / SHIELD_MAX(0, 1)) : 0;
    for (i = 0; i < 4; i++) {
        int max = g_my_type ? (i == 0 ? g_my_type->armor_f : i == 1 ? g_my_type->armor_r : i == 2 ? g_my_type->armor_l
                                                                                                   : g_my_type->armor_rt) : 0;
        g_st.armor[i] = max ? (int)(ARMOR(0, i) * 100L / max) : 0;
    }
    g_st.speed = (int)speedLo[0];
    g_st.set_speed = (int)setSpeed[0];
    g_st.max_speed = g_my_type ? g_my_type->max_kps : 500;
    g_st.fuel = g_sys.fuel / 10;
    g_st.stick_x = g_steer_x > 0 ? 1 : g_steer_x < 0 ? -1 : 0;
    g_st.stick_y = g_steer_y > 0 ? 1 : g_steer_y < 0 ? -1 : 0;
    g_st.damage = rvCockpitDamage;
    g_st.lock = g_sys.lock;
    g_st.warn = g_sys.warn;
    /* the radar: the nearest contacts, where they are relative to the nose */
    for (s = 1; s <= RV_MAX_SHIP_SLOT && n < CP_CONTACTS; s++) {
        RvVec3 d;
        long x, y, z, len, ang;
        int a, rr;
        if (maybeEntityType[s] != 0xC && maybeEntityType[s] != 0xD)
            continue;
        /* (EMCON OFF: a dark scope; beyond the range knob's: not shown) */
        if (g_sys.emcon == EMCON_OFF || player_km(s) > (4000L << g_sys.radar_range))
            continue;
        d.x = (gPositionVector[s].x - gPositionVector[0].x) / 16;
        d.y = (gPositionVector[s].y - gPositionVector[0].y) / 16;
        d.z = (gPositionVector[s].z - gPositionVector[0].z) / 16;
        x = vec_dot(&d, &gOrientationRightVector[0]) / 256;
        y = vec_dot(&d, &gOrientationUpVector[0]) / 256;
        z = vec_dot(&d, &gOrientationFrontVector[0]) / 256;
        while (labs(x) > 16383 || labs(y) > 16383 || labs(z) > 16383) {
            x /= 2;                     /* (the way is all that is wanted: halved till the squares' sum fits a long) */
            y /= 2;
            z /= 2;
        }
        len = (long)lf_sqrt((unsigned long)(x * x + y * y + z * z));
        if (!len)
            continue;
        /* the angle off the nose, 0 the middle .. 512 (behind) the rim */
        ang = lf_atan2((long)lf_sqrt((unsigned long)(x * x + y * y)), z);
        rr = (int)(ang * 256 / 512);
        a = lf_atan2(y, x);
        g_st.contact[n].x = (short)((lf_cos(a) * rr) >> 14);
        g_st.contact[n].y = (short)(-((lf_sin(a) * rr) >> 14));   /* up is up the screen */
        g_st.contact[n].colour = s == g_sys.target ? UI_WHITE   /* (unidentified without IFF or the radar emitting) */
                                 : !g_sys.iff || g_sys.emcon != EMCON_ACTIVE ? UI_GREY5
                                 : commAnimInfo[s] == 0 ? UI_HILITE : commAnimInfo[s] == 1 ? UI_RED : UI_AMBER4;
        n++;
    }
    g_st.ncontacts = n;
    (void)c;
#ifdef RV_TESTS
    if (g_flames_demo) {
        strcpy(g_mfd[0][0], "FLAMES");
        strcpy(g_mfd[0][1], "2 4 6 BURN");
        strcpy(g_mfd[0][2], "3 FRAMES");
    } else if (g_bolt_demo) {           /* the demo's readout: the drawing, the time in its round, where the bolts are */
        static const char __far name[] = "CAPSULES";
        int k = g_demo_frame ? (g_demo_frame - 1) % DEMO_FRAMES : 0;
        _fstrcpy((char __far *)g_mfd[0][0], name);   /* (far: DGROUP is full) */
        sprintf(g_mfd[0][1], "T+%d.%d S", k / 30, k % 30 / 3);
        sprintf(g_mfd[0][2], "%ld KM", BOLT_HALF16 / 16 + (long)k * DEMO_SPEED * BOLT_HALF16 / 256);
    } else if (g_hull_demo) {           /* the demo's readout */
        static const char __far name[] = "HULL";
        _fstrcpy((char __far *)g_mfd[0][0], name);
        sprintf(g_mfd[0][1], "T+%d.%d S", g_demo_frame / 30, g_demo_frame % 30 / 3);
        g_mfd[0][2][0] = 0;
    } else if (g_collide_demo) {        /* the demo's readout: the round, a hit */
        static const char __far name[] = "COLLIDE", hit[] = "HIT", miss[] = "-";
        _fstrcpy((char __far *)g_mfd[0][0], name);
        sprintf(g_mfd[0][1], "ROUND %d", g_demo_frame / CD_FRAMES + 1);
        _fstrcpy((char __far *)g_mfd[0][2], g_cd_hit ? hit : miss);
    } else if (g_sky_demo) {            /* the demo's readout: the time */
        static const char __far name[] = "SKY";
        _fstrcpy((char __far *)g_mfd[0][0], name);
        sprintf(g_mfd[0][1], "T+%d.%d S", g_demo_frame / 30, g_demo_frame % 30 / 3);
        g_mfd[0][2][0] = 0;
    } else if (g_missile_demo) {        /* the demo's readout: the time in its round */
        static const char __far name[] = "MISSILES";
        int k = g_demo_frame ? (g_demo_frame - 1) % DEMO_FRAMES : 0;
        _fstrcpy((char __far *)g_mfd[0][0], name);
        sprintf(g_mfd[0][1], "T+%d.%d S", k / 30, k % 30 / 3);
        g_mfd[0][2][0] = 0;
    } else if (g_ball_demo) {           /* the demo's readout: the time in its round */
        static const char __far name[] = "BALLS";
        int k = g_demo_frame ? (g_demo_frame - 1) % DEMO_FRAMES : 0;
        _fstrcpy((char __far *)g_mfd[0][0], name);
        sprintf(g_mfd[0][1], "T+%d.%d S", k / 30, k % 30 / 3);
        g_mfd[0][2][0] = 0;
    } else if (g_balls) {               /* the balls' benchmark's */
        sprintf(g_mfd[0][0], "BALLS %d+%d", g_near_balls, g_bl_far);
        sprintf(g_mfd[0][1], "%d.%d FPS", g_fps10 / 10, g_fps10 % 10);
        sprintf(g_mfd[0][2], "BOLTS %d+%d", g_near_bolts, lasers_far());
    } else if (g_lasers) {              /* the laser benchmark's readout */
        sprintf(g_mfd[0][0], "BOLTS %d+%d", g_near_bolts, lasers_far());
        sprintf(g_mfd[0][1], "%d.%d FPS", g_fps10 / 10, g_fps10 % 10);
        sprintf(g_mfd[0][2], "SHIPS %d+%d", g_big_ships, g_far_ships);
    } else if (g_nbench) {              /* the benchmark's readout; else the controls (the user asked how to roll) */
        sprintf(g_mfd[0][0], "SPRITES %d", g_nbench);
        sprintf(g_mfd[0][1], "%d.%d FPS", g_fps10 / 10, g_fps10 % 10);
        sprintf(g_mfd[0][2], "%dx%d VIEW", g_bench_w, g_bench_h);
    } else
#endif
    {
        strcpy(g_mfd[0][0], "ARROWS TURN");
        strcpy(g_mfd[0][1], "Q E ROLL");
        strcpy(g_mfd[0][2], "+ - F S SPD");
    }
#ifdef RV_TESTS
    if (g_bolt_demo) {
        int lines, fats;
        bolts_on_screen(&lines, &fats);
        sprintf(g_mfd[0][3], "%d FAT %d LN", fats, lines);
    } else
#endif
    if (g_flight_timed)
        sprintf(g_mfd[0][3], "T-%lu", (FLIGHT_TICKS - (clock240() - g_t0) + 239) / 240);
    else {
        static const char __far s_home[] = "C: CALL HOME", s_calling[] = "CALLING HOME";
        _fstrcpy((char __far *)g_mfd[0][3], g_landing ? s_calling : s_home);
    }
    g_st.mfd_text[0][0] = comms_on(0) ? 0 : g_mfd[0][0];   /* (a comm takes the whole VDU) */
    g_st.mfd_text[0][1] = g_mfd[0][1];
    g_st.mfd_text[0][2] = g_mfd[0][2];
    g_st.mfd_text[0][3] = g_mfd[0][3];
    g_st.mfd_text[1][0] = 0;
    g_st.mfd_draw = mfd_draw;
}

#ifdef RV_TESTS
/* --- the benchmark --- */

static void bench_add(int n)
{
    const RvCockpit __far *c = cockpit_record();
    int y0 = c ? c->open_y0 : 0, h = c ? c->open_y1 - c->open_y0 : SCR_H;
    while (n-- > 0 && g_nbench < BENCH_MAX) {
        g_bench[g_nbench].x = (short)(vrnd() % SCR_W);
        g_bench[g_nbench].y = (short)(y0 + vrnd() % h);
        g_bench[g_nbench].dx = (short)((int)(vrnd() % 5) - 2);
        g_bench[g_nbench].dy = (short)((int)(vrnd() % 5) - 2);
        g_nbench++;
    }
}

static void bench_step(void)
{
    const RvCockpit __far *c = cockpit_record();
    int i, y0 = c ? c->open_y0 : 0, y1 = c ? c->open_y1 : SCR_H;
    for (i = 0; i < g_nbench; i++) {
        int x = g_bench[i].x + g_bench[i].dx, y = g_bench[i].y + g_bench[i].dy;
        if (x < 0 || x >= SCR_W) g_bench[i].dx = -g_bench[i].dx, x = g_bench[i].x;
        if (y < y0 || y >= y1) g_bench[i].dy = -g_bench[i].dy, y = g_bench[i].y;
        g_bench[i].x = (short)x;
        g_bench[i].y = (short)y;
    }
    /* the count follows the frame rate: more while it holds, fewer when it falls */
    if (g_bench_wanted >= 0) {
        if (g_nbench < g_bench_wanted)
            bench_add(g_bench_wanted - g_nbench);
    } else if (g_fps10 > FPS_WANT * 10 + 10) {
        bench_add(g_nbench < 50 ? 4 : 2);
    } else if (g_fps10 < FPS_WANT * 10 - 10 && g_nbench > 0) {
        g_nbench -= g_nbench < 50 ? 2 : 1;
    }
    rvBenchCount = (unsigned short)g_nbench;
    if (clock240() - g_t0 > FLIGHT_TICKS / 2) {
        g_bench_sum += g_nbench;
        g_bench_frames++;
    }
}

int flight_bench_result(int *w, int *h)
{
    *w = g_bench_w;
    *h = g_bench_h;
    return g_bench_frames ? (int)(g_bench_sum / g_bench_frames) : 0;
}

/* --- the laser benchmark (RV.EXE /lasers N) ---
 *
 * The user's question: how many laser bolts a frame holds at 30 fps beside
 * two big ships near (drawn exactly: turned and scaled each frame), many
 * small ones far (copies from the sets), and a dozen bolts near enough to be
 * drawn fat.  The far bolts are fired from the far ships, every way, and
 * live a second or two; the near ones are kept 220 .. 900 km ahead.  With N
 * -1 their count follows the frame rate as the sprite benchmark's does; the
 * frame's parts are timed, for how the four (big ships, far ships, near and
 * far bolts) share the frame. */
#define LZ_SHIPS 48
#define LZ_DIRS 64
#define LZ_SPEED (75L * 256 / BOLT_HALF16)   /* positions per sixteenth of a km of half axis: 75 km a frame, 1,500 kps */
static struct { LfVec pos; LfBasis b; unsigned char set; } __far g_lz_ship[LZ_SHIPS];
static struct { short x, y, z; } __far g_lz_dir[LZ_DIRS];   /* bolts' half axes, every way, BOLT_HALF16 long */
static unsigned char __far *g_lz_life;  /* frames each bolt has left */
static int g_lz_far, g_lz_cap, g_lz_n, g_lz_frames;
static long g_lz_sum;
static unsigned long g_t_far_ships, g_t_lz_step;

static void lz_aim(LzBolt __far *b)
{
    int k = vrnd() % LZ_DIRS;
    b->hx = g_lz_dir[k].x;
    b->hy = g_lz_dir[k].y;
    b->hz = g_lz_dir[k].z;
}

/* a near one: 220 .. 900 km ahead of the camera, within the view, any way */
static void lz_spawn_near(int i)
{
    LzBolt __far *b = bolts_pool() + i;
    long d = 220 + vrnd() % 680, across = (long)((int)(vrnd() % 2001) - 1000) * d * 3 / 10000,
         up = (long)((int)(vrnd() % 2001) - 1000) * d * 18 / 100000;
    const RvVec3 *f = &gOrientationFrontVector[0], *r = &gOrientationRightVector[0], *u = &gOrientationUpVector[0];
    b->x = gPositionVector[0].x + f->x * d + r->x * across + u->x * up;
    b->y = gPositionVector[0].y + f->y * d + r->y * across + u->y * up;
    b->z = gPositionVector[0].z + f->z * d + r->z * across + u->z * up;
    lz_aim(b);
    g_lz_life[i] = (unsigned char)(6 + vrnd() % 10);
}

/* a far one: from a far ship, any way (with no far ships, from where one would be) */
static void lz_spawn_far(int i)
{
    LzBolt __far *b = bolts_pool() + i;
    int k = (int)(vrnd() % (g_far_ships ? g_far_ships : LZ_SHIPS));
    b->x = g_lz_ship[k].pos.x + 256L * ((int)(vrnd() % 1201) - 600);
    b->y = g_lz_ship[k].pos.y + 256L * ((int)(vrnd() % 601) - 300);
    b->z = g_lz_ship[k].pos.z + 256L * ((int)(vrnd() % 1201) - 600);
    lz_aim(b);
    g_lz_life[i] = (unsigned char)(20 + vrnd() % 40);
}

static int lasers_far(void) { return g_lz_far; }

/* --- the missile demo (RV.EXE /missiledemo): missiles seen every way ---
 *
 * The user's (2026-10-21): missiles drawn with the bolts' code, four in
 * flight at a time expected, six at most.  Six here, each on a round of its
 * own, placed from the frame's number (the player stands still): two fired
 * from under the wings, seen from behind as they speed away; one crossing
 * 700 km ahead, side on; one 2,500 km out the other way, turned a little
 * toward the camera; one coming at the camera nose first from 6,000 km,
 * passing beside it; one far (9,000 km) climbing across, a line.  Ten
 * seconds (DEMO_FRAMES), then the flight lands. */
static const struct {
    short a, b, c;                      /* where it starts: km ahead, right, up of the player */
    signed char fx, fr, fu;             /* its way in the player's frame (64 = 1.0) */
    unsigned char speed, accel, period, delay;   /* km a frame, + accel/16 km a frame each frame; its round's frames;
                                           its first start */
} __far g_md[MISSILE_MAX] = {
    { 0, -40, -30, 64, 0, 0, 4, 8, 150, 0 },
    { 0, 40, -30, 64, 0, 0, 4, 8, 150, 20 },
    { 700, -650, 60, 0, 64, 0, 12, 0, 110, 0 },
    { 2500, 1400, -150, -16, -62, 0, 30, 0, 100, 30 },
    { 6000, 260, 90, -64, 0, 0, 32, 0, 190, 0 },
    { 9000, -3800, -1200, 0, 45, 45, 60, 0, 140, 10 },
};

static void missile_demo_step(void)
{
    const RvVec3 *f = &gOrientationFrontVector[0], *r = &gOrientationRightVector[0], *u = &gOrientationUpVector[0];
    Missile __far *m = missiles_pool();
    int i;
    for (i = 0; i < MISSILE_MAX; i++) {
        long t = (long)g_demo_frame - g_md[i].delay, a, b, c, d;
        if (t < 0) {
            m[i].live = 0;
            continue;
        }
        t %= g_md[i].period;
        d = g_md[i].speed * t + (long)g_md[i].accel * t * t / 32;   /* km flown */
        a = g_md[i].a + d * g_md[i].fx / 64;
        b = g_md[i].b + d * g_md[i].fr / 64;
        c = g_md[i].c + d * g_md[i].fu / 64;
        m[i].x = gPositionVector[0].x + f->x * a + r->x * b + u->x * c;
        m[i].y = gPositionVector[0].y + f->y * a + r->y * b + u->y * c;
        m[i].z = gPositionVector[0].z + f->z * a + r->z * b + u->z * c;
        m[i].dx = (short)((f->x * g_md[i].fx + r->x * g_md[i].fr + u->x * g_md[i].fu) / 64);
        m[i].dy = (short)((f->y * g_md[i].fx + r->y * g_md[i].fr + u->y * g_md[i].fu) / 64);
        m[i].dz = (short)((f->z * g_md[i].fx + r->z * g_md[i].fr + u->z * g_md[i].fu) / 64);
        m[i].live = 1;
        m[i].burn = 1;
    }
    if (++g_demo_frame >= DEMO_FRAMES)
        missionStatus = 1;              /* landed */
}

/* --- the sky demo (RV.EXE /skydemo): the far things as the camera rolls ---
 *
 * The user's (2026-10-21): nebulae, galaxies and planets at infinity, one
 * size each, turning as the camera rolls.  The four SKY.DAT has, round the
 * nose, and the camera standing, rolling a whole turn every twelve seconds
 * and yawing and pitching gently, so each comes round at every turn and
 * passes across the view.  Fifteen seconds, then the flight lands. */
static void sky_demo_setup(void)
{
    static const signed char __far at[4][3] = {   /* the thing; its yaw and pitch (degrees) */
        { SKY_NEBULA, -20, 5 }, { SKY_GALAXY, 23, 9 }, { SKY_GIANT, 7, -5 }, { SKY_RINGED, -4, 7 }
    };
    int i;
    for (i = 0; i < 4; i++) {
        long yaw = at[i][1] * 1024L / 360, pitch = at[i][2] * 1024L / 360;
        LfVec dir, up;
        dir.x = (long)lf_sin((int)yaw) * lf_cos((int)pitch) >> 16;   /* (4096 = 1.0) */
        dir.y = (long)lf_sin((int)pitch) >> 2;
        dir.z = (long)lf_cos((int)yaw) * lf_cos((int)pitch) >> 16;
        up.x = at[i][0] == SKY_GALAXY ? 1700 : 0;   /* (the galaxy's up leans) */
        up.y = 4096;
        up.z = 0;
        sky_add(at[i][0], &dir, &up);
    }
}

static void sky_demo_step(void)
{
    int t = g_demo_frame;
    int yaw = (int)((long)lf_sin((int)(t * 1024L / 450)) * 40 >> 14), pitch = (int)((long)lf_sin((int)(t * 1024L / 225)) * 14 >> 14);
    ship_face(0, yaw, pitch);
    vec_turn(&gOrientationRightVector[0], &gOrientationUpVector[0], (int)(t * 1024L / 360 % 1024));
    ship_orthonormalise(0);
    if (++g_demo_frame >= SKY_FRAMES)
        missionStatus = 1;              /* landed */
}

#endif /* RV_TESTS */

static void missiles_frame(void)
{
    LfBasis cam;
    const RvCockpit __far *c = cockpit_record();
    if (!g_missile_demo) {              /* the missiles are slots (EntityType ET_MISSILE): the first MISSILE_MAX drawn */
        Missile __far *p = missiles_pool();
        int s, k = 0;
        for (s = 1; s <= RV_MAX_SHIP_SLOT && k < MISSILE_MAX; s++)
            if (maybeEntityType[s] == ET_MISSILE) {
                p[k].x = gPositionVector[s].x;
                p[k].y = gPositionVector[s].y;
                p[k].z = gPositionVector[s].z;
                p[k].dx = (short)gOrientationFrontVector[s].x;
                p[k].dy = (short)gOrientationFrontVector[s].y;
                p[k].dz = (short)gOrientationFrontVector[s].z;
                p[k].live = 1;
                p[k].burn = 1;
                k++;
            }
        for (; k < MISSILE_MAX; k++)
            p[k].live = 0;
    }
    basis_of(0, &cam);
    missiles_place(gPositionVector[0].x, gPositionVector[0].y, gPositionVector[0].z, &cam, SCR_W / 2, g_cam_cy, 400,
                   c ? c->open_y0 : 0, c ? c->open_y1 : SCR_H);
}

#ifdef RV_TESTS
/* --- the balls in the laser benchmark (RV.EXE /balls N) ---
 *
 * The user's (2026-10-21): spherical projectiles, far more common than the
 * bolts, so the question is again how many a frame holds at 30 fps.  N far
 * ones fired from the far ships every way, living one to three seconds, and
 * g_near_balls kept 150 .. 900 km ahead; the four kinds at random; N -1
 * adapts to the frame rate.  They fly 60 km a frame (1,800 kps). */
#define BALL_SPEED_KM 60

static void ball_spawn(int i)
{
    LzBall __far *b = balls_pool() + i;
    int k;
    if (i < g_near_balls) {             /* near: 150 .. 900 km ahead, within the view */
        long d = 150 + vrnd() % 750, across = (long)((int)(vrnd() % 2001) - 1000) * d * 3 / 10000,
             up = (long)((int)(vrnd() % 2001) - 1000) * d * 18 / 100000;
        const RvVec3 *f = &gOrientationFrontVector[0], *r = &gOrientationRightVector[0], *u = &gOrientationUpVector[0];
        b->x = gPositionVector[0].x + f->x * d + r->x * across + u->x * up;
        b->y = gPositionVector[0].y + f->y * d + r->y * across + u->y * up;
        b->z = gPositionVector[0].z + f->z * d + r->z * across + u->z * up;
        g_bl_life[i] = (unsigned char)(6 + vrnd() % 10);
    } else {                            /* far: from a far ship (or where one would be) */
        k = (int)(vrnd() % (g_far_ships ? g_far_ships : LZ_SHIPS));
        b->x = g_lz_ship[k].pos.x + 256L * ((int)(vrnd() % 1201) - 600);
        b->y = g_lz_ship[k].pos.y + 256L * ((int)(vrnd() % 601) - 300);
        b->z = g_lz_ship[k].pos.z + 256L * ((int)(vrnd() % 1201) - 600);
        g_bl_life[i] = (unsigned char)(30 + vrnd() % 60);
    }
    k = vrnd() % LZ_DIRS;               /* any way */
    b->vx = (short)((long)g_lz_dir[k].x * (BALL_SPEED_KM * 256L) / BOLT_HALF16);
    b->vy = (short)((long)g_lz_dir[k].y * (BALL_SPEED_KM * 256L) / BOLT_HALF16);
    b->vz = (short)((long)g_lz_dir[k].z * (BALL_SPEED_KM * 256L) / BOLT_HALF16);
    b->kind = (unsigned char)(vrnd() % BALL_KINDS);
    b->phase = (unsigned char)(vrnd() % 4);
}

/* --- the ball demo (RV.EXE /balldemo): the four kinds flying at the eye ---
 *
 * Ten of each kind in a column off the view's middle (one kind in each
 * corner), 300 km apart from 100 km out to 3,000, flying at the eye 10 km a
 * frame and starting again at the back: every size at once, from a pixel to
 * the biggest, animating.  Ten seconds (DEMO_FRAMES), then the flight lands. */
#define BDEMO_DEPTHS 10
static void ball_demo_step(void)
{
    static const signed char __far at[BALL_KINDS][2] = { { -24, 10 }, { 24, 10 }, { -24, -10 }, { 24, -10 } };
    const RvVec3 *f = &gOrientationFrontVector[0], *r = &gOrientationRightVector[0], *u = &gOrientationUpVector[0];
    int k, j, i = 0;
    if (g_bl_cap < BALL_KINDS * BDEMO_DEPTHS)
        return;
    for (k = 0; k < BALL_KINDS; k++)
        for (j = 0; j < BDEMO_DEPTHS; j++, i++) {
            LzBall __far *b = balls_pool() + i;
            long d = 100 + ((j * 300L - (long)g_demo_frame * 10) % 3000 + 3000) % 3000, across = at[k][0], up = at[k][1];
            b->x = gPositionVector[0].x + f->x * d + r->x * across + u->x * up;
            b->y = gPositionVector[0].y + f->y * d + r->y * across + u->y * up;
            b->z = gPositionVector[0].z + f->z * d + r->z * across + u->z * up;
            b->vx = b->vy = b->vz = 0;
            b->kind = (unsigned char)k;
            b->phase = (unsigned char)j;
        }
    balls_set_count(i);
    if (++g_demo_frame >= DEMO_FRAMES)
        missionStatus = 1;              /* landed */
}

/* once a frame: the count, the flight, the expired ones fired again */
static void balls_step(void)
{
    int i, n;
    unsigned long t0 = clock240();
    if (!g_bl_cap)
        return;
    if (g_ball_demo) {
        ball_demo_step();
        return;
    }
    if (g_balls < 0) {
        if (g_fps10 > FPS_WANT * 10 + 10)
            g_bl_far += g_bl_far < 800 ? 32 : 16;
        else if (g_fps10 < FPS_WANT * 10 - 10)
            g_bl_far -= g_bl_far < 800 ? 16 : 8;
    } else {
        g_bl_far = g_balls;
    }
    if (g_bl_far > g_bl_cap - g_near_balls)
        g_bl_far = g_bl_cap - g_near_balls;
    if (g_bl_far < 0)
        g_bl_far = 0;
    n = g_near_balls + g_bl_far;
    for (i = g_bl_n; i < n; i++)
        ball_spawn(i);
    g_bl_n = n;
    balls_set_count(n);
    balls_move();
    for (i = 0; i < n; i++)
        if (!--g_bl_life[i])
            ball_spawn(i);
    if (clock240() - g_t0 > FLIGHT_TICKS / 2) {
        g_bl_sum += g_bl_far;
        g_bl_frames++;
    }
    g_t_bl_step += clock240() - t0;
}

/* --- the bolt demo (RV.EXE /boltdemo): the player's own shots ---
 *
 * The user's question (2026-10-21): most near bolts a player sees are their
 * own, so what do the shapes look like on bolts fired from round the eye?
 * Eleven bolts, all flying straight ahead from the eye's depth: one at the
 * eye, one just right of it (a 10 px gap between the two where they appear),
 * one half a metre left, four 1.5 m left and right and 1 m up and down, and
 * four at the corners of those.  A metre is taken as 32 km of the flight's
 * units: a fighter's view (128 px) is full size at 2,500 km, 800 km for a
 * 25-30 m hull.  They fly at a constant speed that takes them from the eye to
 * the line drawing (3/4 px of radius, 1,067 km) in about five seconds; the round
 * is ten seconds (drawn as this build draws fat bolts), then the flight
 * lands. */
#define DEMO_GAP_KM ((2 * BOLT_RMAX16 / 16 + 10) * BOLT_ZMIN_KM / 400)   /* centres 18 px apart at the near plane */
static const signed char __far g_demo_at[DEMO_BOLTS][2] = {   /* across, up: km */
    { 0, 0 }, { DEMO_GAP_KM, 0 }, { -DEMO_METRE / 2, 0 },
    { -DEMO_METRE * 3 / 2, 0 }, { DEMO_METRE * 3 / 2, 0 }, { 0, DEMO_METRE }, { 0, -DEMO_METRE },
    { -DEMO_METRE * 3 / 2, DEMO_METRE }, { DEMO_METRE * 3 / 2, DEMO_METRE },
    { -DEMO_METRE * 3 / 2, -DEMO_METRE }, { DEMO_METRE * 3 / 2, -DEMO_METRE }
};
static void demo_step(void)
{
    int i, k = g_demo_frame % DEMO_FRAMES;
    if (g_lz_cap < DEMO_BOLTS)
        return;
    if (!k) {                           /* the round: the bolts at the eye */
        const RvVec3 *f = &gOrientationFrontVector[0], *r = &gOrientationRightVector[0], *u = &gOrientationUpVector[0];
        long d = BOLT_HALF16 / 16;      /* (their tails at the eye's depth) */
        for (i = 0; i < DEMO_BOLTS; i++) {
            LzBolt __far *b = bolts_pool() + i;
            long across = g_demo_at[i][0], up = g_demo_at[i][1];
            b->x = gPositionVector[0].x + f->x * d + r->x * across + u->x * up;
            b->y = gPositionVector[0].y + f->y * d + r->y * across + u->y * up;
            b->z = gPositionVector[0].z + f->z * d + r->z * across + u->z * up;
            b->hx = (short)(f->x * BOLT_HALF16 / 256);
            b->hy = (short)(f->y * BOLT_HALF16 / 256);
            b->hz = (short)(f->z * BOLT_HALF16 / 256);
        }
        bolts_set_count(DEMO_BOLTS);
    } else {
        bolts_move(DEMO_SPEED);
    }
    if (++g_demo_frame >= DEMO_FRAMES)
        missionStatus = 1;              /* landed */
}

static void lasers_setup(int enemy_type)
{
    int k;
    /* the two big ships, 1,500 and 2,100 km ahead (drawn exactly), and the player still */
    memset(maybeEntityType + 1, 0, (RV_SLOTS - 1) * 2);
    ship_spawn(1, enemy_type, 1, 4, -256L * 380, 256L * 60, 256L * 1500, 300);
    ship_spawn(2, g_camp.player_ship, 0, 4, 256L * 520, -256L * 90, 256L * 2100, 700);
    for (k = g_big_ships + 1; k <= 2; k++)
        maybeEntityType[k] = 0;         /* (/bigships: fewer; their sets stay loaded for the far ones) */
    for (k = 0; k < 3; k++)
        setSpeed[k] = speedLo[k] = 0;
    /* the far ships: 5,000 .. 15,000 km, within 35 degrees of the nose across and 15 up and down */
    if (g_far_ships > LZ_SHIPS)
        g_far_ships = LZ_SHIPS;
    for (k = 0; k < LZ_SHIPS; k++) {    /* (all of them placed: the far bolts start at them even when none is drawn) */
        long d = 5000 + vrnd() % 10000;
        int a = (int)(vrnd() % 200) - 100, e = (int)(vrnd() % 86) - 43;   /* 1024 a turn */
        g_lz_ship[k].pos.x = (lf_sin(a) * d >> 14) * 256;
        g_lz_ship[k].pos.y = (lf_sin(e) * d >> 14) * 256;
        g_lz_ship[k].pos.z = (lf_cos(a) * d >> 14) * 256;
        ship_face(RV_SCRATCH_SLOT, (int)(vrnd() % 1024), (int)(vrnd() % 300) - 150);
        g_lz_ship[k].b.front = *(const LfVec *)&gOrientationFrontVector[RV_SCRATCH_SLOT];
        g_lz_ship[k].b.right = *(const LfVec *)&gOrientationRightVector[RV_SCRATCH_SLOT];
        g_lz_ship[k].b.up = *(const LfVec *)&gOrientationUpVector[RV_SCRATCH_SLOT];
    }
    for (k = 0; k < LZ_DIRS; k++) {
        long x, y, z, n;
        unsigned long len;
        do {
            x = (long)(vrnd() % 2001) - 1000;
            y = (long)(vrnd() % 2001) - 1000;
            z = (long)(vrnd() % 2001) - 1000;
            n = x * x + y * y + z * z;
        } while (n < 40000L || n > 1000000L);
        len = lf_sqrt((unsigned long)n);
        g_lz_dir[k].x = (short)(x * BOLT_HALF16 / (long)len);
        g_lz_dir[k].y = (short)(y * BOLT_HALF16 / (long)len);
        g_lz_dir[k].z = (short)(z * BOLT_HALF16 / (long)len);
    }
    lz_init();
    g_demo_frame = 0;
    g_lz_far = g_lasers > 0 ? g_lasers : 64;
    g_bl_far = g_balls > 0 ? g_balls : 128;
    g_bl_n = 0;
    g_bl_sum = 0;
    g_bl_frames = 0;
    g_t_bl_step = 0;
    g_lz_n = 0;
    g_lz_sum = 0;
    g_lz_frames = 0;
    g_t_exact = g_t_sets = g_t_far_ships = g_t_draw = g_t_lz_step = g_t_wait = g_world_calls = 0;
}

/* the bolts' pool: what memory is left once the rest of the flight has taken its share (the exact draw's
 * dense buffers first), after the bolts' checks and timings */
static void lasers_pool(void)
{
    long t[6];
    unsigned long before = bolts_dos_free();
    if (!ships_dense_reserve())
        rv_log("lasers: no room for the exact draw's buffers");
    rv_log("lasers: DOS memory free: %lu KB before the exact draw's buffers, %lu after", before / 1024,
           bolts_dos_free() / 1024);
    bolts_selftest();
    bolts_time(t);
    {
        /* what placing a far ship costs (lf_place, the C every ship's draw starts with, once a band) */
        LfBasis cam, ship = g_lz_ship[0].b;
        LfPlace p;
        LfVec rel;
        unsigned long t0 = clock240(), dt;
        long it = 0;
        basis_of(0, &cam);
        rel.x = 256L * 1500; rel.y = 256L * 400; rel.z = 256L * 8000;
        do {
            lf_place(&rel, &cam, &ship, SCR_W / 2, g_cam_cy, 400, 5, 7, &p);
            it++;
        } while ((dt = clock240() - t0) < 120);
        rv_log("lasers: lf_place takes %lu us", dt * 1000000UL / 240 / (unsigned long)it);
    }
    if (g_balls) {
        balls_selftest();
        balls_time();
        balls_free();
    }
    _fheapshrink();                     /* (what the checks took back to DOS) */
    /* 12 KB kept for what the flight takes later; with balls, the bolts fixed in number or 800 */
    g_lz_cap = bolts_init(g_balls ? (g_lasers > 0 ? g_lasers + g_near_bolts + 16 : 800) : 3200, 12);
    g_lz_life = g_lz_cap ? (unsigned char __far *)_fmalloc(g_lz_cap) : 0;
    if (!g_lz_life)
        g_lz_cap = 0;
    if (g_near_bolts > g_lz_cap)
        g_near_bolts = g_lz_cap;
    if (g_balls) {                      /* the balls: what is left but 14 KB (their lives' 2 from it) */
        static const char __far s_room[] = "balls: room for %d; %d near, %s far";
        g_bl_cap = balls_init(2000, 14);
        g_bl_life = g_bl_cap ? (unsigned char __far *)_fmalloc(g_bl_cap) : 0;
        if (!g_bl_life) {
            g_bl_cap = 0;
            balls_free();
        }
        if (g_near_balls > g_bl_cap)
            g_near_balls = g_bl_cap;
        rv_logf(s_room, g_bl_cap, g_near_balls, g_balls < 0 ? "adapting" : "fixed");
    }
    rv_log("lasers: room for %d bolts; %d near, %s far; 2 big ships near, %d far", g_lz_cap, g_near_bolts,
           g_lasers < 0 ? "adapting" : "fixed", g_far_ships);
}

/* the far ships' sets, once the types are loaded */
static void lasers_sets(void)
{
    int k;
    for (k = 0; k < g_far_ships; k++)
        g_lz_ship[k].set = (unsigned char)((k & 1) && g_set_wing >= 0 ? g_set_wing : g_set_enemy);
}

/* once a frame: the count, the flight, the expired ones fired again */
static void lasers_step(void)
{
    int i, n;
    unsigned long t0 = clock240();
    if (!g_lz_cap)
        return;
    if (g_lasers < 0) {
        if (g_fps10 > FPS_WANT * 10 + 10)
            g_lz_far += g_lz_far < 400 ? 16 : 8;
        else if (g_fps10 < FPS_WANT * 10 - 10)
            g_lz_far -= g_lz_far < 400 ? 8 : 4;
    } else {
        g_lz_far = g_lasers;
    }
    if (g_lz_far > g_lz_cap - g_near_bolts)
        g_lz_far = g_lz_cap - g_near_bolts;
    if (g_lz_far < 0)
        g_lz_far = 0;
    n = g_near_bolts + g_lz_far;
    for (i = g_lz_n; i < n; i++)
        if (i < g_near_bolts)
            lz_spawn_near(i);
        else
            lz_spawn_far(i);
    g_lz_n = n;
    bolts_set_count(n);
    bolts_move(LZ_SPEED);
    for (i = 0; i < n; i++)
        if (!--g_lz_life[i]) {
            if (i < g_near_bolts)
                lz_spawn_near(i);
            else
                lz_spawn_far(i);
        }
    if (clock240() - g_t0 > FLIGHT_TICKS / 2) {
        g_lz_sum += g_lz_far;
        g_lz_frames++;
    }
    g_t_lz_step += clock240() - t0;
}

static void lasers_place(void)
{
    LfBasis cam;
    const RvCockpit __far *c = cockpit_record();
    basis_of(0, &cam);
    bolts_place(gPositionVector[0].x, gPositionVector[0].y, gPositionVector[0].z, &cam, SCR_W / 2, g_cam_cy, 400,
                c ? c->open_y0 : 0, c ? c->open_y1 : SCR_H, BOLT_LINE16);
    balls_place(gPositionVector[0].x, gPositionVector[0].y, gPositionVector[0].z, &cam, SCR_W / 2, g_cam_cy, 400,
                c ? c->open_y0 : 0, c ? c->open_y1 : SCR_H, frameCounter / 2);   /* (15 animation frames a second) */
}

static void lasers_draw_far(int y0, int y1, const short __far *ext, int ey0, int rows)
{
    LfBasis cam;
    int k;
    unsigned long t0 = clock240();
    basis_of(0, &cam);
    for (k = 0; k < g_far_ships; k++) {
        LfVec rel;
        LfBasis ship = g_lz_ship[k].b;  /* (near: ships_draw takes the bases near) */
        rel.x = g_lz_ship[k].pos.x - gPositionVector[0].x;
        rel.y = g_lz_ship[k].pos.y - gPositionVector[0].y;
        rel.z = g_lz_ship[k].pos.z - gPositionVector[0].z;
        ships_draw(g_lz_ship[k].set, &rel, &cam, &ship, scale_of(&rel), y0, y1, 0, SCR_W, ext, ey0, rows);
    }
    g_t_far_ships += clock240() - t0;
    bolts_draw_far(y0, y1, ext, ey0, rows);
}

#endif /* RV_TESTS */

/* ms a frame x 100 from 240 Hz ticks over `frames` */
static unsigned long per_frame(unsigned long ticks, unsigned frames)
{
    return ticks * 100000UL / 240 / (frames ? frames : 1);
}

#ifdef RV_TESTS
static void lasers_report(void)
{
    static const char __far s_held[] = "lasers: %ld far bolts and %d near held %d fps (the settled half's average; "
        "%d lines and %d fat on screen at the end), beside %d big ships drawn exactly and %d far ones",
        __far s_ms[] = "lasers: ms a frame: bolts placed %lu.%02lu, far bolts drawn %lu.%02lu, near %lu.%02lu, flown and fired "
        "%lu.%02lu; big ships %lu.%02lu, far ships %lu.%02lu (the slots' from the sets %lu.%02lu); stars %lu.%02lu; "
        "the whole picture %lu.%02lu",
        __far s_rest[] = "lasers: the cockpit and the flip %lu.%02lu ms a frame; the rest of the frame (simulation, plan, "
        "stars and balls placed) %lu.%02lu; frames %lu.%02lu ms on average; the world drawn in %lu.%02lu bands a frame";
    unsigned long t[4], f = frameCounter;
    int lines, fats;
    if (!g_lz_cap)
        return;
    bolts_ticks(t);
    bolts_on_screen(&lines, &fats);
    rv_logf(s_held, g_lz_frames ? g_lz_sum / g_lz_frames : 0L, g_near_bolts, FPS_WANT, lines, fats, g_big_ships, g_far_ships);
    rv_logf(s_ms, per_frame(t[0], f) / 100, per_frame(t[0], f) % 100, per_frame(t[1], f) / 100, per_frame(t[1], f) % 100,
           per_frame(t[2], f) / 100, per_frame(t[2], f) % 100, per_frame(g_t_lz_step, f) / 100,
           per_frame(g_t_lz_step, f) % 100, per_frame(g_t_exact, f) / 100, per_frame(g_t_exact, f) % 100,
           per_frame(g_t_far_ships, f) / 100, per_frame(g_t_far_ships, f) % 100, per_frame(g_t_sets, f) / 100,
           per_frame(g_t_sets, f) % 100, per_frame(g_star_place_ticks + g_star_draw_ticks, f) / 100,
           per_frame(g_star_place_ticks + g_star_draw_ticks, f) % 100, per_frame(g_t_draw, f) / 100,
           per_frame(g_t_draw, f) % 100);
    {
        /* the rest: the picture less its world (the cockpit, the instruments, the flip) and the frame less the
         * picture and the bolts' own work (the simulation, the plan, the stars' placing) */
        unsigned long world = t[1] + t[2] + g_t_exact + g_t_sets + g_t_far_ships + g_star_draw_ticks;
        unsigned long frame = (clock240() - g_t0) - g_t_wait;
        rv_logf(s_rest, per_frame(g_t_draw - world, f) / 100, per_frame(g_t_draw - world, f) % 100,
               per_frame(frame - g_t_draw - t[0] - g_t_lz_step, f) / 100,
               per_frame(frame - g_t_draw - t[0] - g_t_lz_step, f) % 100, per_frame(frame, f) / 100,
               per_frame(frame, f) % 100, g_world_calls / (f ? f : 1), g_world_calls * 100 / (f ? f : 1) % 100);
    }
    if (g_lz_life)
        _ffree(g_lz_life);
    g_lz_life = 0;
    g_lz_cap = 0;
    bolts_free();
}

static void balls_report(void)
{
    static const char __far s_held[] = "balls: %ld far balls and %d near held %d fps (the settled half's average; %d "
        "small and %d big on screen at the end)",
        __far s_ms[] = "balls: ms a frame: placed %lu.%02lu, the small ones drawn %lu.%02lu, the big %lu.%02lu, flown and "
        "fired %lu.%02lu";
    unsigned long t[3], f = frameCounter;
    int small, big;
    if (g_bl_cap) {
        balls_ticks(t);
        balls_on_screen(&small, &big);
        rv_logf(s_held, g_bl_frames ? g_bl_sum / g_bl_frames : 0L, g_near_balls, FPS_WANT, small, big);
        rv_logf(s_ms, per_frame(t[0], f) / 100, per_frame(t[0], f) % 100, per_frame(t[1], f) / 100,
                per_frame(t[1], f) % 100, per_frame(t[2], f) / 100, per_frame(t[2], f) % 100,
                per_frame(g_t_bl_step, f) / 100, per_frame(g_t_bl_step, f) % 100);
    }
    if (g_bl_life)
        _ffree(g_bl_life);
    g_bl_life = 0;
    g_bl_cap = 0;
    balls_free();
}

#endif /* RV_TESTS */

/* --- the loop's calls (rvflight.inc) --- */

void __far __cdecl flight_startup(void)
{
    const RvMission __far *m = g_m;
    int i, k, n = 1, enemy_type = -1, scene = g_zoom || g_flames_demo || g_lasers;   /* (a test scene: its own ships) */
    unsigned long now;
    memset(maybeEntityType, 0, RV_SLOTS * 2);
    _fmemset(g_was_exact, 0, sizeof g_was_exact);
    _fmemset(g_hold, 0, sizeof g_hold);
    if (g_lasers)
        rv_log("lasers: DOS memory free as the flight starts: %lu KB", bolts_dos_free() / 1024);
    missionStatus = 0;
    frameCounter = 0;
    rvCockpitDamage = 0;
    rvBenchCount = 0;
    rvFps = 0;
    playerCanBeDamaged = 1;
    if (g_test_ship)
        g_camp.player_ship = (unsigned char)(g_test_ship - 1);
    g_my_type = ship_type(g_camp.player_ship);
    cockpit_load(g_camp.player_ship);
    /* the play's random stream for this flight (the same mission flown again from the same save starts the same;
     * with the layer, the host's seed), then the game's logic puts the mission's ships in the slots */
    randomSeed = 0x52560000UL + (unsigned long)camp_mission_index() * 977UL + g_camp.flown;
    logic_start(m);
    for (i = 1; i <= RV_MAX_SHIP_SLOT; i++)
        if (slot_is_ship(i))
            n = i + 1;
    for (i = 0; i < m->nship && enemy_type < 0; i++)   /* (the benchmark's and the test scenes' enemy) */
        if (m->ship[i].side == 1 && m->ship[i].type != RV_SHIP_PLAYER)
            enemy_type = m->ship[i].type == RV_SHIP_WINGMAN ? g_camp.player_ship : m->ship[i].type;
    if (enemy_type < 0)
        enemy_type = 0x08;
    if (g_zoom) {
        /* a test aid: one Gawain dead ahead, a little off the nose, rolling, swinging in and out */
        n = 1;
        memset(maybeEntityType + 1, 0, (RV_SLOTS - 1) * 2);
        ship_spawn(1, 0x02, 0, 4, 0, 0, 256L * 4000, 512 + 100);
        setSpeed[1] = speedLo[1] = 0;
        setSpeed[0] = speedLo[0] = 0;
        ships_load(0x02);
        n = 2;
    }
    if (g_hull_demo && g_hull_type >= 0)
        enemy_type = g_hull_type;       /* (/hulltype: its ship) */
#ifdef RV_TESTS
    if (g_lasers) {
        lasers_setup(enemy_type);
        n = 3;
    }
#endif
    if (g_flames_demo) {
        /* a test aid: six ships 3,000 .. 3,500 km ahead in two rows of three, standing, turning every way */
        static const unsigned char types[6] = { 0x08, 0x02, 0x04, 0x0A, 0x09, 0x06 };
        static const short at[6][3] = { { -1100, 330, 3200 }, { 0, 330, 3500 }, { 1100, 330, 3200 },
                                        { -1100, -330, 3000 }, { 0, -330, 3300 }, { 1100, -330, 3000 } };
        memset(maybeEntityType + 1, 0, (RV_SLOTS - 1) * 2);
        for (k = 0; k < 6; k++) {
            ship_spawn(k + 1, types[k], 1, 4, 256L * at[k][0], 256L * at[k][1], 256L * at[k][2], k * 170);
            setSpeed[k + 1] = speedLo[k + 1] = 0;
            ships_load(types[k]);
        }
        setSpeed[0] = speedLo[0] = 0;
        g_flames_frames = 0;
        n = 7;
    }
    if (g_capital && n <= RV_MAX_SHIP_SLOT) {
        /* a test aid: the Vigilance off to the left, ahead, standing still */
        ship_spawn(n, 0x20, 0, 4, -256L * 3000, 0, 256L * 5000, 256);
        maybeEntityType[n] = 0xD;
        setSpeed[n] = speedLo[n] = 0;
        ships_load(0x20);
        n++;
    }
    if (g_lasers)
        rv_log("lasers: DOS memory free before the ship sets: %lu KB", bolts_dos_free() / 1024);
    {
        /* the sets this flight needs, asked of the page when they are not beside RV.EXE (ships_want) */
        unsigned char want[RV_MAX_SHIP_SLOT + 2 + RV_SHIPS];
        int nw = 0;
        want[nw++] = (unsigned char)enemy_type;
        want[nw++] = g_camp.player_ship;
        for (i = 1; i < n && nw < (int)sizeof want; i++)
            want[nw++] = (unsigned char)gInstanceShipTypes[i];
        for (i = 0; i < m->nship && nw < (int)sizeof want && !scene; i++)
            if (m->ship[i].type < RV_SHIP_WINGMAN)
                want[nw++] = m->ship[i].type;
        ships_want(want, nw, 1);
    }
    g_set_enemy = ships_load(enemy_type);
    g_set_wing = ships_load(g_camp.player_ship);
    for (i = 1; i < n; i++)
        ships_load(gInstanceShipTypes[i]);
    for (i = 0; i < m->nship && !scene; i++)   /* (the mission's ships not in yet: its later waves are drawn too.
                                           SHIP_SETS types at most) */
        if (m->ship[i].type < RV_SHIP_WINGMAN)
            ships_load(m->ship[i].type);
#ifdef RV_TESTS
    if (g_lasers)
        lasers_sets();
#endif
    {
        /* the sets' ladders and rolls, within what the mission level has left (1 MB kept back) */
        unsigned long free_kb = xms_free_kb(), taken, t0 = clock240(), keep = 1024 + rocks_room_kb();   /* (and an
                                           asteroid field's art, read in when the player comes near one) */
        taken = ships_load_blocks(free_kb > keep ? (free_kb - keep) * 1024UL : 0);
        rv_log("ships: %lu KB of blocks loaded in %lu ms, %lu KB of XMS left", taken / 1024, (clock240() - t0) * 1000 / 240,
               xms_free_kb());
    }
    g_cam_cy = cockpit_record() ? (cockpit_record()->open_y0 + cockpit_record()->open_y1) / 2 : SCR_H / 2;
    ships_set_camera(SCR_W / 2, g_cam_cy, 400);
    shots_flight_start(!g_lasers);      /* (the laser benchmark and its demos keep the bolts' and balls' pools) */
#ifdef RV_TESTS
    if (g_set_enemy >= 0) {
        /* the benchmark's sprite: the biggest of the type's band views at the sets' first resident step (84 %) */
        const ShipSet __far *set = ships_set(g_set_enemy);
        long best = 0, paths[5];
        int w, h, tv = 1;
        for (i = 1; i < 36; i++) {
            long area = ships_view_size(set, 1, i, &w, &h) ? (long)w * h : 0;
            if (area > best) {
                best = area;
                g_bench_view = i;
                g_bench_w = w;
                g_bench_h = h;
            }
        }
        /* the kernels checked, then the three draw paths timed on a full-size view (resident) */
        {
            long n = ships_selftest();
            rv_log("lightfield: asm kernels %s the C ones (%ld pixels compared)", n < 0 ? "DISAGREE WITH" : "agree with", n);
        }
        {
            int bw, bh, w, h;
            if (!ships_full_size(g_set_enemy, tv, &bw, &bh))
                bw = bh = 0;
            for (i = 1; i < set->nviews; i++)
                if (ships_full_size(g_set_enemy, i, &w, &h) && (long)w * h > (long)bw * bh) {
                    tv = i;
                    bw = w;
                    bh = h;
                }
            g_tv_w = bw;
            g_tv_h = bh;
        }
        ships_time_paths(g_set_enemy, tv, paths);
        rv_log("lightfield: view %d (%dx%d): copy %ld px/ms, scaled to 3/4 %ld, turned 45 degrees at 3/4 %ld, copy upside down %ld, copy mirrored %ld",
               tv, g_tv_w, g_tv_h, paths[0], paths[1], paths[2], paths[3], paths[4]);
    }
    if (g_lasers)
        lasers_pool();
    g_nbench = 0;
    g_bench_sum = 0;
    g_bench_frames = 0;
#endif
    g_steer_x = g_steer_y = 0;
    g_exit = 0;
    g_damage_next = 0;
    stars_init();
    now = clock240();
    g_t0 = now;
    g_damage_at = now + 240 * 3;
    for (i = 0; i < FPS_WINDOW; i++)
        g_frame_t[i] = now;
    g_fps10 = 0;
    memset(&g_st, 0, sizeof g_st);
    g_scene.full = flight_draw;
    g_scene.no_room = 1;
    g_scene.hide_pointer = 1;
    in_steer(0);
    in_warp(SCR_W / 2, cockpit_record() ? (cockpit_record()->open_y0 + cockpit_record()->open_y1) / 2 : SCR_H / 2);
    gather();
    plan_draws();                       /* (this flight's ships for its first picture: the last flight's plan named
                                           its sets, which a flight with fewer types has not loaded) */
    vid_fade(0);
    vid_dirty(0, SCR_H);
    cockpit_dirty(&g_st);
    scene_draw();
    vid_fade(64);
#ifdef RV_TESTS
    rv_log("flight: %.16Fs, %d ships, benchmark view %d (%dx%d)", (char __far *)m->id, n - 1, g_bench_view, g_bench_w,
           g_bench_h);
#else
    rv_log("flight: %.16Fs, %d ships", (char __far *)m->id, n - 1);
#endif
    cockpit_palette();                  /* only now: the deck kept its own palette while all that loaded (seconds in the
                                           browser: the user saw it go "all crazy" in the flight's palette) */
    flames_init();                      /* (its colours from that palette) */
    comms_flight_start();
    systems_start();
    score_start(g_m);                   /* (the first calm song: the deck's launch music gives way) */
    g_hailed = 0;
    missiles_init();
    sky_clear();
    rocks_flight_start();
    if (g_field > 0)                    /* (centred where the player starts: big enough to fly in for minutes) */
        rocks_field_add(RV_FIELD_ASTEROID, gPositionVector[0].x, gPositionVector[0].y, gPositionVector[0].z, 200000L,
                        g_field);
#ifdef RV_TESTS
    if (g_sky_demo)
        sky_demo_setup();
#endif
    g_t_flames = g_t_flames_set = g_t_plan = 0;
    g_t_exact = g_t_sets = g_t_draw = g_t_wait = 0;
}

/* the keys and the pointer: returns the status the exit key sets, else 0 */
unsigned short __far __cdecl flight_keys(void)
{
    Input in;
    int arrows = in_arrows(), dx = ((arrows >> 1) & 1) - (arrows & 1), dy = ((arrows >> 3) & 1) - ((arrows >> 2) & 1);
    const RvCockpit __far *c = cockpit_record();
    int cx = SCR_W / 2, cy = c ? (c->open_y0 + c->open_y1) / 2 : SCR_H / 2;
    in_poll(&in);
    if (in.key == KEY_F12)
        shot_save("RVSHOT");
    if (in.key == KEY_ESC && script_active())
        return 5;                       /* a test script ends the flight; the player's Esc is the browser's key, ignored */
    if (in.key == KEY_CHAR) {
        if (in.ch == '+' || in.ch == '=') setSpeed[0] += g_st.max_speed / 10;
        if (in.ch == '-') setSpeed[0] = setSpeed[0] > (unsigned long)g_st.max_speed / 10 ? setSpeed[0] - g_st.max_speed / 10 : 0;
        if (in.ch == 'f' || in.ch == 'F') setSpeed[0] = g_st.max_speed;
        if (in.ch == 's' || in.ch == 'S') setSpeed[0] = 0;
        if (in.ch == 'a' || in.ch == 'A') rv_autopilot_key();
        else system_key(in.ch);
        if (in.ch == 'd' || in.ch == 'D') {   /* the next damage layer the cockpit has */
            int k;
            for (k = 0; k < RV_CP_LAYERS; k++) {
                int bit = (g_damage_next + k) % RV_CP_LAYERS;
                if (c && c->layer[bit].sprite != 0xFFFF && !((rvCockpitDamage >> bit) & 1)) {
                    rvCockpitDamage |= 1UL << bit;
                    g_damage_next = bit + 1;
                    break;
                }
            }
        }
        if (in.ch == 'r' || in.ch == 'R') rvCockpitDamage = 0;
    }
    {   /* Ctrl+E pulls the eject handle (twice within two seconds ejects); a click on the panel works its control */
        static unsigned char __far ctrl_e;
        int fn, arg, e = in_key_down(0x1D) && in_key_down(0x12);
        if (e && !ctrl_e)
            eject_pull();
        ctrl_e = (unsigned char)e;
        if (in.clicked && c && in.y >= c->open_y1 && panel_at(in.x, in.y, &fn, &arg))
            system_action(fn, arg);
    }
    if (setSpeed[0] > (unsigned long)g_st.max_speed)
        setSpeed[0] = g_st.max_speed;
    if (in_key_down(0x0D) || in_key_down(0x4E)) setSpeed[0] += 2;    /* = and keypad + held */
    if ((in_key_down(0x0C) || in_key_down(0x4A)) && setSpeed[0] >= 2) setSpeed[0] -= 2;
    g_steer_roll = ((arrows >> 5) & 1) * 9 - ((arrows >> 4) & 1) * 9;
    /* steering: the arrows at full rate, else the pointer's distance from the middle */
    if (dx || dy) {
        g_keyboard_steer = 1;
        g_steer_x = dx * 9;
        g_steer_y = dy * 9;
    } else if (in.keyboard && !in.moved) {
        g_keyboard_steer = 1;
        g_steer_x = g_steer_y = 0;
    } else {
        int ox = in.x - cx, oy = in.y - cy;
        g_keyboard_steer = 0;
        g_steer_x = ox > 16 ? (ox > 120 ? 9 : ox * 9 / 120) : ox < -16 ? (ox < -120 ? -9 : ox * 9 / 120) : 0;
        g_steer_y = oy > 12 ? (oy > 80 ? 9 : oy * 9 / 80) : oy < -12 ? (oy < -80 ? -9 : oy * 9 / 80) : 0;
    }
    steerInput = ((unsigned long)(unsigned short)(short)g_steer_x << 16) | (unsigned short)(short)g_steer_y;
    /* the pilot's controls for the game's logic (logic.h): the turn, the trigger (Space, or the left button held with
     * the pointer in the view: below it the panel's controls take the clicks), the missile, the afterburner */
    g_pilot.steer_x = (signed char)g_steer_x;
    g_pilot.steer_y = (signed char)g_steer_y;
    g_pilot.roll = (signed char)g_steer_roll;
    g_pilot.guns = (unsigned char)(in_trigger() || (in_button() && c && in.y < c->open_y1));
    g_pilot.missile = (unsigned char)(in.key == KEY_ENTER);
    g_pilot.afterburner = g_sys.burning;
    if (in.x != g_scene.px || in.y != g_scene.py) {
        g_scene.px = in.x;
        g_scene.py = in.y;
    }
    return 0;
}

void __far __cdecl flight_frame(void)
{
    unsigned long now = clock240(), dt;
    int k = (int)(frameCounter % FPS_WINDOW);
#ifdef RV_TESTS
    if (g_flame_test == 1) {            /* (the self-test: once, before the first frame's plan) */
        g_flame_test = 2;
        flame_test();
    }
    if (g_comm_demo && !comms_on(0) && !comms_on(1))
        comm_demo_next();
#endif
    if (!g_comm_demo && frameCounter == 90)
        comm_launch();
    else if (!g_comm_demo)
        comm_hail();
    comms_tick();
    sim_step();
#ifdef RV_TESTS
    if (g_sky_demo)
        sky_demo_step();
    if (g_collide_demo)
        collide_demo_step();
    if (g_hull_demo)
        hull_demo_step();
#endif
    {
        LfBasis cam;                    /* the asteroid fields: theirs entered and left, the rocks placed, the hits */
        const RvCockpit __far *c = cockpit_record();
        basis_of(0, &cam);
        rocks_frame(&cam, SCR_W / 2, g_cam_cy, 400, c ? c->open_y0 : 0, c ? c->open_y1 : SCR_H);
    }
    stars_place();
    {
        LfBasis cam;
        basis_of(0, &cam);
        sky_place(&cam, SCR_W / 2, g_cam_cy, 400);
    }
    {
        unsigned long t0 = clock240();
        plan_draws();
        g_t_plan += clock240() - t0;
    }
#ifdef RV_TESTS
    if (g_lasers) {
        if (g_bolt_demo)
            demo_step();
        else if (g_missile_demo)
            missile_demo_step();
        else if (!g_ball_demo && !g_sky_demo && !g_collide_demo && !g_hull_demo)
            lasers_step();
        balls_step();
        lasers_place();
    }
#endif
    missiles_frame();                   /* (after the plan: the ships' flames are the frame's first) */
    {
        LfBasis cam;                    /* the shots (shots.c): through the camera, as the bolts and the balls */
        const RvCockpit __far *c = cockpit_record();
        basis_of(0, &cam);
        shots_place((const LfVec *)&gPositionVector[0], &cam, SCR_W / 2, g_cam_cy, 400, c ? c->open_y0 : 0,
                    c ? c->open_y1 : SCR_H);
    }
    /* the rate over the last FPS_WINDOW frames */
    dt = now - g_frame_t[k];
    g_frame_t[k] = now;
    g_fps10 = dt ? (int)(2400UL * FPS_WINDOW / dt) : 999;
    rvFps = (unsigned short)g_fps10;
#ifdef RV_TESTS
    bench_step();
#endif
#ifdef RV_TESTS
    /* the benchmark's scenery: a damage layer more every three seconds (not in the laser benchmark: they hid its
     * readout).  (The test build's: in the game the damage is the game's logic's) */
    if (now >= g_damage_at && !g_lasers) {
        const RvCockpit __far *c = cockpit_record();
        int i;
        g_damage_at = now + 240 * 3;
        for (i = 0; i < RV_CP_LAYERS; i++) {
            int bit = (g_damage_next + i) % RV_CP_LAYERS;
            if (c && c->layer[bit].sprite != 0xFFFF && !((rvCockpitDamage >> bit) & 1)) {
                rvCockpitDamage |= 1UL << bit;
                g_damage_next = bit + 1;
                break;
            }
        }
        SHIELD(0, 0) = SHIELD(0, 0) > 2 ? SHIELD(0, 0) - 2 : 0;
        ARMOR(0, 0) = ARMOR(0, 0) > 1 ? ARMOR(0, 0) - 1 : 0;
    }
#endif
    gather();
    cockpit_dirty(&g_st);
    {
        unsigned long t0 = clock240();
        scene_draw();
        g_t_draw += clock240() - t0;
    }
    if (g_record && frameCounter % g_record == 0) {
        char name[12];
        sprintf(name, "R%05u", frameCounter / g_record);
        shot_save(name);
    }
    rvFrameDone();
    if (g_flight_timed && now - g_t0 >= FLIGHT_TICKS)
        missionStatus = 1;              /* landed */
    if (g_landing == 2 ? !comms_on(0) : g_landing && now >= g_land_at)
        missionStatus = 1;              /* landed: the call said */
    if (g_flames_demo && g_record && ++g_flames_frames >= FLAMES_FRAMES)
        missionStatus = 1;              /* (its recording is so many frames, however slowly they are made) */
    /* the frame rate capped at FPS_WANT: the simulation is per frame (a kps is 12.8 units a frame), so
     * without the cap a bare flight ran three times too fast natively (the adapting benchmark had been
     * holding it at 30 by loading it; off by default now) */
    if (g_bench_wanted >= 0 && g_lasers >= 0 && g_balls >= 0) {   /* (the adapting benchmarks: the uncapped rate) */
        unsigned long t0 = clock240();
        while (clock240() - now < 240 / FPS_WANT)
            rv_halt();
        g_t_wait += clock240() - t0;
    }
}

void __far __cdecl flight_music(void)
{
    music_poll();
    score_tick(g_landing != 0);
}

void __far __cdecl flight_autopilot(void)
{
    static const char __far s_went[] = "flight: autopilot: on to nav %u", __far s_not[] = "flight: autopilot: not now";
    if (logic_autopilot())
        rv_logf(s_went, currentNavPoint);
    else
        rv_logf(s_not);
}

void __far __cdecl autoAnimation_c(unsigned short camShipType, unsigned short camMode, unsigned short duration)
{
    rv_log("flight: autoAnimation(%u, %u, %u)", camShipType, camMode, duration);
}

/* --- from the game --- */

int flight_fly(const RvMission __far *m)
{
    unsigned short status;
    g_m = m;
    uiMode(0);
    status = rv_flight_run();           /* (its loop begins with flight_startup: the loading, then the palette) */
#ifdef RV_TESTS
    {
        int w, h, n = flight_bench_result(&w, &h);
        rv_log("flight: over with status %u after %u frames; benchmark %d sprites of %dx%d a frame at %d fps (%ld pixels a second)",
               status, frameCounter, n, w, h, FPS_WANT, (long)n * w * h * FPS_WANT);
    }
#else
    rv_log("flight: over with status %u after %u frames", status, frameCounter);
#endif
    {
        unsigned long f = frameCounter, frame = (clock240() - g_t0) - g_t_wait;
        static const char __far s_ms[] = "flight: ms a frame: the plan %lu.%02lu; the picture %lu.%02lu (ships drawn "
            "exactly %lu.%02lu, from the sets %lu.%02lu); the whole frame %lu.%02lu";
        rv_logf(s_ms, per_frame(g_t_plan, f) / 100, per_frame(g_t_plan, f) % 100,
                per_frame(g_t_draw, f) / 100, per_frame(g_t_draw, f) % 100, per_frame(g_t_exact, f) / 100,
                per_frame(g_t_exact, f) % 100, per_frame(g_t_sets, f) / 100, per_frame(g_t_sets, f) % 100,
                per_frame(frame, f) / 100, per_frame(frame, f) % 100);
    }
    {
        static const char __far s_m[] = "flight: the missiles took %lu.%02lu ms a frame to place and %lu.%02lu to draw "
            "(the %lu frames with any flying)";
        static const char __far s_s[] = "flight: the sky's far things took %lu.%02lu ms a frame to place and "
            "%lu.%02lu to draw";
        unsigned long t[3];
        missiles_ticks(t);
        if (t[2])
            rv_logf(s_m, per_frame(t[0], (unsigned)t[2]) / 100, per_frame(t[0], (unsigned)t[2]) % 100,
                    per_frame(t[1], (unsigned)t[2]) / 100, per_frame(t[1], (unsigned)t[2]) % 100, t[2]);
        sky_ticks(t);
        if (t[0] + t[1])
            rv_logf(s_s, per_frame(t[0], frameCounter) / 100, per_frame(t[0], frameCounter) % 100,
                    per_frame(t[1], frameCounter) / 100, per_frame(t[1], frameCounter) % 100);
    }
    rv_log("flight: the engines' flames took %lu.%02lu ms a frame (%lu.%02lu of it set up)",
           g_t_flames * 1000 / 240 / (frameCounter ? frameCounter : 1),
           (g_t_flames * 100000 / 240 / (frameCounter ? frameCounter : 1)) % 100,
           g_t_flames_set * 1000 / 240 / (frameCounter ? frameCounter : 1),
           (g_t_flames_set * 100000 / 240 / (frameCounter ? frameCounter : 1)) % 100);
    rv_log("flight: the stars took %lu.%02lu ms a frame to place and %lu.%02lu to draw",
           g_star_place_ticks * 1000 / 240 / (frameCounter ? frameCounter : 1),
           (g_star_place_ticks * 100000 / 240 / (frameCounter ? frameCounter : 1)) % 100,
           g_star_draw_ticks * 1000 / 240 / (frameCounter ? frameCounter : 1),
           (g_star_draw_ticks * 100000 / 240 / (frameCounter ? frameCounter : 1)) % 100);
#ifdef RV_TESTS
    if (g_lasers) {
        lasers_report();
        balls_report();
    }
    if (g_collide_demo)
        collide_report();
    if (g_hull_demo)
        hull_report();
#endif
    g_star_place_ticks = g_star_draw_ticks = 0;
    g_scene.full = 0;
    g_scene.no_room = 0;
    g_scene.hide_pointer = 0;
    in_steer(1);
    ships_draw_log();
    rocks_report();
    logic_end();
    shots_flight_end();
    rocks_flight_end();
    comms_flight_end();
    ships_unload();
    collide_flight_end();
    cockpit_unload();
    rv_hangar();
    uiMode(1);
    return status;
}
