/* tests/play/playtest.c: the play tests.  What src/logic.c has to do for RetroVega to fly and fight like the space
 * combat games of the early nineties, one check at a time, in the order docs/gameplay.md builds it (M1 flight, M2
 * guns, M3 damage, M4 deaths, M5 the AI, M6 collisions, M7 missiles, M8 the mission, M9 the HUD).  They fail until
 * the logic is written and pass as each piece is; they are yours to change where your design differs (the
 * tolerances are at the top, and each test says what it expects).
 *
 * The same file is built twice:
 *   - on the host into build/host32/playtest (host/build.sh, with the rvsim harness and PLAYTEST_MAIN), where gdb,
 *     rr, valgrind and the sanitizers work and the multiplayer layer's rules are checked at every thunk (host.c);
 *   - into RVP.EXE (build.sh: RV.EXE with the tests), run in DOSBox as RVP.EXE /playtest [IDS], its lines in RVLOG.TXT.
 * Each test ends with a hash of the world: the slot arrays, the mission tables and the shots.  The DOS build must
 * reach the host's world test by test (tests/play/playtest.sh --dos compares them): a difference is a computation
 * that comes out otherwise with DOS's 16-bit int (docs/logic.md, "Sized types").
 *
 * A test is a scene: the flight begun as flight.c begins it (camp_load_units, the scalars, logic_start), ships put
 * where the test wants them (as the layer puts ships in, by ship_spawn), the controls set, then steps of
 * logic_frame, and the world looked at.  Only the logic's public face is used: the thunks, logic_start,
 * logic_frame, logic_end, logic_hud, g_pilot, g_sys and the fixed block.
 *
 * Written for both compilers: C89 (declarations first), the sized types, and nothing in DGROUP (RVP.EXE, RV.EXE and the tests, has
 * 3 KB of it free): every string a `static const char __far`, every table and variable __far.  The messages'
 * numbers are int32_t, printed by ln_f's %d. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "panel.h"
#include "collide.h"
#include "flight.h"
#include "world.h"
#include "shots.h"
#include "hud.h"
#include "logic.h"
#ifdef RV_HOST
#include "host.h"
#endif

int16_t playtest_run(const char *only);

/* the tolerances (yours to change) */
#define TURN_SLACK 30                   /* percent: a turn may be this far off its rate, dps / KPS_HZ degrees a step */
#define SPEED_SLACK 3                   /* percent: a step's move this far off KPS_STEP(the speed) */
#define AIM_COS 222                     /* the AI fires with its target within 30 degrees of its nose (cos x 256) */
#define AIM_SHARE 70                    /* percent of its shots */

/* the ships the tests fly (data/ships.csv) */
#define PT_PLAYER 0x00                  /* the Schroedinger: two lasers, two dumbfires */
#define PT_HOMER 0x02                   /* the Gawain: two particle cannons, two friend-or-foe missiles */
#define PT_ENEMY 0x08                   /* the Nicander */
#define PT_WEAK 0x0B                    /* the Agis */
#define PT_CAPSHIP 0x20                 /* the Vigilance */
#define SEC LOGIC_HZ                    /* steps a second */

typedef void (*PtFn)(void);
typedef struct {
    const char __far *name;             /* "M1.1 name (the functions): what it expects" */
    PtFn fn;
    uint8_t host_only;                  /* asks the harness what the DOS build cannot tell (the thunks' calls, the
                                           screen) */
} PtTest;

typedef struct { uint16_t sh[2], ar[4], hull, state; } PtHurt;

/* --- the run's state --- */

static RvMission __far g_tm;            /* the test's mission */
static int16_t __far g_index;           /* the test running */
static int16_t __far g_failed;          /* its failures */
#ifdef RV_HOST
static uint32_t __far g_rules0;         /* host_rules_broken as it began */
#endif
static char __far g_line[200];
static int16_t __far g_len;
static const char __far *__far g_name;  /* its name */
static int16_t __far g_steps;           /* "steps" in the list: a line a step, the world's parts hashed */

static const char __far s_pre[] = "playtest: ";

/* --- the output: a line at a time, the numbers by hand (the same on both compilers) --- */

static void ln_c(char c)
{
    if (g_len < (int16_t)sizeof g_line - 1)
        g_line[g_len++] = c;
}

static void ln_s(const char __far *s)
{
    while (*s)
        ln_c(*s++);
}

static void ln_d(int32_t v)
{
    char d[12];
    int16_t n = 0;
    uint32_t u = v < 0 ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
    if (v < 0)
        ln_c('-');
    do {
        d[n++] = (char)('0' + (int16_t)(u % 10));
        u /= 10;
    } while (u);
    while (n)
        ln_c(d[--n]);
}

static void ln_x(uint32_t v)
{
    int16_t i, k;
    for (i = 28; i >= 0; i -= 4) {
        k = (int16_t)((v >> i) & 15);
        ln_c((char)(k < 10 ? '0' + k : 'A' + k - 10));
    }
}

/* a message: %d takes the next of a, b, c */
static void ln_f(const char __far *f, int32_t a, int32_t b, int32_t c)
{
    int32_t v[3];
    int16_t k = 0;
    v[0] = a;
    v[1] = b;
    v[2] = c;
    while (*f)
        if (f[0] == '%' && f[1] == 'd' && k < 3) {
            ln_d(v[k++]);
            f += 2;
        } else {
            ln_c(*f++);
        }
}

/* the current test's id: its name up to the space */
static void ln_id(void)
{
    const char __far *n = g_name;
    while (*n && *n != ' ')
        ln_c(*n++);
}

static void ln_end(void)
{
    char buf[200];
#ifndef RV_HOST
    static const char __far s_fmt[] = "%s";
#endif
    g_line[g_len] = 0;
    _fstrcpy(buf, g_line);
    g_len = 0;
#ifdef RV_HOST
    puts(buf);
    fflush(stdout);
#else
    rv_logf(s_fmt, buf);
#endif
}

static const char __far s_indent[] = "     ", __far s_colon[] = ": ";

/* a check failed: the first three a test are told */
static void pt_fail(const char __far *f, int32_t a, int32_t b, int32_t c)
{
    if (++g_failed > 3)
        return;
    ln_s(s_pre);
    ln_s(s_indent);
    ln_id();
    ln_s(s_colon);
    ln_f(f, a, b, c);
    ln_end();
}

#define EXPECT(c, f, a, b, d) do { if (!(c)) pt_fail(f, (int32_t)(a), (int32_t)(b), (int32_t)(d)); } while (0)
#define REQUIRE(c, f, a, b, d) do { if (!(c)) { pt_fail(f, (int32_t)(a), (int32_t)(b), (int32_t)(d)); return; } } while (0)

/* --- numbers --- */

static int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

/* |v| of any vector (halved until its squares fit) */
static int32_t pt_len(int32_t x, int32_t y, int32_t z)
{
    int32_t k = 1;
    while (abs32(x) > 30000 || abs32(y) > 30000 || abs32(z) > 30000) {
        x /= 2;
        y /= 2;
        z /= 2;
        k *= 2;
    }
    return (int32_t)lf_sqrt((uint32_t)(x * x) + (uint32_t)(y * y) + (uint32_t)(z * z)) * k;
}

/* km between two slots */
static int32_t pt_km(int16_t a, int16_t b)
{
    return pt_len((gPositionVector[b].x - gPositionVector[a].x) / 256, (gPositionVector[b].y - gPositionVector[a].y) / 256,
                  (gPositionVector[b].z - gPositionVector[a].z) / 256);
}

/* km from a slot to a place (km) */
static int32_t pt_km_to(int16_t a, int32_t x, int32_t y, int32_t z)
{
    return pt_len(x - gPositionVector[a].x / 256, y - gPositionVector[a].y / 256, z - gPositionVector[a].z / 256);
}

/* how straight s's nose points at b: the cosine x 256 */
static int32_t pt_aim(int16_t s, int16_t b)
{
    RvVec3 d;
    int32_t len;
    d.x = (gPositionVector[b].x - gPositionVector[s].x) / 256;
    d.y = (gPositionVector[b].y - gPositionVector[s].y) / 256;
    d.z = (gPositionVector[b].z - gPositionVector[s].z) / 256;
    while (abs32(d.x) > 30000 || abs32(d.y) > 30000 || abs32(d.z) > 30000) {
        d.x /= 2;
        d.y /= 2;
        d.z /= 2;
    }
    len = pt_len(d.x, d.y, d.z);
    return len ? vec_dot(&d, &gOrientationFrontVector[s]) / len : 256;
}

/* v's angle in the plane of ax (0) and ay (a quarter turn): TURN a whole turn, -512 .. 511 */
static int16_t pt_angle(const RvVec3 *v, const RvVec3 *ax, const RvVec3 *ay)
{
    int16_t t = lf_atan2(vec_dot(v, ay) / 256, vec_dot(v, ax) / 256);
    return t >= 512 ? t - 1024 : t;
}

/* a turn's expected size in n steps at dps and a steer of 9ths: TURN a whole turn */
static int32_t pt_turn_of(int16_t n, int16_t dps, int16_t steer)
{
    return (int32_t)n * dps * TURN * steer / (INT32_C(360) * KPS_HZ * 9);
}

static int16_t pt_square(int16_t s)
{
    const RvVec3 *f = &gOrientationFrontVector[s], *r = &gOrientationRightVector[s], *u = &gOrientationUpVector[s];
    int32_t lf = vec_dot(f, f) / 256, lr = vec_dot(r, r) / 256, lu = vec_dot(u, u) / 256;
    if (lf < 207 || lf > 310 || lr < 207 || lr > 310 || lu < 207 || lu > 310)
        return 0;
    return abs32(vec_dot(f, r)) <= 26 * 256 && abs32(vec_dot(f, u)) <= 26 * 256 && abs32(vec_dot(r, u)) <= 26 * 256;
}

/* shields and armour left, less the hull's damage: falls with every hurt */
static int32_t pt_health(int16_t s)
{
    return (int32_t)SHIELD(s, 0) + SHIELD(s, 1) + ARMOR(s, 0) + ARMOR(s, 1) + ARMOR(s, 2) + ARMOR(s, 3)
           - damagePoints[s];
}

static void pt_hurt(int16_t s, PtHurt *h)
{
    int16_t k;
    for (k = 0; k < 2; k++)
        h->sh[k] = SHIELD(s, k);
    for (k = 0; k < 4; k++)
        h->ar[k] = ARMOR(s, k);
    h->hull = damagePoints[s];
    h->state = shipAiState[s];
}

/* the shots of a slot's in the list */
static int16_t pt_shots_of(int16_t owner)
{
    int16_t i, n = 0;
    for (i = 0; i < shots_count(); i++)
        if (shot(i)->owner == owner)
            n++;
    return n;
}

/* a gun record's weapon kind (RV_WK_*), 0xFF none */
static int16_t pt_kind(int16_t s, int16_t g)
{
    const RvWeapon __far *w = weapon(GUN_TYPE(s, g));
    return w ? w->kind : 0xFF;
}

static int16_t pt_is_gun(int16_t s, int16_t g)
{
    int16_t k = pt_kind(s, g);
    return k == RV_WK_GUN || k == RV_WK_TURRET;
}

static int16_t pt_guns(int16_t s)       /* its guns and turrets */
{
    int16_t g, n = 0;
    for (g = 0; g < GUN_COUNT(s); g++)
        if (pt_is_gun(s, g) && !(GUN_STATE(s, g) & GUN_DESTROYED))
            n++;
    return n;
}

static int16_t pt_launcher(int16_t s)   /* its first missile launcher, -1 none */
{
    int16_t g, k;
    for (g = 0; g < GUN_COUNT(s); g++)
        if ((k = pt_kind(s, g)) == RV_WK_MISSILE || k == RV_WK_TORPEDO || k == RV_WK_ROCKET)
            return g;
    return -1;
}

static int16_t pt_missile_of(int16_t parent)   /* a missile slot launched by parent, -1 none */
{
    int16_t s;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (maybeEntityType[s] == ET_MISSILE && parentShipForShip[s] == parent)
            return s;
    return -1;
}

static int16_t pt_count(int16_t side, int16_t type)   /* ships of a side (and a type: -1 any) in the slots */
{
    int16_t s, n = 0;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s) && commAnimInfo[s] == side && (type < 0 || gInstanceShipTypes[s] == type))
            n++;
    return n;
}

/* the calls of a thunk the harness counted (RV_FN_*, its argument arg was value; arg -1 all): -1 on DOS, which
 * cannot tell */
static int32_t pt_calls(uint16_t fn, int16_t arg, int16_t value)
{
#ifdef RV_HOST
    return (int32_t)host_calls(fn, arg, value);
#else
    (void)fn;
    (void)arg;
    (void)value;
    return -1;
#endif
}

/* --- the world --- */

/* the test's mission: the player alone at nav 0 (pt_nav and pt_mship add to it) */
static void pt_mission(void)
{
    static const char __far s_id[] = "PLAYTEST";
    _fmemset(&g_tm, 0, sizeof g_tm);
    _fstrcpy(g_tm.id, s_id);
    g_tm.nship = 1;
    g_tm.ship[0].type = RV_SHIP_PLAYER;
    g_tm.ship[0].count = 1;
    g_tm.ship[0].pilot = RV_PILOT_NONE;
}

static void pt_nav(uint8_t id, int16_t x, int16_t y, int16_t z)
{
    RvNav __far *n = &g_tm.nav[g_tm.nnav++];
    n->id = id;
    n->x = x;
    n->y = y;
    n->z = z;
}

static void pt_mship(uint8_t type, uint8_t side, uint8_t nav, uint8_t count, uint8_t pilot)
{
    RvShip __far *s = &g_tm.ship[g_tm.nship++];
    s->type = type;
    s->side = side;
    s->nav = nav;
    s->count = count;
    s->pilot = pilot;
}

/* the cockpit's systems as a launch leaves them (flight.c's systems_start) */
static void pt_systems(void)
{
    _fmemset(&g_sys, 0, sizeof g_sys);
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
}

/* the flight begins as flight.c begins it, with the test's mission (g_tm) and the player in a ship of `type`: the
 * world emptied, the mission's tables (camp_load_units), the scalars (victoryPoints 1: the stand-in's roll, which
 * the logic scores over), the systems, the shots' list, then logic_start */
static void pt_launch(int16_t type)
{
    int16_t s;
    _fmemset(&g_pilot, 0, sizeof g_pilot);
    memset(maybeEntityType, 0, (size_t)((uint8_t *)navPointTable - (uint8_t *)maybeEntityType));
    for (s = 0; s < RV_SLOTS; s++) {
        ship_clear(s);
        missileTarget[s] = 0xFF;
        rvTarget[s] = 0xFF;
    }
    shots_flight_end();
    collide_flight_end();
    shots_flight_start(0);
    g_camp.player_ship = (uint8_t)type;
    camp_load_units(&g_tm);
    missionStatus = 0;
    frameCounter = 0;
    playerCanBeDamaged = 1;
    rvCockpitDamage = 0;
    randomSeed = UINT32_C(0x52560000) + (uint32_t)g_index * 977;
    currentNavPoint = 0;
    victoryPoints = 1;
    playerTarget = 0xFFFF;
    landedOn = 0xFFFF;
    rvPlayerCount = 1;
    rvNetRole = 0;
    pt_systems();
#ifdef RV_HOST
    host_counts_reset();
    g_rules0 = host_rules_broken;
#endif
    logic_start(&g_tm);
}

/* a slot's place (km) and heading (TURN a whole turn of yaw), level, still */
static void pt_put(int16_t s, int32_t x, int32_t y, int32_t z, int16_t yaw)
{
    gPositionVector[s].x = x * KM;
    gPositionVector[s].y = y * KM;
    gPositionVector[s].z = z * KM;
    ship_face(s, yaw, 0);
    gVelocityVector[s].x = gVelocityVector[s].y = gVelocityVector[s].z = 0;
}

/* its throttle and speed (kps), and its velocity along its nose */
static void pt_speed(int16_t s, int32_t kps)
{
    setSpeed[s] = (uint32_t)kps;
    speedLo[s] = (uint32_t)kps;
    gVelocityVector[s].x = gOrientationFrontVector[s].x * KPS_STEP(kps) / 256;
    gVelocityVector[s].y = gOrientationFrontVector[s].y * KPS_STEP(kps) / 256;
    gVelocityVector[s].z = gOrientationFrontVector[s].z * KPS_STEP(kps) / 256;
}

/* a ship into a slot, as the layer puts one in (ship_spawn), still */
static void pt_ship(int16_t s, int16_t type, int16_t side, int16_t ctl, int32_t x, int32_t y, int32_t z, int16_t yaw)
{
    ship_spawn(s, type, side, ctl, x * KM, y * KM, z * KM, yaw);
    pt_speed(s, 0);
}

/* the player alone (`type`), at the origin facing +z, still; the slots the mission's start filled emptied (the
 * player put in slot 0 if logic_start did not) */
static void pt_alone(int16_t type)
{
    int16_t s;
    pt_mission();
    pt_launch(type);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        ship_clear(s);
    if (!slot_is_ship(0) || rvController[0] != CTL_PLAYER || gInstanceShipTypes[0] != type) {
        ship_spawn(0, type, SIDE_TERRAN, CTL_PLAYER, 0, 0, 0, 0);
        shipStateByte[0] = 8;
    }
    pt_put(0, 0, 0, 0, 0);
    pt_speed(0, 0);
}

/* the world after a step: every ship's frame square, the layer's rules kept */
static int16_t pt_world_ok(void)
{
    static const char __far e_frame[] = "slot %d's orientation is no longer 256 long and square: ship_orthonormalise "
        "every ship that turns, every step (step %d)";
#ifdef RV_HOST
    static const char __far e_rule[] = "step %d broke a rule the multiplayer layer needs: ";
#endif
    int16_t s;
    for (s = 0; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s) && !pt_square(s)) {
            pt_fail(e_frame, s, frameCounter, 0);
            return 0;
        }
#ifdef RV_HOST
    if (host_rules_broken != g_rules0) {
        g_rules0 = host_rules_broken;
        if (++g_failed <= 3) {
            ln_s(s_pre);
            ln_s(s_indent);
            ln_id();
            ln_s(s_colon);
            ln_f(e_rule, frameCounter, 0, 0);
            ln_s(host_last_rule);
            ln_end();
        }
        return 0;
    }
#endif
    return 1;
}

static void pt_parts(void);

/* a step of play, as the flight loop runs it */
static int16_t pt_step(void)
{
    frameCounter = (uint16_t)(frameCounter + 1);
#ifdef RV_HOST
    host_ticks = (uint32_t)frameCounter * 91 / 150;
#endif
    logic_frame();
    if (g_steps)
        pt_parts();
    return pt_world_ok();
}

static int16_t pt_steps(int16_t n)
{
    while (n-- > 0)
        if (!pt_step())
            return 0;
    return 1;
}

/* a place on a ship's skin: along its front (f), right (r) and up (u), halves of its half box */
static void pt_on(int16_t s, int16_t f, int16_t r, int16_t u, RvVec3 *w)
{
    int32_t h = collide_half(s) / 2;
    const RvVec3 *F = &gOrientationFrontVector[s], *R = &gOrientationRightVector[s], *U = &gOrientationUpVector[s];
    w->x = gPositionVector[s].x + (F->x * f + R->x * r + U->x * u) * (h / 256);
    w->y = gPositionVector[s].y + (F->y * f + R->y * r + U->y * u) * (h / 256);
    w->z = gPositionVector[s].z + (F->z * f + R->z * r + U->z * u) * (h / 256);
}

/* a hit of qty on slot s at that place */
static uint16_t pt_hit(uint16_t src, int16_t s, uint16_t qty, int16_t f, int16_t r, int16_t u)
{
    RvVec3 w;
    pt_on(s, f, r, u, &w);
    return do_damage(src, (uint16_t)s, qty, RV_WHERE(&w));
}

/* ======================================================================================================== */
/* M1: flight (step_minds for the player, step_motion, step_timers) */

/* M1.1 */
static void t_coast(void)
{
    static const char __far e_vel[] = "a step's move was %d,%d,%d positions off its gVelocityVector: position += "
        "velocity, every step (the layer reads velocity as the move a step)";
    static const char __far e_len[] = "it moves %d positions a step at %d kps: KPS_STEP(speed) is %d";
    static const char __far e_dir[] = "it moves %d/256 along its nose: it should fly where it points";
    RvVec3 p0, v;
    int32_t want, got;
    int16_t i;
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->cruise_kps);
    if (!pt_step())
        return;
    for (i = 0; i < 30; i++) {
        p0 = gPositionVector[0];
        if (!pt_step())
            return;
        v = gVelocityVector[0];
        REQUIRE(gPositionVector[0].x - p0.x == v.x && gPositionVector[0].y - p0.y == v.y
                && gPositionVector[0].z - p0.z == v.z, e_vel, gPositionVector[0].x - p0.x - v.x,
                gPositionVector[0].y - p0.y - v.y, gPositionVector[0].z - p0.z - v.z);
    }
    want = KPS_STEP(speedLo[0]);
    got = pt_len(v.x, v.y, v.z);
    REQUIRE(got * 100 >= want * (100 - SPEED_SLACK) && got * 100 <= want * (100 + SPEED_SLACK) && want > 0, e_len, got,
            (int32_t)speedLo[0], want);
    EXPECT(vec_dot(&v, &gOrientationFrontVector[0]) / got >= 253, e_dir, vec_dot(&v, &gOrientationFrontVector[0]) / got,
           0, 0);
}

/* M1.2 */
static void t_throttle(void)
{
    static const char __far e_back[] = "the speed fell (%d to %d kps) while the throttle asked for %d";
    static const char __far e_over[] = "the speed went past the throttle: %d kps for %d";
    static const char __far e_jump[] = "the speed jumped %d kps in a step: more than a second of its accel (%d kps/s)";
    static const char __far e_once[] = "full speed in a single step (%d kps): the speed should build at the type's accel";
    static const char __far e_slow[] = "after 10 s the speed is %d kps, the throttle %d: speedLo should reach setSpeed";
    static const char __far e_stop[] = "after 10 s at a throttle of 0 the speed is still %d kps";
    const RvShipType __far *t;
    int32_t prev = 0, sp;
    int16_t i;
    pt_alone(PT_PLAYER);
    t = slot_type(0);
    setSpeed[0] = t->max_kps;
    for (i = 0; i < 10 * SEC; i++) {
        if (!pt_step())
            return;
        sp = (int32_t)speedLo[0];
        REQUIRE(sp >= prev, e_back, prev, sp, (int32_t)setSpeed[0]);
        REQUIRE(sp <= (int32_t)setSpeed[0], e_over, sp, (int32_t)setSpeed[0], 0);
        REQUIRE(sp - prev <= t->accel, e_jump, sp - prev, t->accel, 0);
        REQUIRE(i > 0 || sp < (int32_t)t->max_kps, e_once, sp, 0, 0);
        prev = sp;
    }
    REQUIRE(prev == (int32_t)t->max_kps, e_slow, prev, (int32_t)t->max_kps, 0);
    setSpeed[0] = 0;
    for (i = 0; i < 10 * SEC; i++) {
        if (!pt_step())
            return;
        sp = (int32_t)speedLo[0];
        REQUIRE(sp <= prev, e_back, prev, sp, 0);
        prev = sp;
    }
    REQUIRE(prev == 0, e_stop, prev, 0, 0);
}

/* M1.3 */
static void t_top_speed(void)
{
    static const char __far e_fast[] = "%d kps without the afterburner: its type's top speed (max_kps) is %d";
    static const char __far e_slow[] = "only %d kps after 10 s of full throttle (its top speed %d)";
    const RvShipType __far *t;
    int16_t i;
    pt_alone(PT_PLAYER);
    t = slot_type(0);
    pt_speed(0, t->cruise_kps);
    setSpeed[0] = 5000;
    for (i = 0; i < 10 * SEC; i++) {
        if (!pt_step())
            return;
        REQUIRE((int32_t)speedLo[0] <= (int32_t)t->max_kps, e_fast, (int32_t)speedLo[0], t->max_kps, 0);
    }
    REQUIRE((int32_t)speedLo[0] >= (int32_t)t->max_kps - 1, e_slow, (int32_t)speedLo[0], t->max_kps, 0);
}

/* a turn of the player's, still, by the controls (x, y, roll) for n steps: the frame before it in f0, r0, u0 */
static int16_t pt_turn(int8_t x, int8_t y, int8_t roll, int16_t n, RvVec3 *f0, RvVec3 *r0, RvVec3 *u0)
{
    pt_alone(PT_PLAYER);
    *f0 = gOrientationFrontVector[0];
    *r0 = gOrientationRightVector[0];
    *u0 = gOrientationUpVector[0];
    g_pilot.steer_x = x;
    g_pilot.steer_y = y;
    g_pilot.roll = roll;
    return pt_steps(n);
}

static const char __far e_rate[] = "it turned %d (1024 a turn) in 10 steps: %d expected (its type's %d degrees a "
    "second, dps / KPS_HZ a step, x steer / 9)";

/* M1.4 */
static void t_yaw(void)
{
    static const char __far e_roll[] = "a yaw tipped its wings: up is %d/256 of what it was";
    RvVec3 f0, r0, u0;
    int16_t got;
    int32_t want;
    if (!pt_turn(9, 0, 0, 10, &f0, &r0, &u0))
        return;
    got = pt_angle(&gOrientationFrontVector[0], &f0, &r0);
    want = pt_turn_of(10, slot_type(0)->yaw_dps, 9);
    REQUIRE(got * INT32_C(100) >= want * (100 - TURN_SLACK) && got * INT32_C(100) <= want * (100 + TURN_SLACK), e_rate,
            got, want, slot_type(0)->yaw_dps);
    EXPECT(vec_dot(&gOrientationUpVector[0], &u0) / 256 >= 248, e_roll, vec_dot(&gOrientationUpVector[0], &u0) / 256, 0,
           0);
}

/* M1.5 */
static void t_pitch(void)
{
    static const char __far e_side[] = "a pitch swung its wings round: right is %d/256 of what it was";
    RvVec3 f0, r0, u0;
    int16_t got;
    int32_t want;
    if (!pt_turn(0, 9, 0, 10, &f0, &r0, &u0))
        return;
    got = pt_angle(&gOrientationFrontVector[0], &f0, &u0);
    want = pt_turn_of(10, slot_type(0)->pitch_dps, 9);
    REQUIRE(got * INT32_C(100) >= want * (100 - TURN_SLACK) && got * INT32_C(100) <= want * (100 + TURN_SLACK), e_rate,
            got, want, slot_type(0)->pitch_dps);
    EXPECT(vec_dot(&gOrientationRightVector[0], &r0) / 256 >= 248, e_side,
           vec_dot(&gOrientationRightVector[0], &r0) / 256, 0, 0);
}

/* M1.6 */
static void t_roll(void)
{
    static const char __far e_nose[] = "a roll moved its nose: front is %d/256 of what it was";
    RvVec3 f0, r0, u0;
    int16_t got;
    int32_t want;
    if (!pt_turn(0, 0, 9, 10, &f0, &r0, &u0))
        return;
    got = pt_angle(&gOrientationRightVector[0], &r0, &u0);
    want = pt_turn_of(10, slot_type(0)->roll_dps, 9);
    REQUIRE(got * INT32_C(100) >= want * (100 - TURN_SLACK) && got * INT32_C(100) <= want * (100 + TURN_SLACK), e_rate,
            got, want, slot_type(0)->roll_dps);
    EXPECT(vec_dot(&gOrientationFrontVector[0], &f0) / 256 >= 253, e_nose,
           vec_dot(&gOrientationFrontVector[0], &f0) / 256, 0, 0);
}

/* M1.7 */
static void t_steer_steps(void)
{
    static const char __far e_none[] = "a full steer turned it %d: nothing to compare";
    static const char __far e_ratio[] = "a steer of 3 turned it %d, a steer of 9 %d: a third as far expected (the "
        "pointer's steps, steer / 9)";
    RvVec3 f0, r0, u0;
    int16_t full, third;
    if (!pt_turn(9, 0, 0, 10, &f0, &r0, &u0))
        return;
    full = pt_angle(&gOrientationFrontVector[0], &f0, &r0);
    REQUIRE(full > 4, e_none, full, 0, 0);
    if (!pt_turn(3, 0, 0, 10, &f0, &r0, &u0))
        return;
    third = pt_angle(&gOrientationFrontVector[0], &f0, &r0);
    REQUIRE(third * INT32_C(100) >= full * INT32_C(20) && third * INT32_C(100) <= full * INT32_C(50), e_ratio, third,
            full, 0);
}

/* M1.8 */
static void t_aerobatics(void)
{
    static const char __far e_still[] = "20 s of flying at cruise moved it %d km";
    static const int8_t __far k_moves[24] = { 9, 0, 0, 0, 9, 0, 0, 0, 9, -9, 5, -9, 4, -9, 9, 9, 9, 9, -3, -3, 0, 0,
                                              -9, 5 };
    int16_t i, k;
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->cruise_kps);
    for (i = 0; i < 20 * SEC; i++) {
        k = (int16_t)(i / 25 % 8) * 3;
        g_pilot.steer_x = k_moves[k];
        g_pilot.steer_y = k_moves[k + 1];
        g_pilot.roll = k_moves[k + 2];
        if (!pt_step())
            return;
    }
    REQUIRE(pt_km_to(0, 0, 0, 0) >= 300, e_still, pt_km_to(0, 0, 0, 0), 0, 0);
}

/* M1.9 */
static void t_afterburner(void)
{
    static const char __far e_ab[] = "%d kps with the afterburner lit: more than its ab_kps, %d";
    static const char __far e_slow[] = "%d kps after 5 s of afterburner: no faster than its top speed (%d)";
    static const char __far e_back[] = "%d kps 20 s after the afterburner went out: back to the throttle (%d) expected";
    const RvShipType __far *t;
    int16_t i;
    pt_alone(PT_PLAYER);
    t = slot_type(0);
    pt_speed(0, t->max_kps);
    g_pilot.afterburner = g_sys.burning = 1;   /* (Tab held: flight.c lights both) */
    rvAfterburner[0] = 1;
    for (i = 0; i < 5 * SEC; i++) {
        if (!pt_step())
            return;
        REQUIRE((int32_t)speedLo[0] <= (int32_t)t->ab_kps, e_ab, (int32_t)speedLo[0], t->ab_kps, 0);
    }
    REQUIRE((int32_t)speedLo[0] > (int32_t)t->max_kps, e_slow, (int32_t)speedLo[0], t->max_kps, 0);
    g_pilot.afterburner = g_sys.burning = 0;
    rvAfterburner[0] = 0;
    if (!pt_steps(20 * SEC))
        return;
    REQUIRE((int32_t)speedLo[0] <= (int32_t)setSpeed[0], e_back, (int32_t)speedLo[0], (int32_t)setSpeed[0], 0);
}

/* M1.10 */
static void t_puppet(void)
{
    static const char __far e_move[] = "a puppet (CTL_PUPPET) is %d,%d,%d positions off where its velocity takes it: "
        "it only coasts, position += velocity (the layer flies it)";
    static const char __far e_turn[] = "a puppet's orientation changed (front.x %d to %d): the layer turns it, not the "
        "logic";
    static const char __far e_fire[] = "a puppet fired %d shots: it does nothing of its own";
    static const char __far e_think[] = "the AI thought for a puppet %d times (aiFighterThink): the layer flies it";
    RvVec3 p0, f0, v;
    int32_t calls;
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->cruise_kps);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_PUPPET, 1000, 0, 5000, TURN / 8);
    v.x = 100;
    v.y = -50;
    v.z = 2000;
    gVelocityVector[1] = v;
    p0 = gPositionVector[1];
    f0 = gOrientationFrontVector[1];
    if (!pt_steps(30))
        return;
    REQUIRE(gPositionVector[1].x == p0.x + 30 * v.x && gPositionVector[1].y == p0.y + 30 * v.y
            && gPositionVector[1].z == p0.z + 30 * v.z, e_move, gPositionVector[1].x - p0.x - 30 * v.x,
            gPositionVector[1].y - p0.y - 30 * v.y, gPositionVector[1].z - p0.z - 30 * v.z);
    EXPECT(abs32(gOrientationFrontVector[1].x - f0.x) <= 2 && abs32(gOrientationFrontVector[1].z - f0.z) <= 2, e_turn,
           f0.x, gOrientationFrontVector[1].x, 0);
    EXPECT(pt_shots_of(1) == 0, e_fire, pt_shots_of(1), 0, 0);
    calls = pt_calls(RV_FN_aiFighterThink, 0, 1);
    EXPECT(calls <= 0, e_think, calls, 0, 0);
}

/* M1.11 */
static void t_frozen(void)
{
    static const char __far e_ctl[] = "the player did not move either (%d km): nothing flies yet";
    static const char __far e_move[] = "a frozen ship (CTL_FROZEN) moved %d km: it does nothing";
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->cruise_kps);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 2000, 0, 8000, 0);
    pt_speed(1, 300);
    rvController[1] = CTL_FROZEN;
    if (!pt_steps(30))
        return;
    REQUIRE(pt_km_to(0, 0, 0, 0) > 10, e_ctl, pt_km_to(0, 0, 0, 0), 0, 0);
    REQUIRE(pt_km_to(1, 2000, 0, 8000) == 0, e_move, pt_km_to(1, 2000, 0, 8000), 0, 0);
}

/* ======================================================================================================== */
/* M2: guns (step_fire, fireGunFromShip, maybe_fire_all_guns, step_timers, step_shots) */

/* M2.1 */
static void t_fire_call(void)
{
    static const char __far e_none[] = "fireGunFromShip(0, 0) put %d shots in the world: one expected (shot_add)";
    static const char __far e_ret[] = "fireGunFromShip returned %d for a gun: 0xFFFF expected (a slot is a missile's)";
    static const char __far e_own[] = "the shot's owner is %d, its weapon %d: the ship (0) and its gun's (%d)";
    static const char __far e_far[] = "the shot starts %d km from the ship: within its size (%d km) expected";
    static const char __far e_speed[] = "the shot moves %d positions a step: the weapon's speed is %d, the ship's own "
        "%d";
    static const char __far e_way[] = "the shot flies %d/256 along the ship's nose";
    Shot __far *s;
    int16_t n0;
    uint16_t r;
    int32_t v, lo, ship;
    RvVec3 sv;
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->cruise_kps);
    if (!pt_step())
        return;
    n0 = shots_count();
    r = fireGunFromShip(0, 0);
    REQUIRE(shots_count() == n0 + 1, e_none, shots_count() - n0, 0, 0);
    EXPECT(r == 0xFFFF, e_ret, r, 0, 0);
    s = shot(n0);
    REQUIRE(s->owner == 0 && s->weapon == GUN_TYPE(0, 0), e_own, s->owner, s->weapon, GUN_TYPE(0, 0));
    REQUIRE(pt_len((s->x - gPositionVector[0].x) / 256, (s->y - gPositionVector[0].y) / 256,
                   (s->z - gPositionVector[0].z) / 256) <= 2 * collide_half(0) / 256, e_far,
            pt_len((s->x - gPositionVector[0].x) / 256, (s->y - gPositionVector[0].y) / 256,
                   (s->z - gPositionVector[0].z) / 256), 2 * collide_half(0) / 256, 0);
    sv.x = s->vx;
    sv.y = s->vy;
    sv.z = s->vz;
    v = pt_len(sv.x, sv.y, sv.z);
    lo = KPS_STEP(weapon(GUN_TYPE(0, 0))->speed_kps);
    ship = pt_len(gVelocityVector[0].x, gVelocityVector[0].y, gVelocityVector[0].z);
    REQUIRE(v * 100 >= lo * 97 && v * 100 <= (lo + ship) * 103 + 100, e_speed, v, lo, ship);
    REQUIRE(v > 0 && vec_dot(&sv, &gOrientationFrontVector[0]) / v >= 243, e_way,
            v ? vec_dot(&sv, &gOrientationFrontVector[0]) / v : 0, 0, 0);
}

/* M2.2 */
static void t_never_refuses(void)
{
    static const char __far e_refused[] = "fireGunFromShip refused a gun with no energy that was cooling: it fires "
        "whatever, the caller decides (the layer replays the call)";
    static const char __far e_broke[] = "firing marked the gun destroyed (GUN_STATE bit 7)";
    static const char __far e_wrap[] = "gun energy went below 0 to %d";
    int16_t n0;
    pt_alone(PT_PLAYER);
    gunEnergy[0] = 0;
    GUN_STATE(0, 0) = 50;
    n0 = shots_count();
    (void)fireGunFromShip(0, 0);
    REQUIRE(shots_count() == n0 + 1, e_refused, 0, 0, 0);
    EXPECT(!(GUN_STATE(0, 0) & GUN_DESTROYED), e_broke, 0, 0, 0);
    EXPECT(gunEnergy[0] <= 100, e_wrap, gunEnergy[0], 0, 0);
}

/* M2.3 */
static void t_cooldown(void)
{
    static const char __far e_none[] = "firing set no cooldown in GUN_STATE's bits 0-6 (the layer copies gun records "
        "whole: the cooldown lives there)";
    static const char __far e_long[] = "still cooling after %d steps: its refire is %d steps";
    static const char __far e_short[] = "cooled in %d steps: its refire is %d";
    static const char __far e_bit[] = "the timers cleared a destroyed gun's bit 7 (GUN_STATE %d)";
    int16_t n, refire;
    pt_alone(PT_PLAYER);
    refire = weapon(GUN_TYPE(0, 0))->refire;
    (void)fireGunFromShip(0, 0);
    REQUIRE(GUN_STATE(0, 0) & 0x7F, e_none, 0, 0, 0);
    GUN_STATE(0, 1) = GUN_DESTROYED | 5;
    for (n = 0; n < 4 * refire + 10 && (GUN_STATE(0, 0) & 0x7F); n++)
        if (!pt_step())
            return;
    REQUIRE(!(GUN_STATE(0, 0) & 0x7F), e_long, n, refire, 0);
    EXPECT(n <= 2 * refire + 1, e_long, n, refire, 0);
    EXPECT(n >= refire / 2, e_short, n, refire, 0);
    EXPECT(GUN_STATE(0, 1) & GUN_DESTROYED, e_bit, GUN_STATE(0, 1), 0, 0);
}

/* M2.4 */
static void t_energy(void)
{
    static const char __far e_free[] = "firing spent no gun energy (gunEnergy %d)";
    static const char __far e_fell[] = "gun energy fell (%d to %d) with no gun firing";
    static const char __far e_over[] = "gun energy %d: 100 at most";
    static const char __far e_slow[] = "gun energy back to only %d of 100 after 20 s";
    int16_t i;
    int32_t prev;
    pt_alone(PT_PLAYER);
    (void)fireGunFromShip(0, 0);
    REQUIRE(gunEnergy[0] < 100, e_free, gunEnergy[0], 0, 0);
    prev = gunEnergy[0];
    for (i = 0; i < 20 * SEC; i++) {
        if (!pt_step())
            return;
        REQUIRE((int32_t)gunEnergy[0] >= prev, e_fell, prev, gunEnergy[0], 0);
        REQUIRE(gunEnergy[0] <= 100, e_over, gunEnergy[0], 0, 0);
        prev = gunEnergy[0];
    }
    REQUIRE(gunEnergy[0] == 100, e_slow, gunEnergy[0], 0, 0);
}

/* M2.5 */
static void t_trigger(void)
{
    static const char __far e_first[] = "the trigger's first step fired %d shots: each of its %d guns should fire";
    static const char __far e_fast[] = "%d shots in 2 s: more than its guns' refire allows (%d)";
    static const char __far e_few[] = "only %d shots in 2 s of trigger";
    static const char __far e_after[] = "%d shots after the trigger was let go";
    static const char __far e_missile[] = "the trigger launched a missile: missiles are the missile key's";
    uint16_t a0;
    int16_t guns, refire;
    int32_t most;
    pt_alone(PT_PLAYER);
    guns = pt_guns(0);
    refire = weapon(GUN_TYPE(0, 0))->refire;
    g_sys.armed = 1;
    g_pilot.guns = 1;
    a0 = shots_added();
    if (!pt_step())
        return;
    REQUIRE((int16_t)(shots_added() - a0) >= guns, e_first, (uint16_t)(shots_added() - a0), guns, 0);
    if (!pt_steps(2 * SEC - 1))
        return;
    most = (int32_t)guns * (2 * SEC / refire + 1);
    REQUIRE((int32_t)(uint16_t)(shots_added() - a0) <= most, e_fast, (uint16_t)(shots_added() - a0), most, 0);
    REQUIRE((int16_t)(shots_added() - a0) >= 2 * guns, e_few, (uint16_t)(shots_added() - a0), 0, 0);
    EXPECT(pt_missile_of(0) < 0, e_missile, 0, 0, 0);
    g_pilot.guns = 0;
    a0 = shots_added();
    if (!pt_steps(10))
        return;
    EXPECT(shots_added() == a0, e_after, (uint16_t)(shots_added() - a0), 0, 0);
}

/* M2.6 */
static void t_safety(void)
{
    static const char __far e_none[] = "no shots with the master arm on either: the trigger fires nothing yet";
    static const char __far e_safe[] = "%d shots with the master arm off (g_sys.armed 0)";
    uint16_t a0, off;
    pt_alone(PT_PLAYER);
    g_sys.armed = 0;
    g_pilot.guns = 1;
    a0 = shots_added();
    if (!pt_steps(30))
        return;
    off = (uint16_t)(shots_added() - a0);
    g_sys.armed = 1;
    a0 = shots_added();
    if (!pt_steps(30))
        return;
    REQUIRE(shots_added() != a0, e_none, 0, 0, 0);
    REQUIRE(off == 0, e_safe, off, 0, 0);
}

/* M2.7 */
static void t_broken_gun(void)
{
    static const char __far e_two[] = "the test wants guns 0 and 1 on opposite sides (mounts %d and %d)";
    static const char __far e_none[] = "no shots from the guns left";
    static const char __far e_from[] = "a shot from the destroyed gun 0's side (%d km across; gun 1's mount is %d)";
    int16_t i, k, side;
    uint16_t a0;
    pt_alone(PT_PLAYER);
    REQUIRE(GUN_COUNT(0) >= 2 && pt_is_gun(0, 0) && pt_is_gun(0, 1)
            && (GUN_MOUNT_X(0, 0) < 0) != (GUN_MOUNT_X(0, 1) < 0), e_two, GUN_MOUNT_X(0, 0), GUN_MOUNT_X(0, 1), 0);
    side = GUN_MOUNT_X(0, 1) < 0 ? -1 : 1;
    GUN_STATE(0, 0) |= GUN_DESTROYED;
    g_sys.armed = 1;
    g_pilot.guns = 1;
    a0 = shots_added();
    for (i = 0; i < 30; i++) {
        if (!pt_step())
            return;
        for (k = 0; k < shots_count(); k++)
            REQUIRE(shot(k)->owner != 0 || (shot(k)->x - gPositionVector[0].x) * side >= 0, e_from,
                    (shot(k)->x - gPositionVector[0].x) / 256, GUN_MOUNT_X(0, 1), 0);
    }
    REQUIRE(shots_added() != a0, e_none, 0, 0, 0);
}

/* M2.8 */
static void t_all_guns(void)
{
    static const char __far e_n[] = "maybe_fire_all_guns fired %d shots: the ship has %d guns ready";
    static const char __far e_missile[] = "maybe_fire_all_guns launched a missile (launcher rounds %d to %d): guns only";
    static const char __far e_again[] = "fired %d more at once: the guns were still cooling";
    uint16_t a0;
    int16_t l, r0;
    pt_alone(PT_PLAYER);
    l = pt_launcher(0);
    r0 = l >= 0 ? GUN_STATE(0, l) : 0;
    a0 = shots_added();
    maybe_fire_all_guns(0);
    REQUIRE((int16_t)(shots_added() - a0) == pt_guns(0), e_n, (uint16_t)(shots_added() - a0), pt_guns(0), 0);
    REQUIRE(pt_missile_of(0) < 0 && (l < 0 || GUN_STATE(0, l) == r0), e_missile, r0, l >= 0 ? GUN_STATE(0, l) : 0, 0);
    a0 = shots_added();
    maybe_fire_all_guns(0);
    REQUIRE(shots_added() == a0, e_again, (uint16_t)(shots_added() - a0), 0, 0);
}

/* M2.9 */
static void t_range(void)
{
    static const char __far e_gone[] = "the shot was gone after one step";
    static const char __far e_left[] = "%d shots still flying %d steps after a shot of %d steps' life: step_shots moves "
        "them (shots_move), which ends them";
    static const char __far e_none[] = "fireGunFromShip(0, 0) put no shot in the world";
    const RvWeapon __far *w;
    int16_t life;
    pt_alone(PT_PLAYER);
    w = weapon(GUN_TYPE(0, 0));
    life = (int16_t)((int32_t)w->range_km * KPS_HZ / w->speed_kps);
    (void)fireGunFromShip(0, 0);
    REQUIRE(shots_count() == 1, e_none, 0, 0, 0);
    if (!pt_step())
        return;
    REQUIRE(shots_count() >= 1, e_gone, 0, 0, 0);
    if (!pt_steps(life + 4))
        return;
    REQUIRE(shots_count() == 0, e_left, shots_count(), life + 5, life);
}

/* ======================================================================================================== */
/* M3: damage (do_damage, step_shots, step_timers) */

/* the player still at the origin, and a frozen Nicander 3,000 km ahead facing away (its rear to the player) */
static void pt_target(void)
{
    pt_alone(PT_PLAYER);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 0, 0, 3000, 0);
}

static const char __far e_did[] = "do_damage returned 0 (nothing done) for a hit of %d on a ship";
static const char __far e_same[] = "%d of its shields and armour changed (front shield %d to %d): only the side hit "
    "takes it";

/* the sides that changed: a bit each, 1 2 the shields (front, rear), 4 .. 32 the armour (front, rear, left, right),
 * 64 the hull */
static int16_t pt_changed(const PtHurt *a, const PtHurt *b)
{
    int16_t k, m = 0;
    for (k = 0; k < 2; k++)
        if (a->sh[k] != b->sh[k])
            m |= 1 << k;
    for (k = 0; k < 4; k++)
        if (a->ar[k] != b->ar[k])
            m |= 4 << k;
    if (a->hull != b->hull)
        m |= 64;
    return m;
}

/* a hit at (f, r) on the target with its shields as given, and what changed */
static int16_t pt_side_hit(int16_t f, int16_t r, uint16_t qty, int16_t shields, PtHurt *a, PtHurt *b, uint16_t *ret)
{
    pt_target();
    if (!shields)
        SHIELD(1, 0) = SHIELD(1, 1) = 0;
    pt_hurt(1, a);
    *ret = pt_hit(0xFFFF, 1, qty, f, r, 0);
    pt_hurt(1, b);
    return pt_changed(a, b);
}

/* M3.1 */
static void t_shield_front(void)
{
    static const char __far e_more[] = "the front shield lost %d for a hit of %d";
    PtHurt a, b;
    uint16_t r;
    int16_t m = pt_side_hit(2, 0, 3, 1, &a, &b, &r);
    REQUIRE(r != 0, e_did, 3, 0, 0);
    REQUIRE(m == 1 && b.sh[0] < a.sh[0], e_same, m, a.sh[0], b.sh[0]);
    EXPECT(a.sh[0] - b.sh[0] <= 3, e_more, a.sh[0] - b.sh[0], 3, 0);
}

/* M3.2 */
static void t_shield_rear(void)
{
    static const char __far e_rear[] = "a hit from behind changed %d (front shield %d to %d): the rear shield (bit 2) "
        "takes it";
    PtHurt a, b;
    uint16_t r;
    int16_t m = pt_side_hit(-2, 0, 3, 1, &a, &b, &r);
    REQUIRE(r != 0, e_did, 3, 0, 0);
    REQUIRE(m == 2 && b.sh[1] < a.sh[1], e_rear, m, a.sh[0], b.sh[0]);
}

/* M3.3 */
static void t_armour(void)
{
    static const char __far e_arm[] = "with the shields down a front hit changed %d (front armour %d to %d): the front "
        "armour (bit 4) takes it";
    PtHurt a, b;
    uint16_t r;
    int16_t m = pt_side_hit(2, 0, 3, 0, &a, &b, &r);
    REQUIRE(r != 0, e_did, 3, 0, 0);
    REQUIRE(m == 4 && b.ar[0] < a.ar[0], e_arm, m, a.ar[0], b.ar[0]);
}

/* M3.4 */
static void t_armour_sides(void)
{
    static const char __far e_left[] = "a hit on its left changed %d (left armour %d to %d): the left armour (bit 16) "
        "takes it";
    static const char __far e_right[] = "a hit on its right changed %d (right armour %d to %d): the right armour (bit "
        "32) takes it";
    PtHurt a, b;
    uint16_t r;
    int16_t m = pt_side_hit(0, -2, 2, 0, &a, &b, &r);
    REQUIRE(r != 0, e_did, 2, 0, 0);
    REQUIRE(m == 16, e_left, m, a.ar[2], b.ar[2]);
    m = pt_side_hit(0, 2, 2, 0, &a, &b, &r);
    REQUIRE(m == 32, e_right, m, a.ar[3], b.ar[3]);
}

/* M3.5 */
static void t_hull(void)
{
    static const char __far e_hull[] = "with its front shield and armour gone a front hit changed %d (hull damage %d to "
        "%d): the hull (damagePoints, bit 64) takes it";
    PtHurt a, b;
    uint16_t r;
    pt_target();
    SHIELD(1, 0) = SHIELD(1, 1) = 0;
    ARMOR(1, 0) = 0;
    pt_hurt(1, &a);
    r = pt_hit(0xFFFF, 1, 3, 2, 0, 0);
    pt_hurt(1, &b);
    REQUIRE(r != 0, e_did, 3, 0, 0);
    REQUIRE(pt_changed(&a, &b) == 64 && b.hull > a.hull, e_hull, pt_changed(&a, &b), a.hull, b.hull);
}

/* M3.6 */
static void t_big_hit(void)
{
    static const char __far e_dead[] = "a hit of 200 on a ship of %d all told left it alive (shipAiState %d, hull "
        "damage %d): a ship whose hull is gone dies, shipAiState 9";
    static const char __far e_wrap[] = "a shield or armour wrapped past 0 (%d, %d): they stop at 0";
    pt_target();
    (void)pt_hit(0xFFFF, 1, 200, 2, 0, 0);
    if (!pt_step())
        return;
    REQUIRE(shipAiState[1] == 9, e_dead, pt_health(1) + 200, shipAiState[1], damagePoints[1]);
    REQUIRE(SHIELD(1, 0) <= SHIELD_MAX(1, 0) && ARMOR(1, 0) <= slot_type(1)->armor_f, e_wrap, SHIELD(1, 0), ARMOR(1, 0),
            0);
}

/* M3.7 */
static void t_no_wrap(void)
{
    static const char __far e_wrap[] = "after hit %d a shield or armour wrapped (front shield %d, front armour %d)";
    static const char __far e_back[] = "a hit on a dying ship brought it back (shipAiState %d)";
    static const char __far e_dead[] = "100 hits of 3 all round a ship of %d all told: still alive";
    int16_t i, was_dead = 0;
    int32_t all;
    pt_target();
    all = pt_health(1) + slot_type(1)->hull;
    for (i = 0; i < 100; i++) {
        (void)pt_hit(0xFFFF, 1, 3, (int16_t)(i % 3 - 1) * 2, (int16_t)(i % 5 - 2), 0);
        REQUIRE(SHIELD(1, 0) <= SHIELD_MAX(1, 0) && SHIELD(1, 1) <= SHIELD_MAX(1, 1)
                && ARMOR(1, 0) <= slot_type(1)->armor_f && ARMOR(1, 1) <= slot_type(1)->armor_r
                && ARMOR(1, 2) <= slot_type(1)->armor_l && ARMOR(1, 3) <= slot_type(1)->armor_rt, e_wrap, i,
                SHIELD(1, 0), ARMOR(1, 0));
        if (shipAiState[1] == 9)
            was_dead = 1;
        REQUIRE(!was_dead || shipAiState[1] == 9, e_back, shipAiState[1], 0, 0);
    }
    if (!pt_step())
        return;
    REQUIRE(shipAiState[1] == 9 || !slot_is_ship(1), e_dead, all, 0, 0);
}

/* M3.8 */
static void t_player_safe(void)
{
    static const char __far e_none[] = "a hit on the player did nothing with playerCanBeDamaged 1: do_damage does "
        "nothing yet";
    static const char __far e_hurt[] = "the player was hurt (%d changed) with playerCanBeDamaged 0";
    static const char __far e_ret[] = "do_damage returned %d for a hit it ignored: 0 expected";
    PtHurt a, b;
    uint16_t r;
    pt_target();
    pt_hurt(0, &a);
    (void)pt_hit(0xFFFF, 0, 3, 2, 0, 0);
    pt_hurt(0, &b);
    REQUIRE(pt_changed(&a, &b), e_none, 0, 0, 0);
    pt_target();
    playerCanBeDamaged = 0;
    pt_hurt(0, &a);
    r = pt_hit(1, 0, 3, 2, 0, 0);
    pt_hurt(0, &b);
    REQUIRE(!pt_changed(&a, &b), e_hurt, pt_changed(&a, &b), 0, 0);
    EXPECT(r == 0, e_ret, r, 0, 0);
}

/* M3.9 */
static void t_invulnerable(void)
{
    static const char __far e_none[] = "a hit did nothing: do_damage does nothing yet";
    static const char __far e_hurt[] = "a ship with rvFlags 1 (invulnerable) was hurt (%d changed)";
    PtHurt a, b;
    pt_target();
    pt_hurt(1, &a);
    (void)pt_hit(0xFFFF, 1, 3, 2, 0, 0);
    pt_hurt(1, &b);
    REQUIRE(pt_changed(&a, &b), e_none, 0, 0, 0);
    pt_target();
    rvFlags[1] |= 1;
    pt_hurt(1, &a);
    (void)pt_hit(0xFFFF, 1, 3, 2, 0, 0);
    pt_hurt(1, &b);
    REQUIRE(!pt_changed(&a, &b), e_hurt, pt_changed(&a, &b), 0, 0);
}

/* what a hit may change on a slot, to compare two tries of it */
typedef struct {
    uint8_t shield[4], armour[8], hull[2], core, guns, engine, table[51], state[2], energy[2];
    uint32_t seed, cockpit;
} PtDamage;

static void pt_damage_get(int16_t s, PtDamage *d)
{
    _fmemcpy(d->shield, (uint8_t *)curShield + s * 4, 4);
    _fmemcpy(d->armour, armorState + s * 8, 8);
    _fmemcpy(d->hull, (uint8_t *)damagePoints + s * 2, 2);
    d->core = coreHp[s];
    d->guns = gunDamage[s];
    d->engine = engineFlag[s];
    _fmemcpy(d->table, gunTable + s * gunTable_SIZE, gunTable_SIZE);
    _fmemcpy(d->state, (uint8_t *)shipAiState + s * 2, 2);
    _fmemcpy(d->energy, (uint8_t *)gunEnergy + s * 2, 2);
    d->seed = randomSeed;
    d->cockpit = rvCockpitDamage;
}

static void pt_damage_set(int16_t s, const PtDamage *d)
{
    _fmemcpy((uint8_t *)curShield + s * 4, d->shield, 4);
    _fmemcpy(armorState + s * 8, d->armour, 8);
    _fmemcpy((uint8_t *)damagePoints + s * 2, d->hull, 2);
    coreHp[s] = d->core;
    gunDamage[s] = d->guns;
    engineFlag[s] = d->engine;
    _fmemcpy(gunTable + s * gunTable_SIZE, d->table, gunTable_SIZE);
    _fmemcpy((uint8_t *)shipAiState + s * 2, d->state, 2);
    _fmemcpy((uint8_t *)gunEnergy + s * 2, d->energy, 2);
    randomSeed = d->seed;
    rvCockpitDamage = d->cockpit;
}

/* M3.10 */
static void t_any_source(void)
{
    static const char __far e_none[] = "a hit of %d did nothing: do_damage does nothing yet";
    static const char __far e_src[] = "a hit of %d came out otherwise with src %d than with src 0xFFFF: what a hit "
        "does must not depend on src (the layer replays hits with another)";
    static const char __far e_ret[] = "do_damage returned %d with src %d and %d with 0xFFFF";
    static uint16_t __far qty[2] = { 3, 30 };
    PtDamage before, base, other;
    uint16_t src[2], r0, r;
    int16_t q, k, i;
    pt_target();
    pt_speed(0, 0);
    (void)fireGunFromShip(0, 0);
    i = shots_count() - 1;
    src[0] = 0;
    src[1] = i >= 0 ? (uint16_t)shot_source(i) : 0;
    for (q = 0; q < 2; q++) {
        pt_damage_get(1, &before);
        r0 = pt_hit(0xFFFF, 1, qty[q], 2, 1, 0);
        pt_damage_get(1, &base);
        REQUIRE(_fmemcmp(&before, &base, sizeof base), e_none, qty[q], 0, 0);
        for (k = 0; k < 2; k++) {
            pt_damage_set(1, &before);
            r = pt_hit(src[k], 1, qty[q], 2, 1, 0);
            pt_damage_get(1, &other);
            REQUIRE(!_fmemcmp(&base, &other, sizeof base), e_src, qty[q], src[k], 0);
            REQUIRE((r != 0) == (r0 != 0), e_ret, r, src[k], r0);
        }
        pt_damage_set(1, &before);
    }
}

/* M3.11 */
static void t_shots_hit(void)
{
    static const char __far e_none[] = "3 s of fire at a ship %d km dead ahead, side on, took nothing off it (%d shots "
        "fired): step_shots finds hits with shot_hit and calls do_damage";
#ifdef RV_HOST
    static const char __far e_src[] = "the hits came with no bolt of the player's as do_damage's src: pass "
        "shot_source(i) (a bolt whose parent is the shooter: how the layer knows who fired)";
#endif
    int32_t h0;
    uint16_t a0;
    pt_alone(PT_PLAYER);
    /* (side on, at the guns' height: fighters' hulls are flat, some 100 km thick to 900 across) */
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 0, (int32_t)GUN_MOUNT_Y(0, 0) * collide_half(0) / 32 / 256, 1500,
            TURN / 4);
    h0 = pt_health(1);
    g_sys.armed = 1;
    g_pilot.guns = 1;
    a0 = shots_added();
    if (!pt_steps(3 * SEC))
        return;
    REQUIRE(pt_health(1) < h0 || shipAiState[1] == 9 || !slot_is_ship(1), e_none, 1500,
            (uint16_t)(shots_added() - a0), 0);
#ifdef RV_HOST
    EXPECT(host_bolt_hits[0] > 0, e_src, 0, 0, 0);
#endif
}

/* M3.12 */
static void t_recharge(void)
{
    static const char __far e_none[] = "the shields did not come back in 20 s (front %d, rear %d)";
    static const char __far e_over[] = "a shield came back past its most (%d of %d)";
    static const char __far e_armour[] = "the armour came back (%d to %d): only the shields recharge in flight";
    int16_t i;
    pt_alone(PT_PLAYER);
    SHIELD(0, 0) = 1;
    SHIELD(0, 1) = 0;
    ARMOR(0, 0) = 2;
    for (i = 0; i < 20 * SEC; i++) {
        if (!pt_step())
            return;
        REQUIRE(SHIELD(0, 0) <= SHIELD_MAX(0, 0) && SHIELD(0, 1) <= SHIELD_MAX(0, 1), e_over, SHIELD(0, 0),
                SHIELD_MAX(0, 0), 0);
    }
    REQUIRE(SHIELD(0, 0) > 1 && SHIELD(0, 1) > 0, e_none, SHIELD(0, 0), SHIELD(0, 1), 0);
    REQUIRE(ARMOR(0, 0) == 2, e_armour, 2, ARMOR(0, 0), 0);
}

/* ======================================================================================================== */
/* M4: deaths (step_deaths, despawn, delayedDespawn) */

/* M4.1 */
static void t_kill(void)
{
    static const char __far e_dying[] = "shipAiState is %d a step after a killing hit: 9 (dying)";
    static const char __far e_kept[] = "the slot was emptied at once: keep it while it explodes (the layer learns of "
        "a death from shipAiState 9 in the slot)";
    static const char __far e_short[] = "its slot was emptied %d steps after the hit: an explosion takes a while";
    static const char __far e_long[] = "still in its slot 10 s after it died";
    static const char __far e_thunk[] = "it was never ended through despawn or delayedDespawn (%d calls): the layer "
        "sees a death end through them";
    int16_t n;
    int32_t ends;
    pt_alone(PT_PLAYER);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_AI, 0, 0, 6000, 0);
    (void)pt_hit(0xFFFF, 1, 200, -2, 0, 0);
    if (!pt_step())
        return;
    REQUIRE(slot_is_ship(1), e_kept, 0, 0, 0);
    REQUIRE(shipAiState[1] == 9, e_dying, shipAiState[1], 0, 0);
    for (n = 1; n < 10 * SEC && slot_is_ship(1); n++)
        if (!pt_step())
            return;
    REQUIRE(!slot_is_ship(1), e_long, 0, 0, 0);
    EXPECT(n >= 3, e_short, n, 0, 0);
    ends = pt_calls(RV_FN_despawn, 0, 1);
    if (ends >= 0) {
        ends += pt_calls(RV_FN_delayedDespawn, 1, 1);
        EXPECT(ends >= 1, e_thunk, ends, 0, 0);
    }
}

/* M4.2 */
static void t_dying_quiet(void)
{
    static const char __far e_fired[] = "a dying ship fired (%d shots, %d missiles, %d calls of fireGunFromShip)";
    static const char __far e_quiet[] = "the enemy never fired at the player in 30 s: nothing to test (M5.4 first)";
    int16_t i, k, missiles = 0, more;
    uint16_t a0;
    int32_t c0, c;
    pt_alone(PT_PLAYER);
    playerCanBeDamaged = 0;
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_AI, 0, 0, 2000, TURN / 2);
    a0 = shots_added();
    for (i = 0; i < 30 * SEC && shots_added() == a0; i++)
        if (!pt_step())
            return;
    REQUIRE(shots_added() != a0, e_quiet, 0, 0, 0);
    (void)pt_hit(0xFFFF, 1, 200, -2, 0, 0);
    if (!pt_step())                     /* (its last step: it may have fired before step_deaths saw its hull gone) */
        return;
    a0 = shots_added();                 /* (the player holds its fire: every shot is the enemy's) */
    c0 = pt_calls(RV_FN_fireGunFromShip, 0, 1);
    for (k = 1; k <= RV_MAX_SHIP_SLOT; k++)
        if (maybeEntityType[k] == ET_MISSILE && parentShipForShip[k] == 1)
            missiles++;
    for (i = 0; i < 3 * SEC && slot_is_ship(1); i++) {
        if (!pt_step())
            return;
        for (k = 1, more = 0; k <= RV_MAX_SHIP_SLOT; k++)
            if (maybeEntityType[k] == ET_MISSILE && parentShipForShip[k] == 1)
                more++;
        REQUIRE(shots_added() == a0 && more <= missiles, e_fired, (uint16_t)(shots_added() - a0), more - missiles, 0);
        missiles = more;
    }
    c = pt_calls(RV_FN_fireGunFromShip, 0, 1);
    EXPECT(c == c0, e_fired, 0, 0, c - c0);
}

/* M4.3 */
static void t_player_dies(void)
{
    static const char __far e_status[] = "10 s after the player's ship was destroyed missionStatus is %d: 4 (dead)";
    static const char __far e_dying[] = "the player's ship never went to shipAiState 9";
    static const char __far e_score[] = "victoryPoints is %d after the player died: a dead pilot's mission is lost (0)";
    int16_t i, dying = 0;
    pt_alone(PT_PLAYER);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 0, 0, 20000, 0);
    (void)pt_hit(1, 0, 500, 2, 0, 0);
    for (i = 0; i < 10 * SEC && !missionStatus; i++) {
        if (!pt_step())
            return;
        if (shipAiState[0] == 9)
            dying = 1;
    }
    REQUIRE(missionStatus == 4, e_status, missionStatus, 0, 0);
    EXPECT(dying, e_dying, 0, 0, 0);
    logic_end();
    REQUIRE(victoryPoints == 0, e_score, victoryPoints, 0, 0);
}

/* ======================================================================================================== */
/* M5: the AI (aiFighterThink, aiShipThink, aiSetSpeed, step_minds) */

/* M5.1 */
static void t_ai_thunks(void)
{
    static const char __far e_think[] = "aiFighterThink was called %d times in 60 steps for an AI fighter (slot %d): "
        "every few steps at least, through the thunk (the layer skips it where the ship is a puppet)";
    static const char __far e_not[] = "aiFighterThink was called %d times for slot %d, which no AI flies (the player, "
        "a puppet, a frozen ship, a capital ship)";
    static const char __far e_ship[] = "aiShipThink was called %d times in 60 steps for an AI capital ship (slot %d)";
    int16_t s;
    int32_t n;
    pt_alone(PT_PLAYER);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_AI, -3000, 0, 9000, TURN / 2);
    pt_ship(2, PT_ENEMY, SIDE_AERA, CTL_AI, 3000, 0, 9000, TURN / 2);
    pt_ship(3, PT_ENEMY, SIDE_AERA, CTL_PUPPET, 0, 3000, 12000, TURN / 2);
    pt_ship(4, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 0, -3000, 12000, TURN / 2);
    pt_ship(5, PT_CAPSHIP, SIDE_TERRAN, CTL_AI, 0, 0, -20000, 0);
    if (!pt_steps(60))
        return;
    for (s = 1; s <= 2; s++) {
        n = pt_calls(RV_FN_aiFighterThink, 0, s);
        REQUIRE(n >= 15, e_think, n, s, 0);
    }
    for (s = 0; s <= 5; s++)
        if (s == 0 || s >= 3) {
            n = pt_calls(RV_FN_aiFighterThink, 0, s);
            REQUIRE(n == 0, e_not, n, s, 0);
        }
    n = pt_calls(RV_FN_aiShipThink, 0, 5);
    REQUIRE(n >= 15, e_ship, n, 5, 0);
}

/* the player still at the origin, its guns safe, and an AI Nicander 6,000 km ahead facing away */
static void pt_duel(void)
{
    pt_alone(PT_PLAYER);
    g_sys.armed = 0;
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_AI, 0, 0, 6000, 0);
}

/* M5.2 */
static void t_ai_turns(void)
{
    static const char __far e_turn[] = "in 10 s the enemy never pointed its nose at the player (best %d/256)";
    int16_t i;
    int32_t best = -256, a;
    pt_duel();
    for (i = 0; i < 10 * SEC && best < 230; i++) {
        if (!pt_step())
            return;
        if (slot_is_ship(1) && (a = pt_aim(1, 0)) > best)
            best = a;
    }
    REQUIRE(best >= 230, e_turn, best, 0, 0);
}

/* M5.3 */
static void t_ai_closes(void)
{
    static const char __far e_far[] = "in 30 s the enemy came no nearer than %d km (it began 6,000 km away)";
    int16_t i;
    int32_t least = 999999, d;
    pt_duel();
    for (i = 0; i < 30 * SEC && least >= 2500; i++) {
        if (!pt_step())
            return;
        if (slot_is_ship(1) && (d = pt_km(0, 1)) < least)
            least = d;
    }
    REQUIRE(least < 2500, e_far, least, 0, 0);
}

/* M5.4 */
static void t_ai_fires(void)
{
    static const char __far e_quiet[] = "in 45 s the enemy fired nothing at a still player %d km away";
    static const char __far e_miss[] = "in 45 s the enemy fired %d shots and none hurt the player";
    int16_t i;
    int32_t h0;
    uint16_t a0;
    pt_duel();
    h0 = pt_health(0);
    a0 = shots_added();                 /* (the enemy is the only one firing) */
    for (i = 0; i < 45 * SEC && missionStatus == 0 && (shots_added() == a0 || pt_health(0) == h0); i++)
        if (!pt_step())
            return;
    REQUIRE(shots_added() != a0, e_quiet, slot_is_ship(1) ? pt_km(0, 1) : 0, 0, 0);
    REQUIRE(pt_health(0) < h0, e_miss, (uint16_t)(shots_added() - a0), 0, 0);
}

/* M5.5: an enemy beside the player, in its guns' range but the player 90 degrees off its nose: it turns before it
 * fires (its first 8 volleys, through the harness's count of fireGunFromShip) */
static void t_ai_aims(void)
{
#ifdef RV_HOST
    static const char __far e_quiet[] = "in 20 s the enemy fired %d volleys at a still player beside it";
    static const char __far e_aim[] = "of %d volleys only %d were fired with the player within 30 degrees of the "
        "enemy's nose: an AI fires when its guns bear";
#endif
    int16_t i;
    int32_t calls = 0, aimed = 0, c, was = 0;
    pt_alone(PT_PLAYER);
    g_sys.armed = 0;
    playerCanBeDamaged = 0;
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_AI, 1200, 0, 0, 0);
    for (i = 0; i < 20 * SEC && calls < 8; i++) {
        if (!pt_step())
            return;
        c = pt_calls(RV_FN_fireGunFromShip, 0, 1);
        if (c > was) {
            calls++;
            if (slot_is_ship(1) && pt_aim(1, 0) >= AIM_COS)
                aimed++;
            was = c;
        }
    }
#ifdef RV_HOST
    REQUIRE(calls > 0, e_quiet, calls, 0, 0);
    REQUIRE(aimed * 100 >= calls * AIM_SHARE, e_aim, calls, aimed, 0);
#else
    (void)calls;
    (void)aimed;
#endif
}

/* M5.6 */
static void t_ai_limits(void)
{
    static const char __far e_turn[] = "the enemy's nose turned %d (1024 a turn) in a step: its type allows %d";
    static const char __far e_fast[] = "the enemy flew %d kps (afterburner %d): its type's top speed is %d";
    static const char __far e_still[] = "the enemy turned %d (1024 a turn) and flew %d km in 30 s: nothing to test";
    const RvShipType __far *t;
    RvVec3 f0, x;
    int16_t i, most;
    int32_t c, turned = 0;
    pt_duel();
    t = slot_type(1);
    most = (int16_t)(pt_turn_of(1, t->yaw_dps > t->pitch_dps ? t->yaw_dps : t->pitch_dps, 9) * 3 / 2 + 3);
    for (i = 0; i < 30 * SEC && missionStatus == 0 && slot_is_ship(1); i++) {
        f0 = gOrientationFrontVector[1];
        if (!pt_step())
            return;
        if (!slot_is_ship(1) || shipAiState[1] == 9)
            break;
        vec_cross(&x, &gOrientationFrontVector[1], &f0);
        c = lf_atan2(pt_len(x.x, x.y, x.z), vec_dot(&gOrientationFrontVector[1], &f0) / 256);
        REQUIRE(c <= most, e_turn, c, most, 0);
        turned += c;
        REQUIRE((int32_t)speedLo[1] <= (int32_t)(rvAfterburner[1] ? t->ab_kps : t->max_kps), e_fast, (int32_t)speedLo[1],
                rvAfterburner[1], rvAfterburner[1] ? t->ab_kps : t->max_kps);
    }
    REQUIRE(turned >= TURN / 8 && (!slot_is_ship(1) || pt_km_to(1, 0, 0, 6000) > 500), e_still, turned,
            slot_is_ship(1) ? pt_km_to(1, 0, 0, 6000) : 0, 0);
}

/* M5.7 */
static void t_wingman_fights(void)
{
    static const char __far e_none[] = "in 60 s the wingman did the enemy no harm (the player held its fire)";
#ifdef RV_HOST
    static const char __far e_src[] = "the enemy was hurt, but not by the wingman's gunfire";
#endif
    int16_t i;
    int32_t h0;
    pt_alone(PT_PLAYER);
    playerCanBeDamaged = 0;
    g_sys.armed = 0;
    pt_ship(1, PT_PLAYER, SIDE_TERRAN, CTL_AI, 1500, 0, 0, 0);
    pt_ship(2, PT_ENEMY, SIDE_AERA, CTL_AI, 0, 0, 7000, TURN / 2);
    h0 = pt_health(2);
    for (i = 0; i < 60 * SEC && slot_is_ship(2) && pt_health(2) == h0; i++)
        if (!pt_step())
            return;
    REQUIRE(!slot_is_ship(2) || pt_health(2) < h0, e_none, 0, 0, 0);
#ifdef RV_HOST
    EXPECT(host_bolt_hits[1] > 0, e_src, 0, 0, 0);
#endif
}

/* M5.8 */
static void t_formation(void)
{
    static const char __far e_far[] = "the wingman was %d km from the player %d s into a straight flight at cruise: "
        "it should keep with its leader (within 5,000 km)";
    static const char __far e_still[] = "the player flew only %d km in 60 s at cruise: nothing to keep up with (M1.1 "
        "first)";
    int16_t i;
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->cruise_kps);
    pt_ship(1, PT_PLAYER, SIDE_TERRAN, CTL_AI, 1500, 0, 0, 0);
    for (i = 0; i < 60 * SEC; i++) {
        if (!pt_step())
            return;
        if (i >= 50 * SEC)
            REQUIRE(slot_is_ship(1) && pt_km(0, 1) < 5000, e_far, slot_is_ship(1) ? pt_km(0, 1) : -1, i / SEC, 0);
    }
    REQUIRE(pt_km_to(0, 0, 0, 0) > 10000, e_still, pt_km_to(0, 0, 0, 0), 0, 0);
}

/* ======================================================================================================== */
/* M6: collisions (step_collisions) */

/* M6.1 */
static void t_ram(void)
{
    static const char __far e_test[] = "the test's two ships do not overlap to begin with";
    static const char __far e_stuck[] = "still inside one another after 10 steps (collide_ships, %d positions deep: "
        "%d is touching)";
    static const char __far e_hurt[] = "a collision hurt the player by %d and the enemy by %d: both take damage";
    LfVec push;
    int32_t depth, h0, h1;
    int16_t i;
    pt_alone(PT_PLAYER);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 0, 0, 300, TURN / 2);
    REQUIRE(collide_ships(0, 1, &push, &depth), e_test, 0, 0, 0);
    h0 = pt_health(0);
    h1 = pt_health(1);
    for (i = 0; i < 10; i++)
        if (!pt_step())
            return;
    REQUIRE(!slot_is_ship(1) || !collide_ships(0, 1, &push, &depth) || depth <= collide_half(0) / 16, e_stuck, depth,
            collide_half(0) / 16, 0);
    REQUIRE(pt_health(0) < h0 && (pt_health(1) < h1 || shipAiState[1] == 9 || !slot_is_ship(1)), e_hurt,
            h0 - pt_health(0), slot_is_ship(1) ? h1 - pt_health(1) : 0, 0);
}

/* M6.2 */
static void t_no_ghosts(void)
{
    static const char __far e_in[] = "flown at another ship, the player stayed inside it %d steps in a row (step %d): "
        "the push should part them";
    static const char __far e_never[] = "the player, flown at a ship %d km ahead, never hurt it: no collision (it "
        "flew through, or a collision does no harm)";
    LfVec push;
    int32_t depth, h1;
    int16_t i, run = 0;
    pt_alone(PT_PLAYER);
    pt_speed(0, slot_type(0)->max_kps);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 0, 0, 1500, TURN / 2);
    playerCanBeDamaged = 0;
    h1 = pt_health(1);
    for (i = 0; i < 5 * SEC; i++) {
        if (!pt_step())
            return;
        if (slot_is_ship(1) && collide_ships(0, 1, &push, &depth) && depth > collide_half(0) / 16)
            REQUIRE(++run <= 3, e_in, run, i, 0);
        else
            run = 0;
    }
    REQUIRE(!slot_is_ship(1) || pt_health(1) < h1 || shipAiState[1] == 9, e_never, 1500, 0, 0);
}

/* ======================================================================================================== */
/* M7: missiles (fireGunFromShip's launchers, step_missiles) */

static const char __far e_nolaunch[] = "the test's ship (type %d) has no missile launcher";
static const char __far e_nomissile[] = "fireGunFromShip on its launcher (gun %d) returned %d: a missile's slot "
    "(ET_MISSILE) expected";

/* M7.1 */
static void t_launch(void)
{
    static const char __far e_slot[] = "fireGunFromShip on a launcher (gun %d) returned %d: a missile's slot "
        "(maybeEntityType ET_MISSILE) expected";
    static const char __far e_parent[] = "the missile's parentShipForShip is %d: its launcher (0)";
    static const char __far e_rounds[] = "the launcher has %d rounds after a launch from %d";
    static const char __far e_far[] = "the missile starts %d km from its ship";
    int16_t l, r0;
    uint16_t m;
    pt_alone(PT_PLAYER);
    REQUIRE((l = pt_launcher(0)) >= 0, e_nolaunch, PT_PLAYER, 0, 0);
    r0 = GUN_STATE(0, l) & 0x7F;
    m = fireGunFromShip(0, (uint16_t)l);
    REQUIRE(m <= RV_MAX_SHIP_SLOT && maybeEntityType[m] == ET_MISSILE, e_slot, l, m, 0);
    REQUIRE(parentShipForShip[m] == 0, e_parent, parentShipForShip[m], 0, 0);
    REQUIRE((GUN_STATE(0, l) & 0x7F) == r0 - 1, e_rounds, GUN_STATE(0, l) & 0x7F, r0, 0);
    REQUIRE(pt_km(0, (int16_t)m) <= 2 * collide_half(0) / 256, e_far, pt_km(0, (int16_t)m), 0, 0);
}

/* M7.2 */
static void t_missile_key(void)
{
    static const char __far e_none[] = "Enter (g_pilot.missile) launched no missile";
    static const char __far e_count[] = "%d missiles from %d presses with %d rounds";
    int16_t l, r0, i, k, n = 0, seen[RV_MAX_SHIP_SLOT + 1];
    pt_alone(PT_PLAYER);
    REQUIRE((l = pt_launcher(0)) >= 0, e_nolaunch, PT_PLAYER, 0, 0);
    r0 = GUN_STATE(0, l) & 0x7F;
    for (i = 0; i <= RV_MAX_SHIP_SLOT; i++)
        seen[i] = 0;
    for (i = 0; i < r0 + 2; i++) {
        g_pilot.missile = 1;
        if (!pt_step())
            return;
        g_pilot.missile = 0;
        for (k = 1; k <= RV_MAX_SHIP_SLOT; k++)
            if (maybeEntityType[k] == ET_MISSILE && parentShipForShip[k] == 0 && !seen[k]) {
                seen[k] = 1;
                n++;
            }
        if (i == 0)
            REQUIRE(n == 1, e_none, 0, 0, 0);
        if (!pt_steps(2 * SEC))
            return;
        for (k = 1; k <= RV_MAX_SHIP_SLOT; k++)
            if (maybeEntityType[k] != ET_MISSILE)
                seen[k] = 0;
    }
    REQUIRE(n == r0, e_count, n, r0 + 2, r0);
}

/* M7.3 */
static void t_missile_flies(void)
{
    static const char __far e_gone[] = "the missile was gone after %d steps in empty space";
    static const char __far e_speed[] = "the missile moves %d positions a step: its weapon's speed is %d";
    static const char __far e_way[] = "the dumbfire flies %d/256 along its launcher's nose: straight ahead expected";
    int16_t l, m;
    RvVec3 p0, d;
    int32_t want, got;
    pt_alone(PT_PLAYER);
    REQUIRE((l = pt_launcher(0)) >= 0, e_nolaunch, PT_PLAYER, 0, 0);
    want = KPS_STEP(weapon(GUN_TYPE(0, l))->speed_kps);
    m = (int16_t)fireGunFromShip(0, (uint16_t)l);
    REQUIRE(m >= 1 && m <= RV_MAX_SHIP_SLOT && maybeEntityType[m] == ET_MISSILE, e_nomissile, l, (uint16_t)m, 0);
    if (!pt_steps(SEC))
        return;
    REQUIRE(maybeEntityType[m] == ET_MISSILE, e_gone, SEC, 0, 0);
    p0 = gPositionVector[m];
    if (!pt_steps(10))
        return;
    REQUIRE(maybeEntityType[m] == ET_MISSILE, e_gone, SEC + 10, 0, 0);
    d.x = (gPositionVector[m].x - p0.x) / 10;
    d.y = (gPositionVector[m].y - p0.y) / 10;
    d.z = (gPositionVector[m].z - p0.z) / 10;
    got = pt_len(d.x, d.y, d.z);
    REQUIRE(got * 100 >= want * 80 && got * 100 <= want * 120, e_speed, got, want, 0);
    EXPECT(got && vec_dot(&d, &gOrientationFrontVector[0]) / got >= 250, e_way,
           got ? vec_dot(&d, &gOrientationFrontVector[0]) / got : 0, 0, 0);
}

/* a missile at a frozen Nicander, side on: ahead on its launcher's line (dumbfire) or off to the right (homing) */
static void t_missile_at(int16_t type, int32_t x, int32_t z, int16_t homing)
{
    static const char __far e_miss[] = "the missile did the target no harm in 10 s (%d km away)";
    static const char __far e_target[] = "the homing missile's missileTarget is %d: the target, slot %d";
    static const char __far e_left[] = "the missile's slot still flies after it hit";
    int16_t l, i;
    uint16_t m;
    int32_t h0;
    pt_alone(type);
    REQUIRE((l = pt_launcher(0)) >= 0, e_nolaunch, type, 0, 0);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, x + (homing ? 0 : (int32_t)GUN_MOUNT_X(0, l) * collide_half(0) / 32 / 256),
            homing ? 0 : (int32_t)GUN_MOUNT_Y(0, l) * collide_half(0) / 32 / 256, z, TURN / 4);
    playerCanBeDamaged = 0;
    g_sys.target = 1;
    rvTarget[0] = 1;
    playerTarget = 1;
    g_sys.lock = 1;
    h0 = pt_health(1);
    m = fireGunFromShip(0, (uint16_t)l);
    REQUIRE(m <= RV_MAX_SHIP_SLOT && maybeEntityType[m] == ET_MISSILE, e_nomissile, l, m, 0);
    if (homing)
        REQUIRE(missileTarget[m] == 1, e_target, missileTarget[m], 1, 0);
    for (i = 0; i < 10 * SEC && pt_health(1) == h0 && slot_is_ship(1) && shipAiState[1] != 9; i++)
        if (!pt_step())
            return;
    REQUIRE(pt_health(1) < h0 || shipAiState[1] == 9 || !slot_is_ship(1), e_miss, pt_len(x, 0, z), 0, 0);
    if (!pt_steps(2))
        return;
    EXPECT(maybeEntityType[m] != ET_MISSILE || parentShipForShip[m] != 0, e_left, 0, 0, 0);
}

/* M7.4 */
static void t_missile_hits(void) { t_missile_at(PT_PLAYER, 0, 2500, 0); }

/* M7.5 */
static void t_missile_homes(void) { t_missile_at(PT_HOMER, 1500, 2500, 1); }

/* M7.6 */
static void t_missile_ends(void)
{
    static const char __far e_long[] = "a missile fired into empty space still flies after %d steps (its range %d km, "
        "%d steps' worth): it ends at its range";
    int16_t l, i, life;
    uint16_t m;
    const RvWeapon __far *w;
    pt_alone(PT_PLAYER);
    REQUIRE((l = pt_launcher(0)) >= 0, e_nolaunch, PT_PLAYER, 0, 0);
    w = weapon(GUN_TYPE(0, l));
    life = (int16_t)((int32_t)w->range_km * KPS_HZ / w->speed_kps);
    m = fireGunFromShip(0, (uint16_t)l);
    REQUIRE(m <= RV_MAX_SHIP_SLOT && maybeEntityType[m] == ET_MISSILE, e_nomissile, l, m, 0);
    for (i = 0; i < 2 * life + SEC && maybeEntityType[m] == ET_MISSILE; i++)
        if (!pt_step())
            return;
    REQUIRE(maybeEntityType[m] != ET_MISSILE, e_long, i, w->range_km, life);
}

/* ======================================================================================================== */
/* M8: the mission (logic_start, step_mission, enterNavPoint, logic_end) */

#define NAV1_Z 20000                    /* km */
#define NAV2_X 12000
#define NAV2_Z 26000

/* a patrol: the player and a wingman at nav 0, two Nicanders at nav 1, an Agis at nav 2 */
static int16_t pt_patrol(void)
{
    int16_t i, pilot = RV_PILOT_NONE;
    for (i = 0; i < (int16_t)g_npilots && pilot == RV_PILOT_NONE; i++)
        if (g_pilots[i].role == RV_ROLE_PILOT)
            pilot = i;
    pt_mission();
    pt_nav(1, 0, 0, NAV1_Z);
    pt_nav(2, NAV2_X, 0, NAV2_Z);
    pt_mship(RV_SHIP_WINGMAN, SIDE_TERRAN, 0, 1, (uint8_t)pilot);
    pt_mship(PT_ENEMY, SIDE_AERA, 1, 2, RV_PILOT_NONE);
    pt_mship(PT_WEAK, SIDE_AERA, 2, 1, RV_PILOT_NONE);
    pt_launch(PT_PLAYER);
    return pilot;
}

/* the player taken to a place (km), as the autopilot would (and the wingman with it), then a few steps */
static int16_t pt_go(int32_t x, int32_t z)
{
    int16_t s;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s) && commAnimInfo[s] == SIDE_TERRAN && rvController[s] == CTL_AI) {
            gPositionVector[s].x = (x + 1500) * KM;
            gPositionVector[s].y = 0;
            gPositionVector[s].z = z * KM;
        }
    gPositionVector[0].x = x * KM;
    gPositionVector[0].y = 0;
    gPositionVector[0].z = z * KM;
    return pt_steps(2 * SEC);
}

/* every ship of a side killed, then their explosions waited out */
static int16_t pt_kill_side(int16_t side)
{
    int16_t s, i;
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s) && commAnimInfo[s] == side && shipAiState[s] != 9)
            (void)pt_hit(0xFFFF, s, 2000, 2, 0, 0);
    for (i = 0; i < 12 * SEC && pt_count(side, -1); i++)
        if (!pt_step())
            return 0;
    return 1;
}

/* M8.1 */
static void t_start(void)
{
    static const char __far e_player[] = "slot 0 is not the player's ship (type %d, controller %d): logic_start puts "
        "the player in slot 0, CTL_PLAYER, type g_camp.player_ship";
    static const char __far e_wing[] = "no wingman (side 0, an AI, %d of them) near the player at the start";
    static const char __far e_pilot[] = "the wingman's pilot (shipStateByte) is %d: the mission's %d";
    static const char __far e_record[] = "slot %d's slotMissionShip (%d) names no mission ship of its type and side";
    static const char __far e_early[] = "%d enemies out there at the start: the ships of nav 1 and 2 come when the "
        "player gets there";
    static const char __far e_nav[] = "currentNavPoint is %d at the start: 0";
    int16_t pilot, s, w = -1, n = 0;
    const uint8_t *rec;
    pilot = pt_patrol();
    REQUIRE(slot_is_ship(0) && rvController[0] == CTL_PLAYER && gInstanceShipTypes[0] == PT_PLAYER, e_player,
            gInstanceShipTypes[0], rvController[0], 0);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s) && commAnimInfo[s] == SIDE_TERRAN && rvController[s] == CTL_AI && pt_km(0, s) < 10000) {
            w = s;
            n++;
        }
    REQUIRE(n == 1, e_wing, n, 0, 0);
    EXPECT(shipStateByte[w] == pilot, e_pilot, shipStateByte[w], pilot, 0);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s)) {
            rec = missionShipTable + missionShipTable_SIZE * (slotMissionShip[s] & 63);
            REQUIRE(slotMissionShip[s] < 64 && *(const uint16_t *)rec == gInstanceShipTypes[s]
                    && *(const uint16_t *)(rec + 2) == commAnimInfo[s], e_record, s, slotMissionShip[s], 0);
        }
    REQUIRE(pt_count(SIDE_AERA, -1) == 0, e_early, pt_count(SIDE_AERA, -1), 0, 0);
    REQUIRE(currentNavPoint == 0, e_nav, currentNavPoint, 0, 0);
}

/* M8.2, M8.3 */
static void t_nav_arrive(int16_t stay)
{
    static const char __far e_nav[] = "at nav 1 currentNavPoint is %d: enterNavPoint(1) when the player gets there";
    static const char __far e_ships[] = "at nav 1 there are %d Nicanders: its 2 come when the player gets there";
    static const char __far e_where[] = "slot %d came %d km from nav 1: its ships come at their nav point";
    static const char __far e_rec[] = "slot %d's slotMissionShip (%d) is no ship of nav 1's";
    static const char __far e_again[] = "nav 1's ships came again: %d Nicanders after 5 s there";
    int16_t s;
    const uint8_t *rec;
    pt_patrol();
    if (!pt_steps(SEC) || !pt_go(0, NAV1_Z))
        return;
    REQUIRE(currentNavPoint == 1, e_nav, currentNavPoint, 0, 0);
    REQUIRE(pt_count(SIDE_AERA, PT_ENEMY) == 2, e_ships, pt_count(SIDE_AERA, PT_ENEMY), 0, 0);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)
        if (slot_is_ship(s) && commAnimInfo[s] == SIDE_AERA) {
            REQUIRE(pt_km_to(s, 0, 0, NAV1_Z) < 15000, e_where, s, pt_km_to(s, 0, 0, NAV1_Z), 0);
            rec = missionShipTable + missionShipTable_SIZE * (slotMissionShip[s] & 63);
            REQUIRE(slotMissionShip[s] < 64 && *(const uint16_t *)(rec + 8) == 1, e_rec, s, slotMissionShip[s], 0);
        }
    if (!stay)
        return;
    playerCanBeDamaged = 0;
    if (!pt_steps(5 * SEC))
        return;
    REQUIRE(pt_count(SIDE_AERA, PT_ENEMY) <= 2, e_again, pt_count(SIDE_AERA, PT_ENEMY), 0, 0);
}

static void t_nav1(void) { t_nav_arrive(0); }
static void t_nav_once(void) { t_nav_arrive(1); }

/* M8.4, M8.6: the patrol flown: each nav visited, its ships killed or not, then home and landed */
static void t_patrol(int16_t kill)
{
    static const char __far e_won[] = "victoryPoints is %d after a patrol that visited both navs and killed every "
        "enemy: nonzero (won)";
    static const char __far e_lost[] = "victoryPoints is %d after a patrol that left the enemies alive: 0 (lost)";
    static const char __far e_status[] = "missionStatus is %d before the landing";
    static const char __far e_early[] = "victoryPoints is %d a second into the patrol: 0 until it is won (logic_start "
        "scores over the launch's roll)";
    pt_patrol();
    playerCanBeDamaged = 0;
    if (!pt_steps(SEC))
        return;
    REQUIRE(victoryPoints == 0, e_early, victoryPoints, 0, 0);
    if (!pt_go(0, NAV1_Z) || (kill && !pt_kill_side(SIDE_AERA)))
        return;
    if (!pt_go(NAV2_X, NAV2_Z) || (kill && !pt_kill_side(SIDE_AERA)))
        return;
    if (!pt_go(0, 0))
        return;
    REQUIRE(missionStatus == 0, e_status, missionStatus, 0, 0);
    missionStatus = 1;                  /* (landed: the engine's C key, flight.c) */
    logic_end();
    if (kill)
        REQUIRE(victoryPoints != 0, e_won, victoryPoints, 0, 0);
    else
        REQUIRE(victoryPoints == 0, e_lost, victoryPoints, 0, 0);
}

static void t_won(void) { t_patrol(1); }
static void t_enemies_left(void) { t_patrol(0); }

/* M8.5 */
static void t_skipped(void)
{
    static const char __far e_lost[] = "victoryPoints is %d after landing without flying the patrol: 0 (lost; the "
        "launch's roll left 1 there for the logic to score over)";
    pt_patrol();
    if (!pt_steps(2 * SEC))
        return;
    missionStatus = 1;
    logic_end();
    REQUIRE(victoryPoints == 0, e_lost, victoryPoints, 0, 0);
}

/* M8.7 */
static void t_layer_spawn(void)
{
    static const char __far e_slot[] = "outerSpawnShipEntity(%d, 0) returned %d: a free slot";
    static const char __far e_where[] = "the spawned ship is %d km from nav 0: its nav point and its place off it";
    uint16_t s;
    int16_t k;
    pt_patrol();
    for (k = 1; k <= RV_MAX_SHIP_SLOT; k++)
        ship_clear(k);
    s = outerSpawnShipEntity(1, 0);
    REQUIRE(s >= 1 && s <= RV_MAX_SHIP_SLOT && slot_is_ship((int16_t)s), e_slot, 1, s, 0);
    REQUIRE(pt_km_to((int16_t)s, 0, 0, 0) < 5000, e_where, pt_km_to((int16_t)s, 0, 0, 0), 0, 0);
}

/* M8.8 */
static void t_autopilot(void)
{
    static const char __far e_went[] = "the autopilot (logic_autopilot) returned %d and left the player %d km from nav "
        "1: with no enemy near it takes the player on to the next nav point";
    static const char __far e_nav[] = "after the autopilot to nav 1 currentNavPoint is %d";
    static const char __far e_stay[] = "the autopilot returned %d and moved the player %d km with %d enemies 4,000 km "
        "ahead: not with enemies about";
    static const char __far e_next[] = "with nav 1's ships gone the autopilot left the player %d km from nav 2 "
        "(returned %d)";
    RvVec3 p0;
    int16_t r, s, k = 0;
    pt_patrol();
    playerCanBeDamaged = 0;
    if (!pt_steps(SEC))
        return;
    r = logic_autopilot();
    if (!pt_steps(2 * SEC))
        return;
    REQUIRE(r && pt_km_to(0, 0, 0, NAV1_Z) < 5000, e_went, r, pt_km_to(0, 0, 0, NAV1_Z), 0);
    REQUIRE(currentNavPoint == 1, e_nav, currentNavPoint, 0, 0);
    for (s = 1; s <= RV_MAX_SHIP_SLOT; s++)   /* (nav 1's enemies 4,000 km ahead of the player) */
        if (slot_is_ship(s) && commAnimInfo[s] == SIDE_AERA) {
            gPositionVector[s].x = gPositionVector[0].x + (int32_t)(k++ * 1500 - 750) * KM;
            gPositionVector[s].y = gPositionVector[0].y;
            gPositionVector[s].z = gPositionVector[0].z + 4000 * KM;
        }
    p0 = gPositionVector[0];
    r = logic_autopilot();
    REQUIRE(!r && pt_len((gPositionVector[0].x - p0.x) / 256, (gPositionVector[0].y - p0.y) / 256,
                         (gPositionVector[0].z - p0.z) / 256) < 100, e_stay, r,
            pt_len((gPositionVector[0].x - p0.x) / 256, (gPositionVector[0].y - p0.y) / 256,
                   (gPositionVector[0].z - p0.z) / 256), pt_count(SIDE_AERA, -1));
    if (!pt_kill_side(SIDE_AERA))
        return;
    r = logic_autopilot();
    if (!pt_steps(2 * SEC))
        return;
    REQUIRE(r && pt_km_to(0, NAV2_X, 0, NAV2_Z) < 5000, e_next, pt_km_to(0, NAV2_X, 0, NAV2_Z), r, 0);
}

/* ======================================================================================================== */
/* M9: the HUD (logic_hud; the host harness's screen) */

#ifdef RV_HOST
static int16_t pt_drawn(int16_t x0, int16_t y0, int16_t x1, int16_t y1)   /* pixels drawn in a box of the screen */
{
    int16_t x, y, n = 0;
    for (y = y0 < 0 ? 0 : y0; y < y1 && y < HOST_H; y++)
        for (x = x0 < 0 ? 0 : x0; x < x1 && x < HOST_W; x++)
            if (host_screen[y * HOST_W + x])
                n++;
    return n;
}
#endif

/* M9.1 */
static void t_gunsight(void)
{
#ifdef RV_HOST
    static const char __far e_none[] = "logic_hud drew nothing within 30 px of the view's middle (%d, %d): a gunsight";
    RvCamera cam;
    pt_alone(PT_PLAYER);
    if (!pt_step())
        return;
    flight_camera(&cam);
    memset(host_screen, 0, sizeof host_screen);
    hud_band(0, HOST_H, NULL, 0, 0);
    logic_hud();
    REQUIRE(pt_drawn((int16_t)(cam.cx - 30), (int16_t)(cam.cy - 30), (int16_t)(cam.cx + 31), (int16_t)(cam.cy + 31)),
            e_none, cam.cx, cam.cy, 0);
#endif
}

/* M9.2 */
static void t_brackets(void)
{
#ifdef RV_HOST
    static const char __far e_none[] = "logic_hud drew nothing round the target (its box %d px about %d, %d): "
        "brackets";
    RvCamera cam;
    EntBox b;
    pt_alone(PT_PLAYER);
    pt_ship(1, PT_ENEMY, SIDE_AERA, CTL_FROZEN, 1200, 900, 4000, TURN / 2);
    g_sys.target = 1;
    if (!pt_step())
        return;
    flight_camera(&cam);
    flight_ent_box(1, &cam, &b);
    memset(host_screen, 0, sizeof host_screen);
    hud_band(0, HOST_H, NULL, 0, 0);
    logic_hud();
    memset(host_screen + (cam.cy - 30) * HOST_W, 0, 61 * HOST_W);   /* (not the gunsight's rows) */
    REQUIRE(pt_drawn((int16_t)(b.sx - b.half - 10), (int16_t)(b.sy - b.half - 10), (int16_t)(b.sx + b.half + 11),
                     (int16_t)(b.sy + b.half + 11)), e_none, b.half, b.sx, b.sy);
#endif
}

/* ======================================================================================================== */

static const char __far n_m1_1[] = "M1.1 coast (step_motion): a ship flies along its nose at its speed, position += "
    "velocity";
static const char __far n_m1_2[] = "M1.2 throttle (step_motion): the speed builds to the throttle at the type's accel, "
    "and falls to 0";
static const char __far n_m1_3[] = "M1.3 top-speed (step_motion): no faster than max_kps without the afterburner";
static const char __far n_m1_4[] = "M1.4 yaw (step_minds, step_motion): steer_x turns the nose right at yaw_dps";
static const char __far n_m1_5[] = "M1.5 pitch (step_minds, step_motion): steer_y pulls the nose up at pitch_dps";
static const char __far n_m1_6[] = "M1.6 roll (step_minds, step_motion): roll lifts the right wing at roll_dps";
static const char __far n_m1_7[] = "M1.7 steer-steps (step_minds): a steer of 3 turns a third as fast as 9";
static const char __far n_m1_8[] = "M1.8 aerobatics (step_motion): 20 s of every turn keeps the frame square";
static const char __far n_m1_9[] = "M1.9 afterburner (step_motion): past max_kps toward ab_kps while lit, back after";
static const char __far n_m1_10[] = "M1.10 puppet (step_minds, step_motion): CTL_PUPPET only coasts";
static const char __far n_m1_11[] = "M1.11 frozen (step_minds, step_motion): CTL_FROZEN stays put";
static const char __far n_m2_1[] = "M2.1 fire-call (fireGunFromShip): one shot from the gun's mount, along the nose, "
    "at the weapon's speed";
static const char __far n_m2_2[] = "M2.2 never-refuses (fireGunFromShip): it fires with no energy and cooling";
static const char __far n_m2_3[] = "M2.3 cooldown (fireGunFromShip, step_timers): GUN_STATE counts the refire down";
static const char __far n_m2_4[] = "M2.4 energy (fireGunFromShip, step_timers): a shot costs gun energy, which "
    "recharges";
static const char __far n_m2_5[] = "M2.5 trigger (step_fire): the trigger fires every gun at its refire";
static const char __far n_m2_6[] = "M2.6 safety (step_fire): nothing fires with the master arm off";
static const char __far n_m2_7[] = "M2.7 broken-gun (step_fire, maybe_fire_all_guns): a destroyed gun stays quiet";
static const char __far n_m2_8[] = "M2.8 all-guns (maybe_fire_all_guns): every ready gun once, no missiles";
static const char __far n_m2_9[] = "M2.9 range (step_shots): shots end at their range";
static const char __far n_m3_1[] = "M3.1 shield-front (do_damage): a front hit takes the front shield";
static const char __far n_m3_2[] = "M3.2 shield-rear (do_damage): a hit from behind takes the rear shield";
static const char __far n_m3_3[] = "M3.3 armour (do_damage): with the shields down, the armour on that side";
static const char __far n_m3_4[] = "M3.4 armour-sides (do_damage): left hits the left armour, right the right";
static const char __far n_m3_5[] = "M3.5 hull (do_damage): through the armour, the hull (damagePoints)";
static const char __far n_m3_6[] = "M3.6 big-hit (do_damage, step_deaths): a hit past the hull kills, nothing wraps";
static const char __far n_m3_7[] = "M3.7 no-wrap (do_damage): many small hits, nothing below 0, the dead stay dead";
static const char __far n_m3_8[] = "M3.8 player-safe (do_damage): playerCanBeDamaged 0 protects slot 0";
static const char __far n_m3_9[] = "M3.9 invulnerable (do_damage): rvFlags 1 protects a ship";
static const char __far n_m3_10[] = "M3.10 any-source (do_damage): the same hit does the same whatever src";
static const char __far n_m3_11[] = "M3.11 shots-hit (step_shots): gunfire hurts what it meets, src a bolt of the "
    "shooter's";
static const char __far n_m3_12[] = "M3.12 recharge (step_timers): shields come back, armour does not";
static const char __far n_m4_1[] = "M4.1 kill (step_deaths, despawn): dying (9), exploding a while, then gone";
static const char __far n_m4_2[] = "M4.2 dying-quiet (step_fire, AI): a dying ship fires nothing";
static const char __far n_m4_3[] = "M4.3 player-dies (step_deaths, logic_end): missionStatus 4, the mission lost";
static const char __far n_m5_1[] = "M5.1 ai-thunks (step_minds): AI fighters think through aiFighterThink, capital "
    "ships through aiShipThink";
static const char __far n_m5_2[] = "M5.2 ai-turns (aiFighterThink): an enemy turns to face the player";
static const char __far n_m5_3[] = "M5.3 ai-closes (aiFighterThink): an enemy closes in";
static const char __far n_m5_4[] = "M5.4 ai-fires (aiFighterThink, step_fire): an enemy shoots the player";
static const char __far n_m5_5[] = "M5.5 ai-aims (aiFighterThink): it turns to bring its guns to bear before it fires";
static const char __far n_m5_6[] = "M5.6 ai-limits (aiFighterThink, step_motion): the AI turns and flies within its "
    "type";
static const char __far n_m5_7[] = "M5.7 wingman-fights (aiFighterThink): a wingman attacks the enemy";
static const char __far n_m5_8[] = "M5.8 formation (aiFighterThink): a wingman keeps with its leader";
static const char __far n_m6_1[] = "M6.1 ram (step_collisions): ships inside one another are pushed apart, both hurt";
static const char __far n_m6_2[] = "M6.2 no-ghosts (step_collisions): a ship flown at another does not pass through";
static const char __far n_m7_1[] = "M7.1 launch (fireGunFromShip): a launcher puts a missile in a ship slot";
static const char __far n_m7_2[] = "M7.2 missile-key (step_fire): Enter launches one, as many as the rounds";
static const char __far n_m7_3[] = "M7.3 missile-flies (step_missiles): a dumbfire flies straight at its speed";
static const char __far n_m7_4[] = "M7.4 missile-hits (step_missiles): a dumbfire hurts the ship ahead and ends";
static const char __far n_m7_5[] = "M7.5 missile-homes (step_missiles): a friend-or-foe turns onto its target";
static const char __far n_m7_6[] = "M7.6 missile-ends (step_missiles): a missile ends at its range";
static const char __far n_m8_1[] = "M8.1 start (logic_start): the player in slot 0, the wingman, no enemies yet";
static const char __far n_m8_2[] = "M8.2 nav1 (step_mission, enterNavPoint): nav 1's ships come when the player does";
static const char __far n_m8_3[] = "M8.3 nav-once (step_mission): and only once";
static const char __far n_m8_4[] = "M8.4 won (step_mission, logic_end): every nav flown and every enemy killed: "
    "victoryPoints nonzero";
static const char __far n_m8_5[] = "M8.5 skipped (logic_start, logic_end): landing without the patrol: lost";
static const char __far n_m8_6[] = "M8.6 enemies-left (logic_end): the navs flown, the enemies alive: lost";
static const char __far n_m8_7[] = "M8.7 layer-spawn (outerSpawnShipEntity, plumbing): a mission ship at its nav point";
static const char __far n_m8_8[] = "M8.8 autopilot (logic_autopilot): on to the next nav point, never with enemies "
    "near";
static const char __far n_m9_1[] = "M9.1 gunsight (logic_hud): a mark at the view's middle";
static const char __far n_m9_2[] = "M9.2 brackets (logic_hud): marks round the target";

static const PtTest __far k_tests[] = {
    { n_m1_1, t_coast, 0 }, { n_m1_2, t_throttle, 0 }, { n_m1_3, t_top_speed, 0 }, { n_m1_4, t_yaw, 0 },
    { n_m1_5, t_pitch, 0 }, { n_m1_6, t_roll, 0 }, { n_m1_7, t_steer_steps, 0 }, { n_m1_8, t_aerobatics, 0 },
    { n_m1_9, t_afterburner, 0 }, { n_m1_10, t_puppet, 0 }, { n_m1_11, t_frozen, 0 },
    { n_m2_1, t_fire_call, 0 }, { n_m2_2, t_never_refuses, 0 }, { n_m2_3, t_cooldown, 0 }, { n_m2_4, t_energy, 0 },
    { n_m2_5, t_trigger, 0 }, { n_m2_6, t_safety, 0 }, { n_m2_7, t_broken_gun, 0 }, { n_m2_8, t_all_guns, 0 },
    { n_m2_9, t_range, 0 },
    { n_m3_1, t_shield_front, 0 }, { n_m3_2, t_shield_rear, 0 }, { n_m3_3, t_armour, 0 },
    { n_m3_4, t_armour_sides, 0 }, { n_m3_5, t_hull, 0 }, { n_m3_6, t_big_hit, 0 }, { n_m3_7, t_no_wrap, 0 },
    { n_m3_8, t_player_safe, 0 }, { n_m3_9, t_invulnerable, 0 }, { n_m3_10, t_any_source, 0 },
    { n_m3_11, t_shots_hit, 0 }, { n_m3_12, t_recharge, 0 },
    { n_m4_1, t_kill, 0 }, { n_m4_2, t_dying_quiet, 0 }, { n_m4_3, t_player_dies, 0 },
    { n_m5_1, t_ai_thunks, 1 }, { n_m5_2, t_ai_turns, 0 }, { n_m5_3, t_ai_closes, 0 }, { n_m5_4, t_ai_fires, 0 },
    { n_m5_5, t_ai_aims, 1 }, { n_m5_6, t_ai_limits, 0 }, { n_m5_7, t_wingman_fights, 0 },
    { n_m5_8, t_formation, 0 },
    { n_m6_1, t_ram, 0 }, { n_m6_2, t_no_ghosts, 0 },
    { n_m7_1, t_launch, 0 }, { n_m7_2, t_missile_key, 0 }, { n_m7_3, t_missile_flies, 0 },
    { n_m7_4, t_missile_hits, 0 }, { n_m7_5, t_missile_homes, 0 }, { n_m7_6, t_missile_ends, 0 },
    { n_m8_1, t_start, 0 }, { n_m8_2, t_nav1, 0 }, { n_m8_3, t_nav_once, 0 }, { n_m8_4, t_won, 0 },
    { n_m8_5, t_skipped, 0 }, { n_m8_6, t_enemies_left, 0 }, { n_m8_7, t_layer_spawn, 0 }, { n_m8_8, t_autopilot, 0 },
    { n_m9_1, t_gunsight, 1 }, { n_m9_2, t_brackets, 1 },
};

/* --- the run --- */

/* the world's hash: the scalars of play, the slot arrays, the mission tables, the shots.  (Its pointer is near: a
 * fixed-block address is a constant, and Watcom makes a constant near pointer passed as a far one segment 0, not
 * DS: the hash of the interrupt table.  docs/gameplay.md, "Watcom traps") */
static uint32_t fnv(uint32_t h, const uint8_t *p, uint16_t n)
{
    for (; n; n--)
        h = (h ^ *p++) * UINT32_C(16777619);
    return h;
}

static uint32_t pt_hash(void)
{
    uint32_t h = UINT32_C(2166136261);
    int16_t i;
    h = fnv(h, (const uint8_t *)&missionStatus, (uint16_t)((uint8_t *)&victoryPoints + 2 - (uint8_t *)&missionStatus));
    h = fnv(h, (const uint8_t *)maybeEntityType, (uint16_t)(RV_FIXED_BYTES - ((uint8_t *)maybeEntityType - RV_AT(uint8_t *, 0))));
    for (i = 0; i < shots_count(); i++) {
        const Shot __far *s = shot(i);
        int32_t w[12];
        w[0] = s->x; w[1] = s->y; w[2] = s->z; w[3] = s->vx; w[4] = s->vy; w[5] = s->vz;
        w[6] = s->hx; w[7] = s->hy; w[8] = s->hz;
        w[9] = s->weapon; w[10] = s->damage; w[11] = (int32_t)((uint32_t)s->life << 16 | (uint32_t)s->owner << 8 | s->look);
        h = fnv(h, (const uint8_t *)w, sizeof w);
    }
    return h;
}

/* the world's parts, a step's line ("steps"): where the DOS build first parts from the host's */
static void pt_parts(void)
{
    static const char __far s_step[] = " step %d ";
    static const uint16_t __far k_at[] = { 0x0600, 0x0B00, 0x1100, 0x1700, 0x2900, 0x2D00, 0x3800, 0x5280, 0x6400,
                                           0x8000 };
    static const char __far k_names[] = "kinds positions velocities frames speeds hurts guns minds mission ";
    const char __far *n = k_names;
    int16_t k;
    uint32_t h;
    ln_s(s_pre);
    ln_id();
    ln_f(s_step, frameCounter, 0, 0);
    ln_x(pt_hash());
    for (k = 0; k + 1 < (int16_t)(sizeof k_at / sizeof k_at[0]); k++) {
        h = fnv(UINT32_C(2166136261), RV_AT(const uint8_t *, 0) + k_at[k], (uint16_t)(k_at[k + 1] - k_at[k]));
        ln_c(' ');
        while (*n != ' ')
            ln_c(*n++);
        n++;
        ln_c('=');
        ln_x(h);
    }
    ln_end();
}

/* the test's id (its name to the space) matches one of `only`'s (commas or spaces between): "M2" takes M2.*,
 * "M2.3" that one */
static int16_t pt_wanted(const char __far *name, const char *only)
{
    const char *o = only;
    int16_t k;
    if (!only || !*only)
        return 1;
    while (*o) {
        while (*o == ',' || *o == ' ')
            o++;
        for (k = 0; o[k] && o[k] != ',' && o[k] != ' '; k++)
            ;
        if (k && !_fmemcmp(name, o, k) && (name[k] == ' ' || name[k] == '.' || o[k - 1] == '.'))
            return 1;
        o += k;
    }
    return 0;
}

int16_t playtest_run(const char *only)
{
    static const char __far s_pass[] = "PASS ", __far s_fail[] = "FAIL ", __far s_sp[] = " ";
#ifndef RV_HOST
    static const char __far s_skip[] = "SKIP -------- ", __far s_host[] = " (the host harness only)";
#endif
    static const char __far s_sum[] = "%d of %d pass";
    static const char __far s_ms[] = "  M%d %d/%d";
    static const char __far s_start[] = "the play tests: %d of them; each line PASS or FAIL, the world's hash, the test";
    static const char __far s_steps[] = "steps";
    int16_t i, n = (int16_t)(sizeof k_tests / sizeof k_tests[0]), ran = 0, passed = 0, m, mr[10], mp[10];
    uint32_t h;
    for (m = 0; m < 10; m++)
        mr[m] = mp[m] = 0;
    g_steps = 0;
    if (only)
        for (i = 0; only[i]; i++)
            if ((i == 0 || only[i - 1] == ',' || only[i - 1] == ' ') && !_fmemcmp(only + i, s_steps, 5))
                g_steps = 1;
    camp_new();
    ln_s(s_pre);
    ln_f(s_start, n, 0, 0);
    ln_end();
    for (i = 0; i < n; i++) {
        if (!pt_wanted(k_tests[i].name, only))
            continue;
#ifndef RV_HOST
        if (k_tests[i].host_only) {
            ln_s(s_pre);
            ln_s(s_skip);
            ln_s(k_tests[i].name);
            ln_s(s_host);
            ln_end();
            continue;
        }
#endif
        g_index = i;
        g_name = k_tests[i].name;
        g_failed = 0;
        k_tests[i].fn();
        h = pt_hash();
        m = (int16_t)(k_tests[i].name[1] - '0');
        ran++;
        mr[m % 10]++;
        if (!g_failed) {
            passed++;
            mp[m % 10]++;
        }
        ln_s(s_pre);
        ln_s(g_failed ? s_fail : s_pass);
        ln_x(h);
        ln_s(s_sp);
        ln_s(k_tests[i].name);
        ln_end();
    }
    shots_flight_end();
    collide_flight_end();
    ln_s(s_pre);
    ln_f(s_sum, passed, ran, 0);
    for (m = 1; m < 10; m++)
        if (mr[m])
            ln_f(s_ms, m, mp[m], mr[m]);
    ln_end();
    return (int16_t)(ran - passed);
}

#ifdef PLAYTEST_MAIN
/* the host's: build/hostNN/playtest [--data DIR] [--log FILE] [IDS...] (host/build.sh builds it beside rvsim) */
#include <unistd.h>

int main(int argc, char **argv)
{
    static char only[512], dir[1024];
    const char *data = 0, *log = "playtest.log";
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data = argv[++i];
        else if (!strcmp(argv[i], "--log") && i + 1 < argc)
            log = argv[++i];
        else if (!strcmp(argv[i], "--list")) {
            for (i = 0; i < (int)(sizeof k_tests / sizeof k_tests[0]); i++)
                printf("%s%s\n", k_tests[i].name, k_tests[i].host_only ? " (the host harness only)" : "");
            return 0;
        } else if (argv[i][0] == '-') {
            fputs("usage: playtest [--data DIR] [--log FILE] [--list] [ID ...]   (M2 runs M2.*, M2.3 that one;\n"
                  "       the game's log and the RULE lines go to the log, playtest.log in the data directory)\n",
                  stderr);
            return 2;
        } else if (strlen(only) + strlen(argv[i]) + 2 < sizeof only) {
            strcat(only, argv[i]);
            strcat(only, ",");
        }
    }
    if (!data) {                        /* beside the program (host/build.sh puts the data there) */
        const char *slash = strrchr(argv[0], '/');
        if (slash && (size_t)(slash - argv[0]) < sizeof dir) {
            memcpy(dir, argv[0], (size_t)(slash - argv[0]));
            data = dir;
        }
    }
    if (data && chdir(data)) {
        fprintf(stderr, "playtest: no such data directory: %s\n", data);
        return 2;
    }
    if ((host_log = fopen(log, "w")) == 0) {
        fprintf(stderr, "playtest: cannot write %s\n", log);
        return 2;
    }
    host_check_src = 1;                 /* (every do_damage tried again with src 0xFFFF: the layer's replay) */
    if (!camp_load_data("RVGAME.DAT") || !collide_reserve()) {
        fprintf(stderr, "playtest: no RVGAME.DAT or SHAPES.DAT here (--data DIR: build/host32 has them)\n");
        return 2;
    }
    i = playtest_run(only);
    xms_free_level(XMS_RESIDENT);
    fclose(host_log);
    return i ? 1 : 0;
}
#endif
