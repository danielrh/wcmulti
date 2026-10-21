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
#include "flight.h"

int g_flight_seconds = 20;              /* the benchmark flight's length (RV.EXE /flight N) */
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
#define DEMO_BOLTS 11
#define DEMO_FRAMES 300                 /* ten seconds at 30 fps */
#define DEMO_SPEED (27L * 64 / BOLT_HALF16)   /* positions per sixteenth of a km of half axis: 6.75 km a frame */
#define DEMO_METRE 32                   /* km a metre */
static int g_demo_frame;                /* its frames so far */
/* the hybrid: the biggest ships on screen are drawn exactly (turned and scaled
 * at draw time, sixteen cycles a pixel) while their pixels fit this budget
 * a frame and they are big enough for the steps to show; the rest are
 * copies from the pre-rendered sets (RV.EXE /onfly N sets the budget) */
long g_onfly_px = 32000L;
#define ONFLY_MIN_SCALE 100
#define PLAN_MAX 16
static struct { unsigned char slot, exact, set; long depth; int scale; } g_plan[PLAN_MAX];   /* exact: 0, or the detail (1, 2, 4) */
static int g_nplan;
#define FLIGHT_TICKS ((unsigned long)g_flight_seconds * 240)   /* ... on the 240 Hz clock */
/* the shield words (front, rear) and the armour words (front, rear, left, right) of a slot */
#define SHIELD(s, k) (((unsigned short *)curShield)[(s) * 2 + (k)])
#define SHIELD_MAX(s, k) (((unsigned short *)shieldMax)[(s) * 2 + (k)])
#define ARMOR(s, k) (((unsigned short *)armorState)[(s) * 4 + (k)])
#define FPS_WINDOW 8                    /* frames the rate is measured over */
#define FPS_WANT 30
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

/* the benchmark's sprites: screen positions drifting about the opening */
static struct { short x, y, dx, dy; } __far g_bench[BENCH_MAX];
static int g_nbench, g_bench_view = 17, g_bench_w, g_bench_h;
static long g_bench_sum;                /* the count summed over the settled half, for the result */
static int g_bench_frames;
static unsigned long g_vseed = 12345;   /* visual randomness: outside the ABI's stream */

static unsigned vrnd(void)
{
    g_vseed = g_vseed * 1103515245UL + 12345UL;
    return (unsigned)(g_vseed >> 16) & 0x7FFF;
}

/* --- the world's geometry --- */

static void vec_norm(RvVec3 *v)
{
    unsigned long len = lf_sqrt((unsigned long)(v->x * v->x + v->y * v->y + v->z * v->z));
    if (!len)
        return;
    v->x = v->x * 256 / (long)len;
    v->y = v->y * 256 / (long)len;
    v->z = v->z * 256 / (long)len;
}

static long vec_dot(const RvVec3 *a, const RvVec3 *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

static void vec_cross(RvVec3 *out, const RvVec3 *a, const RvVec3 *b)
{
    out->x = (a->y * b->z - a->z * b->y) / 256;
    out->y = (a->z * b->x - a->x * b->z) / 256;
    out->z = (a->x * b->y - a->y * b->x) / 256;
}

/* a and b turned by angle t in their plane: a' = a cos + b sin, b' = b cos - a sin */
static void rotate_pair(RvVec3 *a, RvVec3 *b, int t)
{
    long c = lf_cos(t), s = lf_sin(t);
    RvVec3 na, nb;
    na.x = (a->x * c + b->x * s) >> 14; na.y = (a->y * c + b->y * s) >> 14; na.z = (a->z * c + b->z * s) >> 14;
    nb.x = (b->x * c - a->x * s) >> 14; nb.y = (b->y * c - a->y * s) >> 14; nb.z = (b->z * c - a->z * s) >> 14;
    *a = na;
    *b = nb;
}

/* the frame kept right-handed and unit length (the layer may write values that drift) */
static void orthonormalise(int s)
{
    RvVec3 *f = &gOrientationFrontVector[s], *r = &gOrientationRightVector[s], *u = &gOrientationUpVector[s];
    long d;
    vec_norm(f);
    d = vec_dot(f, r) / 256;
    r->x -= f->x * d / 256; r->y -= f->y * d / 256; r->z -= f->z * d / 256;
    vec_norm(r);
    vec_cross(u, f, r);
    vec_norm(u);
}

static void face(int s, int yaw, int pitch)
{
    gOrientationFrontVector[s].x = 0; gOrientationFrontVector[s].y = 0; gOrientationFrontVector[s].z = 256;
    gOrientationRightVector[s].x = 256; gOrientationRightVector[s].y = 0; gOrientationRightVector[s].z = 0;
    gOrientationUpVector[s].x = 0; gOrientationUpVector[s].y = 256; gOrientationUpVector[s].z = 0;
    rotate_pair(&gOrientationFrontVector[s], &gOrientationRightVector[s], yaw);
    rotate_pair(&gOrientationFrontVector[s], &gOrientationUpVector[s], pitch);
    orthonormalise(s);
}

/* --- the simulation --- */

static const RvShipType __far *type_of(int s)
{
    return ship_type(gInstanceShipTypes[s]);
}

/* degrees a second to the turn per frame (1024 a turn, 20 frames a second) */
static int turn_step(int dps, int steer)
{
    return (int)((long)dps * 1024 / 360 / 20 * steer / 9);
}

static void ship_physics(int s)
{
    const RvShipType __far *t = type_of(s);
    long want = (long)setSpeed[s], have = (long)speedLo[s], accel;
    int yaw = 0, pitch = 0;
    if (!t)
        return;
    if (rvController[s] == 1) {
        yaw = g_steer_x;
        pitch = g_steer_y;
        if (g_steer_roll)
            rotate_pair(&gOrientationRightVector[s], &gOrientationUpVector[s], turn_step(t->roll_dps, g_steer_roll));
    } else if (rvController[s] == 0 && commAnimInfo[s] == 0) {
        /* a friendly stand-in keeps station off the player's right, 1,000 km out and 2,500 ahead,
         * flying level and straight (it does not roll with the player) */
        gPositionVector[s].x = gPositionVector[0].x + 256L * 1000;
        gPositionVector[s].y = gPositionVector[0].y;
        gPositionVector[s].z = gPositionVector[0].z + 256L * 2500;
        speedLo[s] = speedLo[0];
        return;
    } else if (rvController[s] == 0) {
        /* an enemy stand-in: turns toward the player while far, passes by when near */
        RvVec3 d;
        long dr, du, df, dist;
        d.x = (gPositionVector[0].x - gPositionVector[s].x) / 256;   /* in km */
        d.y = (gPositionVector[0].y - gPositionVector[s].y) / 256;
        d.z = (gPositionVector[0].z - gPositionVector[s].z) / 256;
        dr = vec_dot(&d, &gOrientationRightVector[s]) / 256;
        du = vec_dot(&d, &gOrientationUpVector[s]) / 256;
        df = vec_dot(&d, &gOrientationFrontVector[s]) / 256;
        dist = (dr < 0 ? -dr : dr) + (du < 0 ? -du : du) + (df < 0 ? -df : df);   /* km, roughly */
        if (dist > 3000 || df < 0) {
            yaw = dr > 0 ? 6 : dr < 0 ? -6 : 0;
            pitch = du > 0 ? -5 : du < 0 ? 5 : 0;
        } else {
            /* passing: a slow turn, and a steady roll about its own axis */
            yaw = (int)((s & 1) ? 3 : -3);
            rotate_pair(&gOrientationRightVector[s], &gOrientationUpVector[s], turn_step(t->roll_dps, (s & 1) ? 4 : -4));
        }
    }
    if (g_lasers && s >= 1 && s <= 2) {
        /* the laser benchmark's two big ships: in place, rolling and turning (so they are drawn exactly, turned
         * and scaled every frame).  (An `if (s == 0) ... else` here, the player rolling in the first arm, was
         * miscompiled by -ox: slot 0 ran the else arm's turns.) */
        rotate_pair(&gOrientationRightVector[s], &gOrientationUpVector[s], turn_step(40, s == 1 ? 9 : -6));
        rotate_pair(&gOrientationFrontVector[s], &gOrientationRightVector[s], turn_step(15, 9));
        orthonormalise(s);
        return;
    }
    if (g_zoom && s == 1) {
        /* the zoom scene's ship, 1,200 .. 6,000 km away on a twelve-second swing: /zoom 1 rolls it
         * at 60 degrees a second (the lightfield's view switches as the camera walks round its
         * nose); /zoom 2 yaws it slowly instead, for the player to roll round; /zoom 3 the same
         * swinging 3,000 .. 15,000 km, through the sets' sizes (the player's roll on far ships) */
        long d = g_zoom == 3 ? 256L * 9000 + ((lf_cos((int)(frameCounter * 1024L / 240 % 1024)) * 6000L) >> 14) * 256
                             : 256L * 3600 + ((lf_cos((int)(frameCounter * 1024L / 240 % 1024)) * 2400L) >> 14) * 256;
        if (g_zoom == 1)
            rotate_pair(&gOrientationRightVector[s], &gOrientationUpVector[s], turn_step(60, 9));
        else
            rotate_pair(&gOrientationFrontVector[s], &gOrientationRightVector[s], turn_step(12, 9));
        gPositionVector[s].z = gPositionVector[0].z + d;
        return;
    }
    if (yaw)
        rotate_pair(&gOrientationFrontVector[s], &gOrientationRightVector[s], turn_step(t->yaw_dps, yaw));
    if (pitch)
        rotate_pair(&gOrientationFrontVector[s], &gOrientationUpVector[s], turn_step(t->pitch_dps, pitch));
    orthonormalise(s);
    /* the throttle: kps a second of acceleration, a frame's worth */
    accel = (long)t->accel / 20 + 1;
    if (have < want) have = have + accel > want ? want : have + accel;
    if (have > want) have = have - accel < want ? want : have - accel;
    speedLo[s] = (unsigned long)have;
    /* 1 kps = 12.8 position units a frame: front (256 = 1.0) x kps / 20 */
    gVelocityVector[s].x = gOrientationFrontVector[s].x * have / 20;
    gVelocityVector[s].y = gOrientationFrontVector[s].y * have / 20;
    gVelocityVector[s].z = gOrientationFrontVector[s].z * have / 20;
    gPositionVector[s].x += gVelocityVector[s].x;
    gPositionVector[s].y += gVelocityVector[s].y;
    gPositionVector[s].z += gVelocityVector[s].z;
}

static void sim_step(void)
{
    int s;
    frameCounter++;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++)
        if (maybeEntityType[s] == 0xC || maybeEntityType[s] == 0xD)
            ship_physics(s);
}

/* a ship into a slot: its type's numbers, at a place, facing a way */
static void spawn(int s, int type, int side, int controller, long x, long y, long z, int yaw)
{
    const RvShipType __far *t = ship_type(type);
    RV_CHECK(s, RV_SLOTS);
    maybeEntityType[s] = t && t->type >= 0x20 ? 0xD : 0xC;
    gInstanceShipTypes[s] = (unsigned short)type;
    commAnimInfo[s] = (unsigned short)side;
    rvController[s] = (unsigned char)controller;
    gPositionVector[s].x = x; gPositionVector[s].y = y; gPositionVector[s].z = z;
    face(s, yaw, 0);
    setSpeed[s] = t ? t->cruise_kps : 200;
    speedLo[s] = setSpeed[s];
    SHIELD(s, 0) = SHIELD_MAX(s, 0) = t ? t->shield_f : 10;
    SHIELD(s, 1) = SHIELD_MAX(s, 1) = t ? t->shield_r : 10;
    ARMOR(s, 0) = t ? t->armor_f : 10;
    ARMOR(s, 1) = t ? t->armor_r : 10;
    ARMOR(s, 2) = t ? t->armor_l : 10;
    ARMOR(s, 3) = t ? t->armor_rt : 10;
    coreHp[s] = 4;
    rvFlags[s] = 0;
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
    long d = (long)lf_sqrt((unsigned long)((rel->x / 64) * (rel->x / 64) + (rel->y / 64) * (rel->y / 64)
                                           + (rel->z / 64) * (rel->z / 64))) * 64;
    long sc = d ? FULL_SIZE_UNITS * 256 / d : 512;
    return (int)(sc > 512 ? 512 : sc);
}

static void rel_of(int s, LfVec *rel)
{
    rel->x = gPositionVector[s].x - gPositionVector[0].x;
    rel->y = gPositionVector[s].y - gPositionVector[0].y;
    rel->z = gPositionVector[s].z - gPositionVector[0].z;
}

/* once a frame: which ships show, how big, which are drawn exactly, in what order */
static void plan_draws(void)
{
    LfBasis cam, ship;
    LfVec rel;
    LfPlace p;
    long px[PLAN_MAX], budget = g_onfly_px;
    int s, i, k;
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
        g_plan[g_nplan].exact = 0;
        g_nplan++;
    }
    /* the exact ones: the biggest first, while the budget holds; one too big
     * for what is left is drawn at half or quarter detail (rows copied, then
     * 2 x 1 blocks) rather than from the sets or slower */
    for (;;) {
        int best = -1, detail;
        for (i = 0; i < g_nplan; i++)
            if (!g_plan[i].exact && g_plan[i].scale >= ONFLY_MIN_SCALE && px[i] > 0 && px[i] <= budget * 4
                && (best < 0 || px[i] > px[best]))
                best = i;
        if (best < 0)
            break;
        detail = px[best] <= budget ? 1 : px[best] <= budget * 2 ? 2 : 4;
        g_plan[best].exact = (unsigned char)detail;
        budget -= px[best] / detail;
        /* its full-size view streamed in now (the nearest first), if the frame's reads allow */
        rel_of(g_plan[best].slot, &rel);
        basis_of(g_plan[best].slot, &ship);
        ships_prefetch(g_plan[best].set, &rel, &cam, &ship);
    }
    if (g_sky && g_set_enemy >= 0)      /* the sky scene's views, in before the draws */
        for (i = 0; i < 3; i++)
            ships_prefetch_view(g_set_enemy, g_sky_sprites[i].v);
    /* back to front */
    for (i = 1; i < g_nplan; i++)
        for (k = i; k > 0 && g_plan[k].depth > g_plan[k - 1].depth; k--) {
            struct { unsigned char slot, exact, set; long depth; int scale; } t = g_plan[k];
            g_plan[k] = g_plan[k - 1];
            g_plan[k - 1] = t;
        }
}

static void lasers_draw_far(int y0, int y1, const short __far *ext, int ey0, int rows);
static unsigned long g_t_exact, g_t_sets;   /* what the slots' ships cost to draw: exactly, and from the sets */
static unsigned long g_world_calls;     /* draw_world's calls: the bands a frame draws the world in */

static void draw_world(int y0, int y1)
{
    int ey0, rows, i;
    const short __far *ext = cockpit_extents(&ey0, &rows);
    const ShipSet __far *bench = g_set_enemy >= 0 ? ships_set(g_set_enemy) : 0;
    LfBasis cam, ship;
    int path;
    unsigned long t0;
    g_world_calls++;
    draw_stars(y0, y1);
    if (g_lasers)
        lasers_draw_far(y0, y1, ext, ey0, rows);
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
        t0 = clock240();
        if (g_plan[i].exact && ships_draw_exact(set, &rel, &cam, &ship, g_plan[i].scale, g_plan[i].exact, y0, y1,
                                                0, SCR_W, ext, ey0, rows)) {
            path = 8 + g_plan[i].exact;
            g_t_exact += clock240() - t0;
        } else {                        /* (or its view is not streamed in yet: from the sets this frame) */
            path = ships_draw(set, &rel, &cam, &ship, g_plan[i].scale, y0, y1, 0, SCR_W, ext, ey0, rows);
            g_t_sets += clock240() - t0;
        }
        if (y0 <= ey0 && (frameCounter % 100 == 1 || (g_record && frameCounter % g_record == 0))) {
            /* now and then, where each ship was put (tests/ships.txt reads it) */
            LfPlace p;
            int nb, nh;
            ships_lattice(set, g_plan[i].exact != 0, &nb, &nh);
            lf_place(&rel, &cam, &ship, SCR_W / 2, (ey0 + rows / 2), 400, nb, nh, &p);
            rv_log("lightfield: frame %u slot %d type %02X at %ld,%ld,%ld: view %d roll %d scale %d at %d,%d path %d",
                   frameCounter, s, gInstanceShipTypes[s], rel.x, rel.y, rel.z, p.view, p.roll, g_plan[i].scale, p.sx, p.sy, path);
        }
    }
    if (g_lasers)                       /* the near bolts last: nearest of all */
        bolts_draw_near(y0, y1, ext, ey0, rows);
    /* the benchmark's copies */
    if (bench)
        for (i = 0; i < g_nbench; i++)
            ships_blit_size(bench, 1, g_bench_view, g_bench[i].x, g_bench[i].y, y0, y1, 0, SCR_W, ext, ey0, rows);
}

static int g_ptr_y0, g_ptr_y1;

static void __far __cdecl draw_pointer_body(void)
{
    /* the steering cross where the pointer is, in mouse mode */
    int x = g_scene.px, y = g_scene.py;
    if (g_keyboard_steer || g_bolt_demo)   /* (the demo's middle bolt is where the cross would be) */
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
    /* the right MFD: the target's view, clipped to the glass */
    const ShipSet __far *set;
    if (which != 1 || g_set_enemy < 0)
        return;
    set = ships_set(g_set_enemy);
    if (y1 <= r->y || y0 >= r->y + r->h)
        return;
    ships_blit_size(set, 1, 10, r->x + r->w / 2, r->y + r->h / 2, y0 > r->y ? y0 : r->y,
                    y1 < r->y + r->h ? y1 : r->y + r->h, r->x, r->x + r->w, 0, 0, 0);
}

static int lasers_far(void);

/* the cockpit's state from the slots */
static void gather(void)
{
    const RvCockpit __far *c = cockpit_record();
    int s, n = 0, i;
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
    g_st.fuel = 100 - (int)((clock240() - g_t0) / 240 * 100 / 600);
    g_st.stick_x = g_steer_x > 0 ? 1 : g_steer_x < 0 ? -1 : 0;
    g_st.stick_y = g_steer_y > 0 ? 1 : g_steer_y < 0 ? -1 : 0;
    g_st.damage = rvCockpitDamage;
    g_st.lock = 0;
    g_st.warn = g_st.shield_f < 34 || g_st.shield_r < 34;
    /* the radar: the nearest contacts, where they are relative to the nose */
    for (s = 1; s <= RV_MAX_SHIP_SLOT && n < CP_CONTACTS; s++) {
        RvVec3 d;
        long x, y, z, len, ang;
        int a, rr;
        if (maybeEntityType[s] != 0xC && maybeEntityType[s] != 0xD)
            continue;
        d.x = (gPositionVector[s].x - gPositionVector[0].x) / 16;
        d.y = (gPositionVector[s].y - gPositionVector[0].y) / 16;
        d.z = (gPositionVector[s].z - gPositionVector[0].z) / 16;
        x = vec_dot(&d, &gOrientationRightVector[0]) / 256;
        y = vec_dot(&d, &gOrientationUpVector[0]) / 256;
        z = vec_dot(&d, &gOrientationFrontVector[0]) / 256;
        len = (long)lf_sqrt((unsigned long)(x * x + y * y + z * z));
        if (!len)
            continue;
        /* the angle off the nose, 0 the middle .. 512 (behind) the rim */
        ang = lf_atan2((long)lf_sqrt((unsigned long)(x * x + y * y)), z);
        rr = (int)(ang * 256 / 512);
        a = lf_atan2(y, x);
        g_st.contact[n].x = (short)((lf_cos(a) * rr) >> 14);
        g_st.contact[n].y = (short)(-((lf_sin(a) * rr) >> 14));   /* up is up the screen */
        g_st.contact[n].colour = commAnimInfo[s] == 0 ? UI_HILITE : commAnimInfo[s] == 1 ? UI_RED : UI_AMBER4;
        n++;
    }
    g_st.ncontacts = n;
    (void)c;
    if (g_bolt_demo) {                  /* the demo's readout: the drawing, the time in its round, where the bolts are */
        static const char __far name[] = "CAPSULES";
        int k = g_demo_frame ? (g_demo_frame - 1) % DEMO_FRAMES : 0;
        _fstrcpy((char __far *)g_mfd[0][0], name);   /* (far: DGROUP is full) */
        sprintf(g_mfd[0][1], "T+%d.%d S", k / 30, k % 30 / 3);
        sprintf(g_mfd[0][2], "%ld KM", BOLT_HALF16 / 16 + (long)k * DEMO_SPEED * BOLT_HALF16 / 256);
    } else if (g_lasers) {              /* the laser benchmark's readout */
        sprintf(g_mfd[0][0], "BOLTS %d+%d", g_near_bolts, lasers_far());
        sprintf(g_mfd[0][1], "%d.%d FPS", g_fps10 / 10, g_fps10 % 10);
        sprintf(g_mfd[0][2], "SHIPS %d+%d", g_big_ships, g_far_ships);
    } else if (g_nbench) {              /* the benchmark's readout; else the controls (the user asked how to roll) */
        sprintf(g_mfd[0][0], "SPRITES %d", g_nbench);
        sprintf(g_mfd[0][1], "%d.%d FPS", g_fps10 / 10, g_fps10 % 10);
        sprintf(g_mfd[0][2], "%dx%d VIEW", g_bench_w, g_bench_h);
    } else {
        strcpy(g_mfd[0][0], "ARROWS TURN");
        strcpy(g_mfd[0][1], "Q E ROLL");
        strcpy(g_mfd[0][2], "+ - F S SPD");
    }
    if (g_bolt_demo) {
        int lines, fats;
        bolts_on_screen(&lines, &fats);
        sprintf(g_mfd[0][3], "%d FAT %d LN", fats, lines);
    } else
        sprintf(g_mfd[0][3], "T-%lu", (FLIGHT_TICKS - (clock240() - g_t0) + 239) / 240);
    g_st.mfd_text[0][0] = g_mfd[0][0];
    g_st.mfd_text[0][1] = g_mfd[0][1];
    g_st.mfd_text[0][2] = g_mfd[0][2];
    g_st.mfd_text[0][3] = g_mfd[0][3];
    g_st.mfd_text[1][0] = 0;
    g_st.mfd_draw = mfd_draw;
}

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
static unsigned long g_t_far_ships, g_t_draw, g_t_lz_step, g_t_wait;

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
    spawn(1, enemy_type, 1, 4, -256L * 380, 256L * 60, 256L * 1500, 300);
    spawn(2, g_camp.player_ship, 0, 4, 256L * 520, -256L * 90, 256L * 2100, 700);
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
        face(RV_SCRATCH_SLOT, (int)(vrnd() % 1024), (int)(vrnd() % 300) - 150);
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
    _fheapshrink();                     /* (what the checks took back to DOS) */
    g_lz_cap = bolts_init(3200, 12);    /* 12 KB kept for what the flight takes later */
    g_lz_life = g_lz_cap ? (unsigned char __far *)_fmalloc(g_lz_cap) : 0;
    if (!g_lz_life)
        g_lz_cap = 0;
    if (g_near_bolts > g_lz_cap)
        g_near_bolts = g_lz_cap;
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

/* ms a frame x 100 from 240 Hz ticks over `frames` */
static unsigned long per_frame(unsigned long ticks, unsigned frames)
{
    return ticks * 100000UL / 240 / (frames ? frames : 1);
}

static void lasers_report(void)
{
    unsigned long t[4], f = frameCounter;
    int lines, fats;
    if (!g_lz_cap)
        return;
    bolts_ticks(t);
    bolts_on_screen(&lines, &fats);
    rv_log("lasers: %ld far bolts and %d near held %d fps (the settled half's average; %d lines and %d fat on screen at "
           "the end), beside %d big ships drawn exactly and %d far ones",
           g_lz_frames ? g_lz_sum / g_lz_frames : 0L, g_near_bolts, FPS_WANT, lines, fats, g_big_ships, g_far_ships);
    rv_log("lasers: ms a frame: bolts placed %lu.%02lu, far bolts drawn %lu.%02lu, near %lu.%02lu, flown and fired "
           "%lu.%02lu; big ships %lu.%02lu, far ships %lu.%02lu (the slots' from the sets %lu.%02lu); stars %lu.%02lu; "
           "the whole picture %lu.%02lu",
           per_frame(t[0], f) / 100, per_frame(t[0], f) % 100, per_frame(t[1], f) / 100, per_frame(t[1], f) % 100,
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
        rv_log("lasers: the cockpit and the flip %lu.%02lu ms a frame; the rest of the frame (simulation, plan, stars "
               "placed) %lu.%02lu; frames %lu.%02lu ms on average; the world drawn in %lu.%02lu bands a frame",
               per_frame(g_t_draw - world, f) / 100, per_frame(g_t_draw - world, f) % 100,
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

/* --- the loop's calls (rvflight.inc) --- */

void __far __cdecl flight_startup(void)
{
    const RvMission __far *m = g_m;
    int i, k, n = 1, enemy_type = -1;
    unsigned long now;
    memset(maybeEntityType, 0, RV_SLOTS * 2);
    if (g_lasers)
        rv_log("lasers: DOS memory free as the flight starts: %lu KB", bolts_dos_free() / 1024);
    missionStatus = 0;
    frameCounter = 0;
    rvCockpitDamage = 0;
    rvBenchCount = 0;
    rvFps = 0;
    playerCanBeDamaged = 1;
    g_my_type = ship_type(g_camp.player_ship);
    cockpit_load(g_camp.player_ship);
    spawn(0, g_camp.player_ship, 0, 1, 0, 0, 0, 0);
    /* the mission's ships round the player: the wingman alongside, the rest
     * a few hundred kilometres out ahead, flying their circles */
    for (i = 0; i < m->nship && n <= RV_MAX_SHIP_SLOT; i++) {
        const RvShip __far *sh = &m->ship[i];
        int type = sh->type == RV_SHIP_PLAYER || sh->type == RV_SHIP_WINGMAN ? g_camp.player_ship : sh->type;
        if (sh->type == RV_SHIP_PLAYER)
            continue;
        if (sh->side == 1 && enemy_type < 0)
            enemy_type = type;
        for (k = 0; k < sh->count && n <= RV_MAX_SHIP_SLOT; k++, n++) {
            int a = (int)(vrnd() % 342) - 171;             /* within 60 degrees of the nose */
            /* (sin x distance would overflow a long: the distance goes in as km) */
            long dist = 256L * (4000 + (long)(vrnd() % 8000)), sx = (lf_sin(a) * (dist >> 8)) >> 6, sz = (lf_cos(a) * (dist >> 8)) >> 6;
            spawn(n, type, sh->side, 0, sx, (long)(vrnd() % 20000) * 20 - 200000L, sz, sh->type == RV_SHIP_WINGMAN ? 0 : a + 512);
            rv_log("flight: slot %d type %02X spawned at bearing %d, %ld km: %ld,%ld", n, type, a, dist / 256, sx, sz);
        }
    }
    if (enemy_type < 0)
        enemy_type = 0x08;
    if (g_zoom) {
        /* a test aid: one Gawain dead ahead, a little off the nose, rolling, swinging in and out */
        n = 1;
        memset(maybeEntityType + 1, 0, (RV_SLOTS - 1) * 2);
        spawn(1, 0x02, 0, 4, 0, 0, 256L * 4000, 512 + 100);
        setSpeed[1] = speedLo[1] = 0;
        setSpeed[0] = speedLo[0] = 0;
        ships_load(0x02);
        n = 2;
    }
    if (g_lasers) {
        lasers_setup(enemy_type);
        n = 3;
    }
    if (g_capital && n <= RV_MAX_SHIP_SLOT) {
        /* a test aid: the Vigilance off to the left, ahead, standing still */
        spawn(n, 0x20, 0, 4, -256L * 3000, 0, 256L * 5000, 256);
        maybeEntityType[n] = 0xD;
        setSpeed[n] = speedLo[n] = 0;
        ships_load(0x20);
        n++;
    }
    if (g_lasers)
        rv_log("lasers: DOS memory free before the ship sets: %lu KB", bolts_dos_free() / 1024);
    g_set_enemy = ships_load(enemy_type);
    g_set_wing = ships_load(g_camp.player_ship);
    for (i = 1; i < n; i++)
        ships_load(gInstanceShipTypes[i]);
    if (g_lasers)
        lasers_sets();
    {
        /* the sets' ladders and rolls, within what the mission level has left (1 MB kept back) */
        unsigned long free_kb = xms_free_kb(), taken, t0 = clock240();
        taken = ships_load_blocks(free_kb > 1024 ? (free_kb - 1024) * 1024UL : 0);
        rv_log("ships: %lu KB of blocks loaded in %lu ms, %lu KB of XMS left", taken / 1024, (clock240() - t0) * 1000 / 240,
               xms_free_kb());
    }
    g_cam_cy = cockpit_record() ? (cockpit_record()->open_y0 + cockpit_record()->open_y1) / 2 : SCR_H / 2;
    ships_set_camera(SCR_W / 2, g_cam_cy, 400);
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
        /* the kernels checked, then the three draw paths timed on a full-size view (streamed in for it) */
        {
            long n = ships_selftest();
            rv_log("lightfield: asm kernels %s the C ones (%ld pixels compared)", n < 0 ? "DISAGREE WITH" : "agree with", n);
        }
        for (i = 1; i < set->nviews; i++)
            if ((long)set->view[i].w * set->view[i].h > (long)set->view[tv].w * set->view[tv].h)
                tv = i;
        ships_time_paths(g_set_enemy, tv, paths);
        rv_log("lightfield: view %d (%dx%d): copy %ld px/ms, scaled to 3/4 %ld, turned 45 degrees at 3/4 %ld, copy upside down %ld, copy mirrored %ld",
               tv, set->view[tv].w, set->view[tv].h, paths[0], paths[1], paths[2], paths[3], paths[4]);
    }
    if (g_lasers)
        lasers_pool();
    g_nbench = 0;
    g_bench_sum = 0;
    g_bench_frames = 0;
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
    vid_fade(0);
    vid_dirty(0, SCR_H);
    cockpit_dirty(&g_st);
    scene_draw();
    vid_fade(64);
    rv_log("flight: %.16Fs, %d ships, benchmark view %d (%dx%d)", (char __far *)m->id, n - 1, g_bench_view, g_bench_w,
           g_bench_h);
    cockpit_palette();                  /* only now: the deck kept its own palette while all that loaded (seconds in the
                                           browser: the user saw it go "all crazy" in the flight's palette) */
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
    ships_frame();
    sim_step();
    stars_place();
    plan_draws();
    if (g_lasers) {
        if (g_bolt_demo)
            demo_step();
        else
            lasers_step();
        lasers_place();
    }
    /* the rate over the last FPS_WINDOW frames */
    dt = now - g_frame_t[k];
    g_frame_t[k] = now;
    g_fps10 = dt ? (int)(2400UL * FPS_WINDOW / dt) : 999;
    rvFps = (unsigned short)g_fps10;
    bench_step();
    /* the benchmark's scenery: a damage layer more every three seconds (not in the laser benchmark: they hid its readout) */
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
    if (now - g_t0 >= FLIGHT_TICKS)
        missionStatus = 1;              /* landed */
    /* the frame rate capped at FPS_WANT: the simulation is per frame (a kps is 12.8 units a frame), so
     * without the cap a bare flight ran three times too fast natively (the adapting benchmark had been
     * holding it at 30 by loading it; off by default now) */
    if (g_bench_wanted >= 0 && g_lasers >= 0) {   /* (the adapting benchmarks measure the uncapped rate) */
        unsigned long t0 = clock240();
        while (clock240() - now < 240 / FPS_WANT)
            rv_halt();
        g_t_wait += clock240() - t0;
    }
}

void __far __cdecl flight_music(void)
{
    music_poll();
}

void __far __cdecl flight_autopilot(void)
{
    rv_log("flight: autopilot (nothing to fly to yet)");
}

void __far __cdecl autoAnimation_c(unsigned short camShipType, unsigned short camMode, unsigned short duration)
{
    rv_log("flight: autoAnimation(%u, %u, %u)", camShipType, camMode, duration);
}

/* --- from the game --- */

int flight_fly(const RvMission __far *m)
{
    unsigned short status;
    int w, h, n;
    g_m = m;
    uiMode(0);
    status = rv_flight_run();           /* (its loop begins with flight_startup: the loading, then the palette) */
    n = flight_bench_result(&w, &h);
    rv_log("flight: over with status %u after %u frames; benchmark %d sprites of %dx%d a frame at %d fps (%ld pixels a second)",
           status, frameCounter, n, w, h, FPS_WANT, (long)n * w * h * FPS_WANT);
    rv_log("flight: the stars took %lu.%02lu ms a frame to place and %lu.%02lu to draw",
           g_star_place_ticks * 1000 / 240 / (frameCounter ? frameCounter : 1),
           (g_star_place_ticks * 100000 / 240 / (frameCounter ? frameCounter : 1)) % 100,
           g_star_draw_ticks * 1000 / 240 / (frameCounter ? frameCounter : 1),
           (g_star_draw_ticks * 100000 / 240 / (frameCounter ? frameCounter : 1)) % 100);
    if (g_lasers)
        lasers_report();
    g_star_place_ticks = g_star_draw_ticks = 0;
    g_scene.full = 0;
    g_scene.no_room = 0;
    g_scene.hide_pointer = 0;
    in_steer(1);
    ships_stream_log();
    ships_unload();
    cockpit_unload();
    rv_hangar();
    uiMode(1);
    return status;
}
