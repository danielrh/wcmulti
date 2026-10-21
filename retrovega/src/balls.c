/* balls.c: the spherical projectiles (balls.h).
 *
 * BALLS.DAT (tools/balls.py) holds, for each kind of ball and each frame of
 * its animation (a sprite set), a sprite at every diameter 1 .. dmax, d x d
 * pixels each, 0 clear.  When the game starts it is read into XMS (the
 * resident level).  A flight with balls copies every set's small sprites (up
 * to LZ_BALL_SMALL across: the far balls, nearly all of them) into a block of
 * conventional memory; once a frame rv_balls_place puts every ball through
 * the camera and points each small one at its sprite there, the big ones
 * (near, few) having theirs fetched from XMS into a buffer for the frame,
 * each set and size once.  The small ones are drawn before the ships, the big
 * ones after.  The module's data and the log's formats are far (DGROUP is
 * nearly full). */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include "rv.h"
#include "music.h"
#include "bolts.h"
#include "balls.h"

#define SUM_SQ(n) ((unsigned long)(n) * ((n) + 1) * (2 * (n) + 1) / 6)   /* the pixels of the sprites 1 .. n */
#define MAX_SETS 16
#define NEAR_BYTES 8192                 /* the frame's big sprites, fetched from XMS ... */
#define NEAR_MAX 24                     /* ... so many different ones at most */
#define FILE_HEAD 20

static XmsBlock __far g_blk;            /* BALLS.DAT's sprites */
static int __far g_kinds, __far g_dmax, __far g_sets;
static unsigned long __far g_set_bytes; /* SUM_SQ(dmax) */
static LzBallKind __far g_kind[BALL_KINDS];
static const short __far g_radius16[BALL_KINDS] = { 24, 40, 48, 40 };   /* their radii, sixteenths of a km */
static unsigned char __far *__far g_small;   /* the small sprites (LZ_BALL_SMALL_SET a set) */
static unsigned char __far *__far g_near;    /* the frame's big ones */
static struct { unsigned char set, d; unsigned at; } __far g_near_of[NEAR_MAX];
static int __far g_nnear;
static unsigned __far g_near_used;
static LzBall __far *__far g_b;
static LzBallOut __far *__far g_o;
static int __far g_cap, __far g_n, __far g_nsmall, __far g_nbig;
static LzCam __far g_cam;
static unsigned long __far g_t_place, __far g_t_far, __far g_t_near;

int balls_reserve(void)
{
    static const char __far s_magic[] = "RVBALLS", __far s_none[] = "balls: no BALLS.DAT",
        __far s_other[] = "balls: BALLS.DAT is not this build's",
        __far s_in[] = "balls: %d kinds, %d sprite sets, 1 .. %d px: %lu KB in XMS";
    FILE *f = fopen("BALLS.DAT", "rb");
    unsigned char *buf = rv_filebuf;
    unsigned long size, at;
    int k, s;
    g_blk = 0;
    if (!f) {
        rv_logf(s_none);
        return 0;
    }
    fread(buf, 1, FILE_HEAD, f);
    g_kinds = buf[10] | buf[11] << 8;
    g_dmax = buf[12] | buf[13] << 8;
    for (k = 0, s = 0; k < g_kinds && k < BALL_KINDS; k++) {
        g_kind[k].set = (unsigned char)s;
        g_kind[k].frames = buf[16 + k];
        g_kind[k].mask = (unsigned char)(buf[16 + k] - 1);
        if (buf[16 + k] & g_kind[k].mask)
            s = 999;                    /* (a power of 2, or not this build's) */
        s += buf[16 + k];
    }
    if (_fmemcmp(buf, s_magic, 8) || (buf[8] | buf[9] << 8) != 1 || g_kinds < 1 || g_kinds > BALL_KINDS
        || g_dmax < LZ_BALL_SMALL || g_dmax > 64 || s > MAX_SETS) {
        rv_logf(s_other);
        fclose(f);
        return 0;
    }
    g_sets = s;
    g_set_bytes = SUM_SQ(g_dmax);
    fseek(f, 0, SEEK_END);
    size = ftell(f) - FILE_HEAD;
    fseek(f, FILE_HEAD, SEEK_SET);
    g_blk = xms_alloc(size + 2, XMS_RESIDENT);
    if (!g_blk) {
        fclose(f);
        return 0;
    }
    for (at = 0; at < size; at += sizeof rv_filebuf) {
        unsigned part = size - at < sizeof rv_filebuf ? (unsigned)(size - at) : sizeof rv_filebuf;
        fread(buf, 1, part, f);
        xms_put(g_blk, at, buf, part);
    }
    fclose(f);
    rv_logf(s_in, g_kinds, s, g_dmax, size / 1024);
    return 1;
}

/* the small sprites' block and the big ones' buffer (a flight's) */
static int buffers(void)
{
    int i;
    if (!g_small) {
        g_small = (unsigned char __far *)_fmalloc(g_sets * LZ_BALL_SMALL_SET);
        if (!g_small)
            return 0;
        for (i = 0; i < g_sets; i++)    /* (the small sizes come first in each set) */
            xms_get(g_blk, i * g_set_bytes, g_small + i * LZ_BALL_SMALL_SET, LZ_BALL_SMALL_SET);
    }
    if (!g_near)
        g_near = (unsigned char __far *)_fmalloc(NEAR_BYTES);
    return g_near != 0;
}

void balls_free(void)
{
    if (g_b) _ffree(g_b);
    if (g_o) _ffree(g_o);
    if (g_small) _ffree(g_small);
    if (g_near) _ffree(g_near);
    g_b = 0;
    g_o = 0;
    g_small = g_near = 0;
    g_cap = g_n = g_nsmall = g_nbig = 0;
}

int balls_init(int want, unsigned keep_kb)
{
    static const char __far s_room[] = "balls: room for %d";
    unsigned long room;
    unsigned each = sizeof(LzBall) + sizeof(LzBallOut);
    balls_free();
    if (!g_blk || !buffers()) {
        balls_free();
        return 0;
    }
    room = bolts_dos_free();
    if (want > 2000)
        want = 2000;                    /* (each array within a segment) */
    room = room > (unsigned long)keep_kb * 1024 ? room - (unsigned long)keep_kb * 1024 : 0;
    if ((unsigned long)want * each > room)
        want = (int)(room / each);
    while (want >= 16) {
        g_b = (LzBall __far *)_fmalloc((size_t)want * sizeof(LzBall));
        g_o = g_b ? (LzBallOut __far *)_fmalloc((size_t)want * sizeof(LzBallOut)) : 0;
        if (g_b && g_o)
            break;
        if (g_b) _ffree(g_b);
        if (g_o) _ffree(g_o);
        g_b = 0;
        g_o = 0;
        want = want * 3 / 4;
    }
    g_cap = g_b ? want : 0;
    rv_logf(s_room, g_cap);
    return g_cap;
}

LzBall __far *balls_pool(void) { return g_b; }
int balls_count(void) { return g_n; }
void balls_set_count(int n) { g_n = n < 0 ? 0 : n > g_cap ? g_cap : n; }

void balls_move(void)
{
    LzBall __far *b = g_b;
    int i;
    for (i = g_n; i > 0; i--, b++) {
        b->x += b->vx;
        b->y += b->vy;
        b->z += b->vz;
    }
}

/* a big ball's sprite: from the frame's buffer, fetched from XMS the first time; else (the buffer full) the
 * biggest small one, centred where it was */
static const unsigned char __far *near_sprite(LzBallOut __far *o)
{
    unsigned bytes = (unsigned)o->d * o->d;
    int i;
    for (i = 0; i < g_nnear; i++)
        if (g_near_of[i].set == o->set && g_near_of[i].d == o->d)
            return g_near + g_near_of[i].at;
    if (g_nnear < NEAR_MAX && g_near_used + bytes <= NEAR_BYTES) {
        g_near_of[g_nnear].set = o->set;
        g_near_of[g_nnear].d = o->d;
        g_near_of[g_nnear].at = g_near_used;
        xms_get(g_blk, o->set * g_set_bytes + SUM_SQ(o->d - 1), g_near + g_near_used, bytes);
        g_near_used += (bytes + 1) & ~1U;
        return g_near + g_near_of[g_nnear++].at;
    }
    o->x += (o->d - LZ_BALL_SMALL) / 2;
    o->y += (o->d - LZ_BALL_SMALL) / 2;
    o->d = LZ_BALL_SMALL;
    return g_small + o->set * LZ_BALL_SMALL_SET + (unsigned)SUM_SQ(LZ_BALL_SMALL - 1);
}

/* the big ones' sprites (at the list's back) */
static void resolve(LzBallOut __far *o, int n)
{
    g_nnear = 0;
    g_near_used = 0;
    for (; n > 0; n--, o++)
        o->spr = near_sprite(o);
}

static void camera(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1)
{
    long f[3], r[3], u[3];
    int k;
    f[0] = cam->front.x; f[1] = cam->front.y; f[2] = cam->front.z;
    r[0] = cam->right.x; r[1] = cam->right.y; r[2] = cam->right.z;
    u[0] = cam->up.x; u[1] = cam->up.y; u[2] = cam->up.z;
    lz_camera(&g_cam, px, py, pz, f, r, u, cx, cy, focal, 0, BALL_ZMIN_KM, BALL_ZMAX_KM, 0, 0, y0, y1);
    for (k = 0; k < g_kinds; k++) {
        g_kind[k].rf = (long)g_radius16[k] * 256 * g_cam.f16;
        g_kind[k].z1 = g_kind[k].rf / 12;
    }
}

void balls_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1,
                 unsigned anim)
{
    unsigned long t0 = clock240();
    long r;
    g_nsmall = g_nbig = 0;
    if (!g_cap || !g_n)
        return;
    camera(px, py, pz, cam, cx, cy, focal, y0, y1);
    r = rv_balls_place(g_b, g_n, &g_cam, g_kind, g_dmax, anim, g_o, g_cap, g_small);
    g_nsmall = (int)(r & 0xFFFF);
    g_nbig = (int)(r >> 16);
    resolve(g_o + g_cap - g_nbig, g_nbig);
    g_t_place += clock240() - t0;
}

int balls_on_screen(int *small, int *big)
{
    *small = g_nsmall;
    *big = g_nbig;
    return g_nsmall + g_nbig;
}

static void draw(const LzBallOut __far *o, int n, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int run0, run1;
    if (n && vid_run(&run0, &run1) && bolts_rows(&y0, &y1, &ext, &ext_y0, ext_rows))
        rv_balls_draw(o, n, vid_row(run0), run0, y0, y1, ext, ext_y0);
}

void balls_draw_far(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    unsigned long t0 = clock240();
    draw(g_o, g_nsmall, y0, y1, ext, ext_y0, ext_rows);
    g_t_far += clock240() - t0;
}

void balls_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    unsigned long t0 = clock240();
    draw(g_o + g_cap - g_nbig, g_nbig, y0, y1, ext, ext_y0, ext_rows);
    g_t_near += clock240() - t0;
}

void balls_ticks(unsigned long out[3])
{
    out[0] = g_t_place;
    out[1] = g_t_far;
    out[2] = g_t_near;
    g_t_place = g_t_far = g_t_near = 0;
}

/* --- the checks and the timings (with balls_init's buffers) --- */

#define TEST_ROWS 40
#define TEST_BALLS 400
static unsigned long __far g_seed = 777;

static int rnd(int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 16) & 0x7FFF) % n;
}

/* balls round the camera (at the origin, looking down +z): `near` of them 120 .. 900 km out, the rest 900 ..
 * 20,000; across `spread`/1000 of their distance either way, up half that */
static void test_balls(LzBall __far *b, int n, int n_near, int spread)
{
    int i;
    for (i = 0; i < n; i++) {
        long d = i < n_near ? 120 + rnd(780) : 900 + rnd(19100);
        b[i].x = (long)(rnd(2001) - 1000) * d / 1000 * spread / 1000 * 256;
        b[i].y = (long)(rnd(2001) - 1000) * d / 1000 * spread / 2000 * 256;
        b[i].z = d * 256;
        b[i].vx = b[i].vy = b[i].vz = 0;
        b[i].kind = (unsigned char)rnd(g_kinds);
        b[i].phase = (unsigned char)rnd(4);
    }
}

static void test_camera(int y0, int y1)
{
    LfBasis cam;
    memset(&cam, 0, sizeof cam);
    cam.front.z = 256;
    cam.right.x = 256;
    cam.up.y = 256;
    camera(0, 0, 0, &cam, SCR_W / 2, SCR_H / 2, 400, y0, y1);
}

static unsigned long checksum(const unsigned char __far *band, long *drawn)
{
    unsigned long sum = 0;
    unsigned i;
    for (i = 0; i < (unsigned)TEST_ROWS * SCR_W; i++)
        if (band[i]) {
            sum += (i * 2654435761UL) ^ band[i];
            ++*drawn;
        }
    return sum;
}

long balls_selftest(void)
{
    static const char __far s_mem[] = "balls: no memory for the self-test",
        __far s_count[] = "balls: rv_balls_place gave %d small and %d big, lz_balls_place %d and %d",
        __far s_one[] = "balls: ball %d: C %d,%d d %d set %d; asm %d,%d d %d set %d",
        __far s_rows[] = "balls: THE DRAWS DISAGREE in rows %d-%d (extents %d)",
        __far s_ok[] = "balls: the asm agrees with lasers.c (%d small and %d big on screen of %d, %ld pixels compared)";
    LzBall __far *b = (LzBall __far *)_fmalloc(TEST_BALLS * sizeof(LzBall));
    LzBallOut __far *oc = (LzBallOut __far *)_fmalloc(2 * TEST_BALLS * sizeof(LzBallOut)), __far *oa = oc + TEST_BALLS;
    unsigned char __far *band = (unsigned char __far *)_fmalloc(TEST_ROWS * SCR_W);
    short __far *ext = (short __far *)_fmalloc(SCR_H * 2 * sizeof(short));
    long drawn = 0, rc, ra;
    int nsc, nbc, nsa, nba, i, w, k, bad = 0;
    if (!g_blk || !buffers() || !b || !oc || !band || !ext) {
        rv_logf(s_mem);
        bad = -2;
        goto out;
    }
    bolts_tables();
    for (k = 0; k < SCR_H; k++) {       /* an opening of uneven extents, rows 12 .. 387 */
        ext[2 * k] = (short)(30 + (k * 7) % 61);
        ext[2 * k + 1] = (short)(610 - (k * 5) % 47);
    }
    test_balls(b, TEST_BALLS, 80, 700);
    for (i = 0; i < 6; i++) {           /* some at the near plane, behind, at the screen's edges */
        b[i].z = (BALL_ZMIN_KM + rnd(5) - 2) * 256L;
        b[6 + i].z = -256L * (100 + rnd(900));
        b[12 + i].x = (long)(rnd(2) ? 1 : -1) * b[12 + i].z * 8 / 10;
        b[18 + i].y = (long)(rnd(2) ? 1 : -1) * b[18 + i].z / 2;
    }
    test_camera(0, SCR_H);
    rc = lz_balls_place(b, TEST_BALLS, &g_cam, g_kind, g_dmax, 5, oc, TEST_BALLS, g_small);
    ra = rv_balls_place(b, TEST_BALLS, &g_cam, g_kind, g_dmax, 5, oa, TEST_BALLS, g_small);
    nsc = (int)(rc & 0xFFFF);
    nbc = (int)(rc >> 16);
    nsa = (int)(ra & 0xFFFF);
    nba = (int)(ra >> 16);
    if (rc != ra || _fmemcmp(oc, oa, nsc * sizeof(LzBallOut))
        || _fmemcmp(oc + TEST_BALLS - nbc, oa + TEST_BALLS - nbc, nbc * sizeof(LzBallOut))) {
        rv_logf(s_count, nsa, nba, nsc, nbc);
        for (i = 0; i < TEST_BALLS; i++)
            if ((i < nsc || i >= TEST_BALLS - nbc) && _fmemcmp(&oc[i], &oa[i], sizeof(LzBallOut))) {
                rv_logf(s_one, i, oc[i].x, oc[i].y, oc[i].d, oc[i].set, oa[i].x, oa[i].y, oa[i].d, oa[i].set);
                break;
            }
        bad = -1;
        goto out;
    }
    /* the big ones' sprites as a frame finds them, the same list for both draws */
    resolve(oa + TEST_BALLS - nba, nba);
    _fmemcpy(oc, oa, TEST_BALLS * sizeof(LzBallOut));
    for (w = 0; w < SCR_H / TEST_ROWS * 2 && !bad; w++) {
        int y0 = (w % (SCR_H / TEST_ROWS)) * TEST_ROWS, y1 = y0 + TEST_ROWS, cy0 = y0, cy1 = y1, e0 = 12, part;
        const short __far *ex = w >= SCR_H / TEST_ROWS ? ext + 2 * 12 : 0;
        const short __far *ax = ex;
        unsigned long s1, s2;
        if (!ex)
            e0 = 0;
        bolts_rows(&cy0, &cy1, &ax, &e0, 376);   /* (no extents: the whole width's) */
        _fmemset(band, 0, TEST_ROWS * SCR_W);
        lz_balls_draw(oc, nsc, band, y0, y0, y1, ex, 12, 376);
        lz_balls_draw(oc + TEST_BALLS - nbc, nbc, band, y0, y0, y1, ex, 12, 376);
        s1 = checksum(band, &drawn);
        _fmemset(band, 0, TEST_ROWS * SCR_W);
        if (cy0 < cy1)
            for (part = 0; part < 2; part++)
                rv_balls_draw(part ? oa + TEST_BALLS - nba : oa, part ? nba : nsa, band, y0, cy0, cy1, ax, e0);
        s2 = checksum(band, &drawn);
        if (s1 != s2) {
            rv_logf(s_rows, y0, y1, ex != 0);
            bad = -1;
        }
    }
    if (!bad)
        rv_logf(s_ok, nsa, nba, TEST_BALLS, drawn / 2);
out:
    if (b) _ffree(b);
    if (oc) _ffree(oc);
    if (band) _ffree(band);
    if (ext) _ffree(ext);
    return bad ? bad : drawn / 2;
}

/* the timings' work */
static LzBall __far *__far t_b;
static LzBallOut __far *__far t_o;
static unsigned char __far *__far t_band;
static int __far t_n, __far t_no, __far t_y0;

static void t_place(void)
{
    t_no = (int)(rv_balls_place(t_b, t_n, &g_cam, g_kind, g_dmax, 0, t_o, t_n, g_small) & 0xFFFF);
}

static void t_draw(void)
{
    const short __far *ext = 0;
    int y0 = t_y0, y1 = t_y0 + TEST_ROWS, e0 = 0;
    bolts_rows(&y0, &y1, &ext, &e0, 0);
    rv_balls_draw(t_o, t_no, t_band, t_y0, y0, y1, ext, e0);
}

/* hundredths of a microsecond a ball (at least half a second of it) */
static long time_of(void (*work)(void), int n)
{
    unsigned long t0 = clock240(), dt;
    long it = 0;
    do {
        work();
        it++;
    } while ((dt = clock240() - t0) < 120);
    return (long)(dt * 416667UL / (unsigned long)it / (unsigned long)(n > 0 ? n : 1));
}

void balls_time(void)
{
    static const char __far s_mem[] = "balls: no memory for the timings",
        __far s_t[] = "balls: a ball %ld.%02ld us to place; to draw %ld.%02ld us at 1 px, %ld.%02ld at 4, %ld.%02ld at 8, "
                "%ld.%02ld at 16 (%d of %d on screen)";
    static const unsigned char __far sizes[4] = { 1, 4, 8, 16 };
    int i, k, placed;
    long pl, dr[4];
    t_b = (LzBall __far *)_fmalloc(512 * sizeof(LzBall));
    t_o = (LzBallOut __far *)_fmalloc(512 * sizeof(LzBallOut));
    t_band = (unsigned char __far *)_fmalloc(TEST_ROWS * SCR_W);
    if (!g_blk || !buffers() || !t_b || !t_o || !t_band) {
        rv_logf(s_mem);
        goto out;
    }
    bolts_tables();
    t_y0 = SCR_H / 2 - TEST_ROWS / 2;
    test_camera(t_y0, t_y0 + TEST_ROWS);
    test_balls(t_b, 512, 0, 700);       /* far ones, in a slab 40 rows high */
    for (i = 0; i < 512; i++)
        t_b[i].y = (long)(rnd(2001) - 1000) * (t_b[i].z / 256) / 1000 * 40 / 1000 * 256;
    t_n = 512;
    pl = time_of(t_place, 512);
    placed = t_no;
    /* the draw at each size: the placed balls made that size, their sprites the block's or the frame's */
    for (k = 0; k < 4; k++) {
        int d = sizes[k];
        t_place();
        if (d > LZ_BALL_SMALL && t_no > 64)
            t_no = 64;                  /* (big ones: a frame's worth) */
        g_nnear = 0;
        g_near_used = 0;
        for (i = 0; i < t_no; i++) {
            t_o[i].d = (unsigned char)d;
            t_o[i].y = (short)(t_y0 + rnd(TEST_ROWS - d));
            t_o[i].spr = d <= LZ_BALL_SMALL ? g_small + t_o[i].set * LZ_BALL_SMALL_SET + (unsigned)SUM_SQ(d - 1)
                                            : near_sprite(&t_o[i]);
        }
        dr[k] = time_of(t_draw, t_no);
    }
    rv_logf(s_t, pl / 100, pl % 100, dr[0] / 100, dr[0] % 100, dr[1] / 100, dr[1] % 100, dr[2] / 100, dr[2] % 100,
            dr[3] / 100, dr[3] % 100, placed, 512);
out:
    if (t_b) _ffree(t_b);
    if (t_o) _ffree(t_o);
    if (t_band) _ffree(t_band);
    t_b = 0;
    t_o = 0;
    t_band = 0;
}
