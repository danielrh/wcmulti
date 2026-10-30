/* host.c: what the game's logic (src/logic.c and the modules it shares) needs from the DOS engine, done on the host
 * (docs/logic.md, "The host harness"): DGROUP and the fixed block (rv_dgroup), the log, the failure stop, XMS
 * (blocks of the host's memory, bounds checked), the screen (host_screen), the clock, the bolts' and balls' pools
 * (empty: sim.c draws the shots itself), the cockpit's systems, the camera, and at each thunk the checks of the
 * rules the layer needs. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"
#include "world.h"
#include "shots.h"
#include "flight.h"
#include "panel.h"
#include "bolts.h"
#include "balls.h"
#include "host.h"

uint8_t host_screen[HOST_H * HOST_W];
unsigned char rv_filebuf[1024];
ShipSystems g_sys;
FILE *host_log;
int host_trace, host_check_src, host_strict;
uint32_t host_ticks, host_rules_broken;

/* the sanitizers stop at the first report with an abort (a debugger, or rr, is then at the bug):
 * UBSAN_OPTIONS=halt_on_error=0 goes on past them */
#ifndef HOST_SYMBOLIZER
#define HOST_SYMBOLIZER ""
#endif
const char *__ubsan_default_options(void);
const char *__ubsan_default_options(void)
{
    return "print_stacktrace=1:halt_on_error=1:abort_on_error=1" HOST_SYMBOLIZER;
}
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "abort_on_error=1" HOST_SYMBOLIZER; }

/* --- the log --- */

/* a Watcom format as the host's printf takes it: l (a long: an int32_t, an int on every host) and F, N (a far, a
 * near pointer) dropped */
static void vlog(const char *fmt, va_list ap)
{
    char f[512];
    size_t i = 0;
    while (*fmt && i < sizeof f - 8) {
        if ((f[i++] = *fmt++) != '%')
            continue;
        while (*fmt && strchr("-+ #0123456789.*", *fmt) && i < sizeof f - 8)
            f[i++] = *fmt++;
        while (*fmt == 'l' || *fmt == 'F' || *fmt == 'N')
            fmt++;
        if (*fmt)
            f[i++] = *fmt++;
    }
    f[i] = 0;
    fprintf(host_log ? host_log : stderr, "%6u  ", (unsigned)frameCounter);
    vfprintf(host_log ? host_log : stderr, f, ap);
    fputc('\n', host_log ? host_log : stderr);
}

void rv_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void rv_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void rv_fail(const char *file, int line, const char *what, long value, long limit)
{
    fflush(host_log);
    fprintf(stderr, "rv_fail: %s:%d: %s (%ld, the limit %ld) at step %u\n", file, line, what, value, limit,
            (unsigned)frameCounter);
    abort();
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-overflow"   /* (gcc 15 with UBSan and _FORTIFY_SOURCE sees a null format
                                           in vfprintf below that no caller passes) */
#endif
char host_last_rule[256];

void host_rule(const char *fmt, ...)
{
    va_list ap;
    FILE *f = host_log ? host_log : stderr;
    host_rules_broken++;
    va_start(ap, fmt);
    vsnprintf(host_last_rule, sizeof host_last_rule, fmt, ap);
    va_end(ap);
    fprintf(f, "%6u  RULE: ", (unsigned)frameCounter);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    if (host_strict) {
        fflush(f);
        abort();
    }
}

/* the play's randomness is game_rand's (randomSeed, which the layer re-seeds before a replayed spawn or hit) */
int rand(void)
{
    host_rule("rand() called: play's randomness is game_rand()'s");
    return game_rand();
}

void srand(unsigned seed)
{
    (void)seed;
    host_rule("srand() called: play's randomness is game_rand()'s (randomSeed)");
}

/* --- XMS: blocks of the host's memory, every move bounds checked --- */

static struct { uint8_t *p; unsigned long n; int level; } g_xms[XMS_MAX_BLOCKS];
static unsigned long g_xms_used;

int xms_init(unsigned long min_kb, unsigned long max_kb) { (void)min_kb; (void)max_kb; return 1; }
unsigned long xms_free_kb(void) { return 40UL * 1024 - g_xms_used / 1024; }   /* (about a flight's on DOS) */

XmsBlock xms_alloc(unsigned long bytes, int level)
{
    int b;
    for (b = 1; b < XMS_MAX_BLOCKS; b++)
        if (!g_xms[b].p) {
            if ((g_xms[b].p = malloc(bytes ? bytes : 1)) == 0)
                return 0;
            g_xms[b].n = bytes;
            g_xms[b].level = level;
            g_xms_used += bytes;
            return (XmsBlock)b;
        }
    return 0;
}

void xms_free_level(int level)
{
    int b;
    for (b = 1; b < XMS_MAX_BLOCKS; b++)
        if (g_xms[b].p && g_xms[b].level >= level) {
            free(g_xms[b].p);
            g_xms_used -= g_xms[b].n;
            g_xms[b].p = 0;
        }
}

static uint8_t *xms_at(XmsBlock b, unsigned long off, unsigned long n)
{
    RV_CHECK(b, XMS_MAX_BLOCKS);
    RV_ASSERT(g_xms[b].p != 0);
    if (off > g_xms[b].n || n > g_xms[b].n - off)
        rv_fail(__FILE__, __LINE__, "an XMS move past its block's end", (long)(off + n), (long)g_xms[b].n);
    return g_xms[b].p + off;
}

int xms_put(XmsBlock b, unsigned long off, const void *src, unsigned long n)
{
    memcpy(xms_at(b, off, n), src, n);
    return 1;
}

int xms_get(XmsBlock b, unsigned long off, void *dst, unsigned long n)
{
    memcpy(dst, xms_at(b, off, n), n);
    return 1;
}

/* --- the screen and the clock --- */

unsigned char *vid_row(int y)
{
    RV_CHECK(y, HOST_H);
    return host_screen + y * HOST_W;
}

unsigned long ticks(void) { return host_ticks; }

/* --- the bolts' and balls' pools: none (shots_flight_start(0) asks for none; sim.c draws the shot list) --- */

int bolts_init(int want, unsigned keep_kb) { (void)want; (void)keep_kb; return 0; }
void bolts_free(void) {}
LzBolt *bolts_pool(void) { return 0; }
void bolts_set_count(int n) { (void)n; }
void bolts_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1, int line16)
{
    (void)px; (void)py; (void)pz; (void)cam; (void)cx; (void)cy; (void)focal; (void)y0; (void)y1; (void)line16;
}
void bolts_draw_far(int y0, int y1, const short *ext, int ext_y0, int ext_rows)
{
    (void)y0; (void)y1; (void)ext; (void)ext_y0; (void)ext_rows;
}
void bolts_draw_near(int y0, int y1, const short *ext, int ext_y0, int ext_rows)
{
    (void)y0; (void)y1; (void)ext; (void)ext_y0; (void)ext_rows;
}
int balls_init(int want, unsigned keep_kb) { (void)want; (void)keep_kb; return 0; }
void balls_free(void) {}
LzBall *balls_pool(void) { return 0; }
void balls_set_count(int n) { (void)n; }
void balls_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1,
                 unsigned anim)
{
    (void)px; (void)py; (void)pz; (void)cam; (void)cx; (void)cy; (void)focal; (void)y0; (void)y1; (void)anim;
}
void balls_draw_far(int y0, int y1, const short *ext, int ext_y0, int ext_rows)
{
    (void)y0; (void)y1; (void)ext; (void)ext_y0; (void)ext_rows;
}
void balls_draw_near(int y0, int y1, const short *ext, int ext_y0, int ext_rows)
{
    (void)y0; (void)y1; (void)ext; (void)ext_y0; (void)ext_rows;
}

/* --- the camera and a ship on the screen (flight.c's, without the cockpit: the view is the whole screen, and a
 * ship's box is the steady square, as the game gives it for a ship whose sprite is not drawn) --- */

void flight_camera(RvCamera *c)
{
    c->pos = *(const LfVec *)&gPositionVector[0];
    c->b.front = *(const LfVec *)&gOrientationFrontVector[0];
    c->b.right = *(const LfVec *)&gOrientationRightVector[0];
    c->b.up = *(const LfVec *)&gOrientationUpVector[0];
    c->cx = HOST_W / 2;
    c->cy = HOST_H / 2;
    c->focal = 400;
    c->vx0 = 0;
    c->vx1 = HOST_W;
    c->vy0 = 0;
    c->vy1 = HOST_H;
}

#define FULL_SIZE_UNITS (INT32_C(256) * 2500)
static int16_t scale_of(const LfVec *rel)
{
    int32_t x = rel->x / 64, y = rel->y / 64, z = rel->z / 64, k = 64, d, sc;
    while (x > 37000 || x < -37000 || y > 37000 || y < -37000 || z > 37000 || z < -37000) {
        x /= 4;
        y /= 4;
        z /= 4;
        k *= 4;
    }
    d = (int32_t)lf_sqrt((uint32_t)(x * x) + (uint32_t)(y * y) + (uint32_t)(z * z)) * k;
    sc = d ? FULL_SIZE_UNITS * 256 / d : 512;
    return (int16_t)(sc > 512 ? 512 : sc);
}

int16_t flight_ent_box(int16_t slot, const RvCamera *cam, EntBox *o)
{
    const RvShipType *t;
    LfVec rel;
    int32_t rx, ry, rz, ax, ay;
    int16_t sc;
    memset(o, 0, sizeof *o);
    if (slot < 0 || slot > RV_MAX_SHIP_SLOT || (maybeEntityType[slot] != ET_SHIP && maybeEntityType[slot] != ET_CAPSHIP))
        return 0;
    rel.x = gPositionVector[slot].x - cam->pos.x;
    rel.y = gPositionVector[slot].y - cam->pos.y;
    rel.z = gPositionVector[slot].z - cam->pos.z;
    rx = rel.x >> 4;
    ry = rel.y >> 4;
    rz = rel.z >> 4;
    o->X = (rx * cam->b.right.x + ry * cam->b.right.y + rz * cam->b.right.z) / 256;
    o->Y = (rx * cam->b.up.x + ry * cam->b.up.y + rz * cam->b.up.z) / 256;
    o->Z = (rx * cam->b.front.x + ry * cam->b.front.y + rz * cam->b.front.z) / 256;
    for (ax = o->X, ay = o->Y; ax > 65535 || ax < -65535 || ay > 65535 || ay < -65535; ax /= 2, ay /= 2)
        ;
    o->dir = lf_atan2(ax, ay);
    if (o->Z < 16)
        return 1;
    o->ahead = 1;
    ax = cam->cx + o->X * cam->focal / o->Z;
    ay = cam->cy - o->Y * cam->focal / o->Z;
    o->sx = (int16_t)(ax < -16000 ? -16000 : ax > 16000 ? 16000 : ax);
    o->sy = (int16_t)(ay < -16000 ? -16000 : ay > 16000 ? 16000 : ay);
    sc = (int16_t)((int32_t)scale_of(&rel) * cam->focal / 400);
    t = slot_type(slot);
    o->half = (int16_t)(t ? (int32_t)t->lod0_px * sc / 512 : 4);
    if (o->half < 2)
        o->half = 2;
    o->x0 = o->sx - o->half;
    o->y0 = o->sy - o->half;
    o->x1 = o->sx + o->half;
    o->y1 = o->sy + o->half;
    o->visible = o->x1 > cam->vx0 && o->x0 < cam->vx1 && o->y1 > cam->vy0 && o->y0 < cam->vy1;
    return 1;
}

/* --- the thunks: a trace, and the rules the layer needs (docs/logic.md "The multiplayer rules") --- */

/* the arrays that say what a hit did (do_damage's amount: the layer replays a hit with another src, and its amount
 * must come out the same) */
static const struct { const uint8_t *at; uint16_t bytes; const char *name; } k_damage[] = {
#define D(a) { (const uint8_t *)(a), (uint16_t)(RV_SLOTS * a##_SIZE), #a }
    D(curShield), D(armorState), D(damagePoints), D(coreHp), D(gunDamage), D(engineFlag), D(gunTable),
#undef D
};

static uint8_t g_before[RV_FIXED_BYTES], g_alt[RV_FIXED_BYTES];
static struct { uint16_t shots; int16_t alt; uint16_t alt_ret; } g_call[16];
static int16_t g_depth, g_in_check;

/* the thunks' calls counted (the play tests' questions: was the AI asked through its thunk, was a death ended
 * through despawn): by thunk, by its first and second argument's value (a slot; RV_SLOTS: any other value) */
#define HOST_FNS 64
static uint32_t g_count[HOST_FNS][2][RV_SLOTS + 1], g_count_all[HOST_FNS];
uint32_t host_bolt_hits[RV_SLOTS];

void host_counts_reset(void)
{
    memset(g_count, 0, sizeof g_count);
    memset(g_count_all, 0, sizeof g_count_all);
    memset(host_bolt_hits, 0, sizeof host_bolt_hits);
}

uint32_t host_calls(uint16_t fn, int16_t arg, int16_t value)
{
    uint16_t i = (uint16_t)((fn - RV_FN_rvHello) / 5);
    if (i >= HOST_FNS)
        return 0;
    if (arg < 0)
        return g_count_all[i];
    return g_count[i][arg & 1][value >= 0 && value < RV_SLOTS ? value : RV_SLOTS];
}

static void count_call(uint16_t fn, int16_t nargs, const uint16_t *a)
{
    uint16_t i = (uint16_t)((fn - RV_FN_rvHello) / 5), k;
    if (i >= HOST_FNS)
        return;
    g_count_all[i]++;
    for (k = 0; k < 2 && k < (uint16_t)nargs; k++)
        g_count[i][k][a[k] < RV_SLOTS ? a[k] : RV_SLOTS]++;
}

static int16_t dying(const uint8_t *fixed, uint16_t s)
{
    return ((const uint16_t *)(fixed + ((uint8_t *)shipAiState - rv_dgroup)))[s] == 9;
}

void host_thunk_enter(uint16_t fn, const char *name, int16_t nargs, const uint16_t *a);
void host_thunk_leave(uint16_t fn, const char *name, int16_t nargs, const uint16_t *a, uint16_t ret);

FILE *host_shiptrace;

void host_thunk_enter(uint16_t fn, const char *name, int16_t nargs, const uint16_t *a)
{
    int16_t d = g_depth++;
    if (host_shiptrace && !g_in_check) {
        if (fn == RV_FN_fireGunFromShip)
            fprintf(host_shiptrace, "F %u %u %u 0\n", frameCounter, a[0], a[1]);
        else if (fn == RV_FN_do_damage)
            fprintf(host_shiptrace, "D %u %u %u %u\n", frameCounter, a[0], a[1], a[2]);
    }
    if (host_trace && !g_in_check) {
        char args[64] = "";
        int16_t i;
        for (i = 0; i < nargs; i++)
            sprintf(args + strlen(args), "%s%u", i ? ", " : "", a[i]);
        rv_log("thunk %s(%s)", name, args);
    }
    if (d >= (int16_t)RV_COUNT(g_call))
        return;
    g_call[d].alt = 0;
    g_call[d].shots = (uint16_t)shots_count();
    if (g_in_check)
        return;
    count_call(fn, nargs, a);
    if (fn == RV_FN_do_damage) {
        uint16_t src = a[0], dst = a[1];
        if (!RV_SLOT_OK(dst))
            host_rule("do_damage: dst %u is not a slot", dst);
        if (src != 0xFFFF) {
            if (!RV_SLOT_OK(src))
                host_rule("do_damage: src %u is neither a slot nor 0xFFFF", src);
            else if (maybeEntityType[src] == ET_BOLT) {
                if (!slot_is_ship(parentShipForShip[src]))
                    host_rule("do_damage: src %u is a bolt whose parent (%u) is no ship: the layer finds the shooter "
                              "there", src, parentShipForShip[src]);
                else if (RV_SLOT_OK(dst) && parentShipForShip[src] != dst)
                    host_bolt_hits[parentShipForShip[src]]++;
            } else if (!slot_is_ship(src) && maybeEntityType[src] != ET_MISSILE
                       && !(src >= RV_SLOTS - 10 && src < RV_CAMERA_SLOT))
                host_rule("do_damage: src %u (type %u) is no bolt, ship, missile or field hazard", src,
                          maybeEntityType[src]);
        }
        if (a[3] > 0x10000 - sizeof(RvVec3))
            host_rule("do_damage: where (%u) is not a near RvVec3", a[3]);
        if (host_check_src && src != 0xFFFF && RV_SLOT_OK(dst)) {
            /* the hit again with src 0xFFFF, as another machine may replay it, on a copy of the fixed block */
            memcpy(g_before, rv_dgroup, RV_FIXED_BYTES);
            g_in_check = 1;
            g_call[d].alt_ret = do_damage_impl(0xFFFF, dst, a[2], a[3]);
            g_in_check = 0;
            memcpy(g_alt, rv_dgroup, RV_FIXED_BYTES);
            memcpy(rv_dgroup, g_before, RV_FIXED_BYTES);
            g_call[d].alt = 1;
        }
    } else if (fn == RV_FN_fireGunFromShip) {
        if (!slot_is_ship(a[0]) && maybeEntityType[a[0]] != ET_MISSILE)
            host_rule("fireGunFromShip: slot %u holds no ship", a[0]);
        else if (a[1] >= GUN_COUNT(a[0]))
            host_rule("fireGunFromShip: slot %u has %u guns, not a gun %u", a[0], GUN_COUNT(a[0]), a[1]);
    }
}

void host_thunk_leave(uint16_t fn, const char *name, int16_t nargs, const uint16_t *a, uint16_t ret)
{
    int16_t d = --g_depth;
    (void)name;
    (void)nargs;
    if (d >= (int16_t)RV_COUNT(g_call) || g_in_check)
        return;
    if (fn == RV_FN_do_damage && g_call[d].alt) {
        uint16_t k;
        if ((ret != 0) != (g_call[d].alt_ret != 0))
            host_rule("do_damage(%u, %u, %u): returned %u, and %u with src 0xFFFF", a[0], a[1], a[2], ret,
                      g_call[d].alt_ret);
        for (k = 0; k < RV_COUNT(k_damage); k++)
            if (memcmp(k_damage[k].at, g_alt + (k_damage[k].at - rv_dgroup), k_damage[k].bytes)) {
                host_rule("do_damage(%u, %u, %u): %s comes out different with src 0xFFFF (the layer replays hits "
                          "with another src: the amount must not depend on it)", a[0], a[1], a[2], k_damage[k].name);
                break;
            }
        if (RV_SLOT_OK(a[1]) && dying(rv_dgroup, a[1]) != dying(g_alt, a[1]))
            host_rule("do_damage(%u, %u, %u): the ship dies with one src and not the other", a[0], a[1], a[2]);
    } else if (fn == RV_FN_fireGunFromShip && RV_SLOT_OK(a[0]) && a[1] < GUN_COUNT(a[0])) {
        if (ret != 0xFFFF && (!RV_SLOT_OK(ret) || maybeEntityType[ret] != ET_MISSILE))
            host_rule("fireGunFromShip(%u, %u) returned %u: a missile's slot or 0xFFFF", a[0], a[1], ret);
        if (ret == 0xFFFF && (uint16_t)shots_count() == g_call[d].shots && g_call[d].shots < SHOTS_MAX)
            host_rule("fireGunFromShip(%u, %u) put no shot in the world: the layer replays the call on the other "
                      "machines and needs it to fire whatever the cooldown and the energy", a[0], a[1]);
    }
}

/* --- the world's invariants after a step: each reported once a slot until the slot is emptied --- */

enum { W_FRAME = 1, W_SQUARE = 2, W_LINK = 4, W_TYPE = 8, W_CONTROLLER = 16 };
static uint8_t g_warned[RV_SLOTS];

static int32_t len2(const RvVec3 *v) { return (v->x * v->x + v->y * v->y + v->z * v->z) / 256; }

static void warn(int16_t s, uint8_t bit, const char *what)
{
    if (g_warned[s] & bit)
        return;
    g_warned[s] |= bit;
    host_rule("slot %d: %s", s, what);
}

void host_check_world(void)
{
    int16_t s;
    for (s = 0; s < RV_SLOTS; s++) {
        uint8_t links[3];
        int16_t k;
        if (maybeEntityType[s] == ET_NONE) {
            g_warned[s] = 0;
            continue;
        }
        links[0] = parentShipForShip[s];
        links[1] = missileTarget[s];
        links[2] = rvTarget[s];
        for (k = 0; k < 3; k++)
            if (links[k] != 0xFF && links[k] >= RV_SLOTS)
                warn(s, W_LINK, "parentShipForShip, missileTarget or rvTarget is neither a slot nor 0xFF");
        if (s > RV_MAX_SHIP_SLOT || !slot_is_ship(s))
            continue;
        if (rvController[s] > CTL_FROZEN)
            warn(s, W_CONTROLLER, "rvController is no CTL_*");
        if (!slot_type(s))
            warn(s, W_TYPE, "gInstanceShipTypes is no ship type");
        /* its frame: unit length (256, within a tenth) and square (within a tenth of a turn's quarter) */
        {
            const RvVec3 *f = &gOrientationFrontVector[s], *r = &gOrientationRightVector[s],
                         *u = &gOrientationUpVector[s];
            int32_t lf = len2(f), lr = len2(r), lu = len2(u);
            if (lf < 207 || lf > 310 || lr < 207 || lr > 310 || lu < 207 || lu > 310)
                warn(s, W_FRAME, "its orientation vectors are no longer 256 long (ship_orthonormalise after turning)");
            if (labs(vec_dot(f, r)) > 26 * 256 || labs(vec_dot(f, u)) > 26 * 256 || labs(vec_dot(r, u)) > 26 * 256)
                warn(s, W_SQUARE, "its orientation vectors are no longer square (ship_orthonormalise after turning)");
        }
    }
}

/* --- the run's fingerprint --- */

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

uint32_t host_hash(void)
{
    uint32_t h = fnv(2166136261u, rv_dgroup, RV_FIXED_BYTES);
    int16_t i, n = (int16_t)shots_count();
    for (i = 0; i < n; i++) {
        const Shot *s = shot(i);
        int32_t w[12];
        w[0] = s->x; w[1] = s->y; w[2] = s->z; w[3] = s->vx; w[4] = s->vy; w[5] = s->vz;
        w[6] = s->hx; w[7] = s->hy; w[8] = s->hz;
        w[9] = s->weapon; w[10] = s->damage; w[11] = (int32_t)((uint32_t)s->life << 16 | s->owner << 8 | s->look);
        h = fnv(h, w, sizeof w);
    }
    return h;
}
