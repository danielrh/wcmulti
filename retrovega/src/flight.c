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
#include "rv.h"
#include "rvhooks.h"
#include "room.h"
#include "campaign.h"
#include "music.h"
#include "ships.h"
#include "cockpit.h"
#include "flight.h"

int g_flight_seconds = 20;              /* the benchmark flight's length (RV.EXE /flight N) */
int g_bench_wanted = -1;                /* RV.EXE /bench N: that many copies, no adapting; 0 none; -1 adapt */
int g_record = 0;                       /* RV.EXE /record N: a screenshot (Rnnnnn.BMP) every N frames of flight */
int g_capital = 0;                      /* RV.EXE /capital: the carrier in the flight (a test aid) */
int g_zoom = 0;                         /* RV.EXE /zoom: one ship ahead, rolling, swinging in and out (a test aid) */
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
#define STARS 160

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
    if (g_zoom && s == 1) {
        /* the zoom scene's ship, 1,200 .. 6,000 km away on a twelve-second swing: /zoom 1 rolls it
         * at 60 degrees a second (the lightfield's view switches as the camera walks round its
         * nose); /zoom 2 yaws it slowly instead, for the player to roll round */
        long d = 256L * 3600 + ((lf_cos((int)(frameCounter * 1024L / 240 % 1024)) * 2400L) >> 14) * 256;
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

static struct { short x, y; } __far g_star[STARS];
static int g_star_x0, g_star_y0, g_star_w, g_star_h;

static void stars_init(void)
{
    const RvCockpit __far *c = cockpit_record();
    int i;
    g_star_x0 = 0;
    g_star_w = SCR_W;
    g_star_y0 = c ? c->open_y0 : 0;
    g_star_h = c ? c->open_y1 - c->open_y0 : SCR_H;
    for (i = 0; i < STARS; i++) {
        g_star[i].x = (short)(vrnd() % g_star_w);
        g_star[i].y = (short)(vrnd() % g_star_h);
    }
}

/* the stars slide against the turn, wrapping round the opening */
static void stars_move(void)
{
    int i, dx = -g_steer_x * 2, dy = -g_steer_y * 2;
    if (!dx && !dy)
        return;
    for (i = 0; i < STARS; i++) {
        int x = (g_star[i].x + dx) % g_star_w, y = (g_star[i].y + dy) % g_star_h;
        g_star[i].x = (short)(x < 0 ? x + g_star_w : x);
        g_star[i].y = (short)(y < 0 ? y + g_star_h : y);
    }
}

static void draw_stars(int y0, int y1)
{
    int i, ey0, rows;
    const short __far *ext = cockpit_extents(&ey0, &rows);
    for (i = 0; i < STARS; i++) {
        int x = g_star_x0 + g_star[i].x, y = g_star_y0 + g_star[i].y, k = y - ey0;
        if (y < y0 || y >= y1)
            continue;
        if (ext && (k < 0 || k >= rows || x < ext[2 * k] || x >= ext[2 * k + 1]))
            continue;
        vid_row(y)[x] = (i & 3) ? UI_GREY4 : UI_GREY6;
    }
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
    }
    /* back to front */
    for (i = 1; i < g_nplan; i++)
        for (k = i; k > 0 && g_plan[k].depth > g_plan[k - 1].depth; k--) {
            struct { unsigned char slot, exact, set; long depth; int scale; } t = g_plan[k];
            g_plan[k] = g_plan[k - 1];
            g_plan[k - 1] = t;
        }
}

static void draw_world(int y0, int y1)
{
    int ey0, rows, i;
    const short __far *ext = cockpit_extents(&ey0, &rows);
    const ShipSet __far *bench = g_set_enemy >= 0 ? ships_set(g_set_enemy) : 0;
    LfBasis cam, ship;
    int path;
    draw_stars(y0, y1);
    /* the ships, as the frame's plan has them: the nearest exactly, the rest from the sets */
    basis_of(0, &cam);
    for (i = 0; i < g_nplan; i++) {
        LfVec rel;
        int s = g_plan[i].slot, set = g_plan[i].set;
        rel_of(s, &rel);
        basis_of(s, &ship);
        if (g_plan[i].exact)
            path = 8 + g_plan[i].exact * ships_draw_exact(set, &rel, &cam, &ship, g_plan[i].scale, g_plan[i].exact, y0, y1,
                                                          0, SCR_W, ext, ey0, rows);
        else
            path = ships_draw(set, &rel, &cam, &ship, g_plan[i].scale, y0, y1, 0, SCR_W, ext, ey0, rows);
        if (y0 <= ey0 && (frameCounter % 100 == 1 || (g_record && frameCounter % g_record == 0))) {
            /* now and then, where each ship was put (tests/ships.txt reads it) */
            LfPlace p;
            lf_place(&rel, &cam, &ship, SCR_W / 2, (ey0 + rows / 2), 400, &p);
            rv_log("lightfield: frame %u slot %d type %02X at %ld,%ld,%ld: view %d roll %d scale %d at %d,%d path %d",
                   frameCounter, s, gInstanceShipTypes[s], rel.x, rel.y, rel.z, p.view, p.roll, g_plan[i].scale, p.sx, p.sy, path);
        }
    }
    /* the benchmark's copies */
    if (bench)
        for (i = 0; i < g_nbench; i++)
            ships_blit(bench, g_bench_view, g_bench[i].x, g_bench[i].y, y0, y1, 0, SCR_W, ext, ey0, rows);
}

static int g_ptr_y0, g_ptr_y1;

static void __far __cdecl draw_pointer_body(void)
{
    /* the steering cross where the pointer is, in mouse mode */
    int x = g_scene.px, y = g_scene.py;
    if (g_keyboard_steer)
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
    ships_blit(set, 10, r->x + r->w / 2, r->y + r->h / 2, y0 > r->y ? y0 : r->y,
               y1 < r->y + r->h ? y1 : r->y + r->h, r->x, r->x + r->w, 0, 0, 0);
}

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
    sprintf(g_mfd[0][0], "SPRITES %d", g_nbench);
    sprintf(g_mfd[0][1], "%d.%d FPS", g_fps10 / 10, g_fps10 % 10);
    sprintf(g_mfd[0][2], "%dx%d VIEW", g_bench_w, g_bench_h);
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

/* --- the loop's calls (rvflight.inc) --- */

void __far __cdecl flight_startup(void)
{
    const RvMission __far *m = g_m;
    int i, k, n = 1, enemy_type = -1;
    unsigned long now;
    memset(maybeEntityType, 0, RV_SLOTS * 2);
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
    if (g_capital && n <= RV_MAX_SHIP_SLOT) {
        /* a test aid: the Vigilance off to the left, ahead, standing still */
        spawn(n, 0x20, 0, 4, -256L * 3000, 0, 256L * 5000, 256);
        maybeEntityType[n] = 0xD;
        setSpeed[n] = speedLo[n] = 0;
        ships_load(0x20);
        n++;
    }
    g_set_enemy = ships_load(enemy_type);
    g_set_wing = ships_load(g_camp.player_ship);
    for (i = 1; i < n; i++)
        ships_load(gInstanceShipTypes[i]);
    {
        /* the sets' ladders and rolls, within what the mission level has left (1 MB kept back) */
        unsigned long free_kb = xms_free_kb(), taken;
        taken = ships_load_blocks(free_kb > 1024 ? (free_kb - 1024) * 1024UL : 0);
        rv_log("ships: %lu KB of blocks loaded, %lu KB of XMS left", taken / 1024, xms_free_kb());
    }
    ships_set_camera(SCR_W / 2, cockpit_record() ? (cockpit_record()->open_y0 + cockpit_record()->open_y1) / 2 : SCR_H / 2,
                     400);
    if (g_set_enemy >= 0) {
        /* the benchmark's sprite: the biggest of the type's band views */
        const ShipSet __far *set = ships_set(g_set_enemy);
        long best = 0, paths[5];
        for (i = 1; i < 36; i++) {
            long area = (long)set->view[i].w * set->view[i].h;
            if (area > best) {
                best = area;
                g_bench_view = i;
            }
        }
        g_bench_w = set->view[g_bench_view].w;
        g_bench_h = set->view[g_bench_view].h;
        /* the kernels checked, then the three draw paths timed on it */
        {
            long n = ships_selftest();
            rv_log("lightfield: asm kernels %s the C ones (%ld pixels compared)", n < 0 ? "DISAGREE WITH" : "agree with", n);
        }
        ships_time_paths(g_set_enemy, g_bench_view, paths);
        rv_log("lightfield: view %d (%dx%d): copy %ld px/ms, scaled to 3/4 %ld, turned 45 degrees at 3/4 %ld, copy upside down %ld, copy mirrored %ld",
               g_bench_view, g_bench_w, g_bench_h, paths[0], paths[1], paths[2], paths[3], paths[4]);
    }
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
    if (in.key == KEY_ESC)
        return 5;                       /* exit: the flight is over */
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
    sim_step();
    stars_move();
    plan_draws();
    /* the rate over the last FPS_WINDOW frames */
    dt = now - g_frame_t[k];
    g_frame_t[k] = now;
    g_fps10 = dt ? (int)(2400UL * FPS_WINDOW / dt) : 999;
    rvFps = (unsigned short)g_fps10;
    bench_step();
    /* the benchmark's scenery: a damage layer more every three seconds */
    if (now >= g_damage_at) {
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
    scene_draw();
    if (g_record && frameCounter % g_record == 0) {
        char name[12];
        sprintf(name, "R%05u", frameCounter / g_record);
        shot_save(name);
    }
    rvFrameDone();
    if (now - g_t0 >= FLIGHT_TICKS)
        missionStatus = 1;              /* landed */
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
    status = rv_flight_run();
    n = flight_bench_result(&w, &h);
    rv_log("flight: over with status %u after %u frames; benchmark %d sprites of %dx%d a frame at %d fps (%ld pixels a second)",
           status, frameCounter, n, w, h, FPS_WANT, (long)n * w * h * FPS_WANT);
    g_scene.full = 0;
    g_scene.no_room = 0;
    g_scene.hide_pointer = 0;
    in_steer(1);
    ships_unload();
    cockpit_unload();
    rv_hangar();
    uiMode(1);
    return status;
}
