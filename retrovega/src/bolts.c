/* bolts.c: the flight's laser bolts (bolts.h).
 *
 * The pool is two far arrays: the bolts (24 bytes each, the world) and the
 * lines the camera makes of them (20 bytes), as many as memory allows.  Once
 * a frame rv_bolts_place puts every bolt through the camera: the far ones
 * (under BOLT_LINE16 of radius on screen) come out as lines, the near ones as
 * fat bolts, whose nested capsules lz_fat_setup prepares in C (they are few:
 * a dozen).  The bands then draw the lines (rv_lines_draw, one call for all)
 * and the fat bolts smallest first, each its passes then its core line.
 *
 * The asm is lasers.c's module to the bit: bolts_selftest checks that when a
 * flight starts, bolts_time measures what each stage costs. */
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include "rv.h"
#include "music.h"
#include "bolts.h"

#define FAT_MAX 32                      /* fat bolts a frame (the rest are not drawn): a dozen is the plan */

static LzBolt __far *g_b;
static LzLine __far *g_l;
static int g_cap, g_n, g_nl, g_nf;
static LzCam __far g_cam;
static LzFat __far g_fat[FAT_MAX];
static LzPass __far g_pass[FAT_MAX * 3];
static LzLine __far g_core[FAT_MAX];
static struct { unsigned char first, n, core; } __far g_fat_of[FAT_MAX];   /* its passes, and its core line (0xFF none) */
static LzPoly __far g_poly[FAT_MAX * 2];   /* the simpler shapes (LZ_SHAPE_AXIS ..): outline and middle */
static LzLine __far g_slab_line[FAT_MAX * 3];   /* ... or three lines */
static struct { unsigned char npoly, nline; } __far g_slab_of[FAT_MAX];   /* (each bolt's at 2i and 3i) */
static int g_shape = LZ_SHAPE_CAPSULE;
static short __far g_open[SCR_H * 2];   /* the whole width, for a draw without the cockpit's extents */
static LzStyle __far g_style = { UI_RED, 130, UI_RED, UI_WHITE, BOLT_LINE16, 32 };   /* far lines and bodies red, the rim
                                           fire 130 (dark red), the core white */
static LzStyle __far g_style2 = { UI_RED, 130, UI_RED, UI_WHITE, BOLT_LINE16, 0x7FFF };   /* BOLT_SHAPE_CAPSULE2: no rim */
#define CAPSULES(s) ((s) == LZ_SHAPE_CAPSULE || (s) == BOLT_SHAPE_CAPSULE2)
static unsigned long g_t_place, g_t_far, g_t_near, g_t_move;

/* the whole width every row, and the roots' table: for draws without the cockpit's extents */
static void open_init(void)
{
    int k;
    for (k = 0; k < SCR_H; k++) {
        g_open[2 * k] = 0;
        g_open[2 * k + 1] = SCR_W;
    }
    lz_init();
}

unsigned long bolts_dos_free(void)
{
    union REGS r;
    r.h.ah = 0x48;
    r.x.bx = 0xFFFF;
    int86(0x21, &r, &r);
    return (unsigned long)r.x.bx * 16;
}

void bolts_free(void)
{
    if (g_b) _ffree(g_b);
    if (g_l) _ffree(g_l);
    g_b = 0;
    g_l = 0;
    g_cap = g_n = g_nl = g_nf = 0;
}

int bolts_init(int want, unsigned keep_kb)
{
    union REGS r;
    unsigned long room;
    bolts_free();
    if (want > 3200)
        want = 3200;                    /* each array within a segment */
    r.h.ah = 0x48;                      /* what DOS has left, less what must stay free */
    r.x.bx = 0xFFFF;
    int86(0x21, &r, &r);
    room = (unsigned long)r.x.bx * 16;
    room = room > (unsigned long)keep_kb * 1024 ? room - (unsigned long)keep_kb * 1024 : 0;
    if ((unsigned long)want * (sizeof(LzBolt) + sizeof(LzLine) + 1) > room)
        want = (int)(room / (sizeof(LzBolt) + sizeof(LzLine) + 1));
    while (want >= 16) {
        g_b = (LzBolt __far *)_fmalloc((size_t)want * sizeof(LzBolt));
        g_l = g_b ? (LzLine __far *)_fmalloc((size_t)want * sizeof(LzLine)) : 0;
        if (g_b && g_l)
            break;
        bolts_free();
        want = want * 3 / 4;
    }
    g_cap = g_b ? want : 0;
    rv_log("bolts: room for %d (%lu KB of DOS memory was free)", g_cap, (unsigned long)r.x.bx * 16 / 1024);
    open_init();
    bolts_ticks_reset();
    return g_cap;
}

LzBolt __far *bolts_pool(void) { return g_b; }
int bolts_count(void) { return g_n; }
void bolts_set_count(int n) { g_n = n < 0 ? 0 : n > g_cap ? g_cap : n; }

void bolts_move(long k)
{
    unsigned long t0 = clock240();
    rv_bolts_move(g_b, g_n, k);
    g_t_move += clock240() - t0;
}

static void camera(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1,
                   int line16)
{
    long f[3], r[3], u[3];
    f[0] = cam->front.x; f[1] = cam->front.y; f[2] = cam->front.z;
    r[0] = cam->right.x; r[1] = cam->right.y; r[2] = cam->right.z;
    u[0] = cam->up.x; u[1] = cam->up.y; u[2] = cam->up.z;
    lz_camera(&g_cam, px, py, pz, f, r, u, cx, cy, focal, BOLT_RADIUS16, BOLT_ZMIN_KM, BOLT_ZMAX_KM, BOLT_RMAX16,
              line16, y0, y1);
}

void bolts_set_shape(int shape) { g_shape = shape < LZ_SHAPE_CAPSULE || shape > BOLT_SHAPE_CAPSULE2 ? LZ_SHAPE_CAPSULE : shape; }

/* the fat ones' passes (or polygons), the smallest (the farthest) first so the nearest paint last */
static void fat_setup(void)
{
    int i, k, np = 0, nc = 0;
    for (i = 1; i < g_nf; i++)
        for (k = i; k > 0 && g_fat[k].r < g_fat[k - 1].r; k--) {
            LzFat t = g_fat[k];
            g_fat[k] = g_fat[k - 1];
            g_fat[k - 1] = t;
        }
    if (!CAPSULES(g_shape)) {
        for (i = 0; i < g_nf; i++) {
            int nl;
            g_slab_of[i].npoly = (unsigned char)rv_slab_setup(&g_fat[i], g_shape, &g_style, &g_poly[2 * i],
                                                              &g_slab_line[3 * i], (int __far *)&nl);
            g_slab_of[i].nline = (unsigned char)nl;
        }
        return;
    }
    for (i = 0; i < g_nf; i++) {
        int cl;
        g_fat_of[i].first = (unsigned char)np;
        g_fat_of[i].n = (unsigned char)rv_fat_setup(&g_fat[i], g_shape == BOLT_SHAPE_CAPSULE2 ? &g_style2 : &g_style,
                                                    &g_pass[np], &g_core[nc], (int __far *)&cl);
        g_fat_of[i].core = (unsigned char)(cl ? nc : 0xFF);
        np += g_fat_of[i].n;
        nc += cl;
    }
}

void bolts_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1, int line16)
{
    int nf;
    unsigned long t0 = clock240();
    camera(px, py, pz, cam, cx, cy, focal, y0, y1, line16);
    g_nl = rv_bolts_place(g_b, g_n, &g_cam, g_l, g_cap, g_fat, FAT_MAX, (int __far *)&nf);
    g_nf = nf;
    fat_setup();
    g_t_place += clock240() - t0;
}

int bolts_on_screen(int *lines, int *fats)
{
    *lines = g_nl;
    *fats = g_nf;
    return g_nl + g_nf;
}

/* the rows of this band the bolts may touch, and the extents for them */
static int rows_of(int *y0, int *y1, const short __far **ext, int *ext_y0, int ext_rows)
{
    if (*ext) {
        if (*y0 < *ext_y0) *y0 = *ext_y0;
        if (*y1 > *ext_y0 + ext_rows) *y1 = *ext_y0 + ext_rows;
    } else {
        *ext = g_open;
        *ext_y0 = 0;
    }
    if (*y0 < 0) *y0 = 0;
    if (*y1 > SCR_H) *y1 = SCR_H;
    return *y0 < *y1;
}

void bolts_draw_far(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int run0, run1;
    unsigned long t0 = clock240();
    if (g_nl && vid_run(&run0, &run1) && rows_of(&y0, &y1, &ext, &ext_y0, ext_rows))
        rv_lines_draw(g_l, g_nl, g_style.line, vid_row(run0), run0, y0, y1, ext, ext_y0);
    g_t_far += clock240() - t0;
}

void bolts_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int run0, run1, i, k;
    unsigned long t0 = clock240();
    if (g_nf && vid_run(&run0, &run1) && rows_of(&y0, &y1, &ext, &ext_y0, ext_rows)) {
        unsigned char __far *band = vid_row(run0);
        for (i = 0; i < g_nf && !CAPSULES(g_shape); i++) {
            for (k = 0; k < g_slab_of[i].npoly; k++)
                rv_poly_draw(&g_poly[2 * i + k], band, run0, y0, y1, ext, ext_y0);
            for (k = 0; k < g_slab_of[i].nline; k++)
                rv_lines_draw(&g_slab_line[3 * i + k], 1, k == 1 || g_slab_of[i].nline == 1 ? g_style.core : g_style.body,
                              band, run0, y0, y1, ext, ext_y0);
        }
        for (i = 0; i < g_nf && CAPSULES(g_shape); i++) {
            for (k = 0; k < g_fat_of[i].n; k++)
                rv_pass_draw(&g_pass[g_fat_of[i].first + k], band, run0, y0, y1, ext, ext_y0, lz_sqrt16);
            if (g_fat_of[i].core != 0xFF)
                rv_lines_draw(&g_core[g_fat_of[i].core], 1, g_style.core, band, run0, y0, y1, ext, ext_y0);
        }
    }
    g_t_near += clock240() - t0;
}

void bolts_ticks(unsigned long out[4])
{
    out[0] = g_t_place;
    out[1] = g_t_far;
    out[2] = g_t_near;
    out[3] = g_t_move;
}

void bolts_ticks_reset(void) { g_t_place = g_t_far = g_t_near = g_t_move = 0; }

/* --- the checks and the timings, on bolts of their own in a band of their own --- */

#define TEST_ROWS 40
#define TEST_BOLTS 320
static unsigned long g_seed = 4321;

static int rnd(int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (int)((g_seed >> 16) & 0x7FFF) % n;
}

/* bolts round the camera (at the origin, looking down +z): `near` of them 220 .. 900 km out, the rest 1,500 ..
 * 9,000 (`near` is a word Watcom keeps: n_near); spread `spread`/1000 of their distance across and `vspread` up and down; every way */
static void test_bolts(LzBolt __far *b, int n, int n_near, int spread, int vspread)
{
    int i;
    for (i = 0; i < n; i++) {
        long d = i < n_near ? 220 + rnd(680) : 1500 + rnd(7500);
        long hx = rnd(193) - 96, hy = rnd(193) - 96, hz = rnd(193) - 96;
        long len = (long)lf_sqrt((unsigned long)(hx * hx + hy * hy + hz * hz));
        if (len < 8) { hx = 96; len = 96; }
        b[i].x = (long)(rnd(2001) - 1000) * d / 1000 * spread / 1000 * 256;
        b[i].y = (long)(rnd(2001) - 1000) * d / 1000 * vspread / 1000 * 256;
        b[i].z = d * 256;
        b[i].hx = (short)(hx * BOLT_HALF16 / len);
        b[i].hy = (short)(hy * BOLT_HALF16 / len);
        b[i].hz = (short)(hz * BOLT_HALF16 / len);
    }
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

static void identity(LfBasis *cam)
{
    memset(cam, 0, sizeof *cam);
    cam->front.z = 256;
    cam->right.x = 256;
    cam->up.y = 256;
}

long bolts_selftest(void)
{
    LzBolt __far *b = (LzBolt __far *)_fmalloc(TEST_BOLTS * sizeof(LzBolt));
    LzLine __far *lc = (LzLine __far *)_fmalloc(TEST_BOLTS * sizeof(LzLine));
    LzLine __far *la = (LzLine __far *)_fmalloc(TEST_BOLTS * sizeof(LzLine));
    unsigned char __far *band = (unsigned char __far *)_fmalloc(TEST_ROWS * SCR_W);
    LzFat __far *fc = (LzFat __far *)_fmalloc(2 * FAT_MAX * sizeof(LzFat)), __far *fa = fc + FAT_MAX;
    short __far *ext = (short __far *)_fmalloc(SCR_H * 2 * sizeof(short));
    LfBasis cam;
    long drawn = 0;
    int nlc, nfc, nla, nfa, w, i, k, bad = 0;
    open_init();
    if (!b || !lc || !la || !band || !fc || !ext) {
        rv_log("bolts: no memory for the self-test");
        bad = -2;
        goto out;
    }
    /* an opening of uneven extents, from row 12 to 388 */
    for (k = 0; k < SCR_H; k++) {
        ext[2 * k] = (short)(30 + (k * 7) % 61);
        ext[2 * k + 1] = (short)(610 - (k * 5) % 47);
    }
    identity(&cam);
    test_bolts(b, TEST_BOLTS, 60, 700, 450);
    /* a few that cross the near plane, and some behind */
    for (i = 0; i < 12; i++) {
        b[i].z = (BOLT_ZMIN_KM + rnd(13) - 6) * 256L;
        b[i].hz = (short)(rnd(2) ? BOLT_HALF16 : -BOLT_HALF16);
        b[i].hx = (short)(rnd(40) - 20);
        b[12 + i].z = -256L * (100 + rnd(900));
    }
    camera(0, 0, 0, &cam, SCR_W / 2, SCR_H / 2, 400, 0, SCR_H, BOLT_LINE16);
    lz_place(b, TEST_BOLTS, &g_cam, lc, &nlc, TEST_BOLTS, fc, &nfc, FAT_MAX);
    nla = rv_bolts_place(b, TEST_BOLTS, &g_cam, la, TEST_BOLTS, fa, FAT_MAX, (int __far *)&nfa);
    if (nla != nlc || nfa != nfc || _fmemcmp(lc, la, nlc * sizeof(LzLine)) || _fmemcmp(fc, fa, nfc * sizeof(LzFat))) {
        rv_log("bolts: rv_bolts_place gave %d lines, %d fat; lz_place %d, %d", nla, nfa, nlc, nfc);
        for (i = 0; i < nlc && i < nla; i++)
            if (_fmemcmp(&lc[i], &la[i], sizeof(LzLine))) {
                rv_log("bolts: line %d: C rows %d-%d x %ld %ld %ld m %ld; asm rows %d-%d x %ld %ld %ld m %ld", i,
                       lc[i].y0, lc[i].y1, lc[i].ax, lc[i].bx, lc[i].x1, lc[i].m, la[i].y0, la[i].y1, la[i].ax,
                       la[i].bx, la[i].x1, la[i].m);
                break;
            }
        bad = -1;
        goto out;
    }
    /* the fat bolts' set-ups: rv_fat_setup against lz_fat_setup, at their own radii and every other */
    for (i = 0; i < nfc && !bad; i++) {
        for (k = 4; k <= 64 && !bad; k += 3) {
            LzPass pc[3], pa[3];
            LzLine cc, ca;
            LzFat f = fc[i];
            LzStyle st = g_style;
            int clc, cla, n1, n2;
            f.r = (short)(k == 4 ? fc[i].r : k);
            n1 = lz_fat_setup(&f, &st, pc, &cc, &clc);
            n2 = rv_fat_setup(&f, &g_style, pa, &ca, (int __far *)&cla);
            if (n1 != n2 || clc != cla || _fmemcmp(pc, pa, n1 * sizeof(LzPass)) || (clc && _fmemcmp(&cc, &ca, sizeof cc))) {
                rv_log("bolts: rv_fat_setup differs for %d,%d-%d,%d r %d: %d passes (C %d), core line %d (C %d)", f.ax,
                       f.ay, f.bx, f.by, f.r, n2, n1, cla, clc);
                for (w = 0; w < n1 && w < n2; w++)
                    if (_fmemcmp(&pc[w], &pa[w], sizeof(LzPass)))
                        rv_log("bolts: pass %d: C top %d bot %d la %d lb %d ra %d rb %d c0 %ld m %ld hw %ld; asm %d %d %d %d "
                               "%d %d %ld %ld %ld", w, pc[w].top, pc[w].bot, pc[w].la, pc[w].lb, pc[w].ra, pc[w].rb,
                               pc[w].c0, pc[w].m, pc[w].hw, pa[w].top, pa[w].bot, pa[w].la, pa[w].lb, pa[w].ra, pa[w].rb,
                               pa[w].c0, pa[w].m, pa[w].hw);
                bad = -1;
            }
        }
    }
    /* the draws, band by band (40 rows), with and without the extents */
    for (w = 0; w < SCR_H / TEST_ROWS * 2 && !bad; w++) {
        int y0 = (w % (SCR_H / TEST_ROWS)) * TEST_ROWS, y1 = y0 + TEST_ROWS, cy0 = y0, cy1 = y1, e0 = 12;
        const short __far *ex = w >= SCR_H / TEST_ROWS ? ext + 2 * 12 : 0;
        const short __far *ax = ex ? ex : g_open;
        unsigned long s1, s2;
        if (ex) {
            if (cy0 < 12) cy0 = 12;
            if (cy1 > 388) cy1 = 388;
        } else {
            e0 = 0;
        }
        _fmemset(band, 0, TEST_ROWS * SCR_W);
        lz_lines_draw(lc, nlc, UI_RED, band, y0, y0, y1, ex, 12, 376);
        for (i = 0; i < nfc; i++) {
            LzPass p[3];
            LzLine core;
            LzStyle st = g_style;
            int cl, n = lz_fat_setup(&fc[i], &st, p, &core, &cl);
            for (k = 0; k < n; k++)
                lz_pass_draw(&p[k], band, y0, y0, y1, ex, 12, 376);
            if (cl)
                lz_lines_draw(&core, 1, UI_WHITE, band, y0, y0, y1, ex, 12, 376);
        }
        s1 = checksum(band, &drawn);
        _fmemset(band, 0, TEST_ROWS * SCR_W);
        if (cy0 < cy1) {
            rv_lines_draw(la, nla, UI_RED, band, y0, cy0, cy1, ax, e0);
            for (i = 0; i < nfa; i++) {
                LzPass p[3];
                LzLine core;
                int cl, n = rv_fat_setup(&fa[i], &g_style, p, &core, (int __far *)&cl);
                for (k = 0; k < n; k++)
                    rv_pass_draw(&p[k], band, y0, cy0, cy1, ax, e0, lz_sqrt16);
                if (cl)
                    rv_lines_draw(&core, 1, UI_WHITE, band, y0, cy0, cy1, ax, e0);
            }
        }
        s2 = checksum(band, &drawn);
        if (s1 != s2) {
            rv_log("bolts: the draws differ in rows %d-%d (%s the extents)", y0, y1, ex ? "with" : "without");
            bad = -1;
        }
    }
    /* the simpler shapes' set-up: rv_slab_setup against lz_slab_setup */
    for (k = LZ_SHAPE_AXIS; k <= LZ_SHAPE_LAST && !bad; k++)
        for (i = 0; i < nfc && !bad; i++) {
            int r;
            for (r = 12; r <= 64 && !bad; r += 4) {
                LzPoly pc[2], pa[2];
                LzLine lc3[3], la3[3];
                LzFat f = fc[i];
                LzStyle st = g_style;
                int n1, n2, l1, l2;
                f.r = (short)r;
                if (i % 4 == 3) {               /* (some end on: no length) */
                    f.bx = f.ax;
                    f.by = f.ay;
                }
                _fmemset(pc, 0, sizeof pc);
                _fmemset(pa, 0, sizeof pa);
                n1 = lz_slab_setup(&f, k, &st, pc, lc3, &l1);
                n2 = rv_slab_setup(&f, k, &g_style, pa, la3, (int __far *)&l2);
                if (n1 != n2 || l1 != l2 || (n1 && _fmemcmp(pc, pa, n1 * sizeof(LzPoly))) ||
                    (l1 && _fmemcmp(lc3, la3, l1 * sizeof(LzLine)))) {
                    rv_log("bolts: rv_slab_setup differs for shape %d, %d,%d-%d,%d r %d: %d polygons, %d lines (C %d, %d)",
                           k, f.ax, f.ay, f.bx, f.by, r, n2, l2, n1, l1);
                    if (n1 && n2)
                        rv_log("bolts: C n %d top %d bot %d v0 %d,%d v1 %d,%d; asm n %d top %d bot %d v0 %d,%d v1 %d,%d",
                               pc[0].n, pc[0].top, pc[0].bot, pc[0].x[0], pc[0].y[0], pc[0].x[1], pc[0].y[1], pa[0].n,
                               pa[0].top, pa[0].bot, pa[0].x[0], pa[0].y[0], pa[0].x[1], pa[0].y[1]);
                    bad = -1;
                }
            }
        }
    /* the simpler shapes: rv_poly_draw against lz_poly_draw, every shape, band by band with the extents */
    for (k = LZ_SHAPE_AXIS; k <= LZ_SHAPE_LAST && !bad; k++)
        for (w = 0; w < SCR_H / TEST_ROWS && !bad; w++) {
            int y0 = w * TEST_ROWS, y1 = y0 + TEST_ROWS, cy0 = y0 < 12 ? 12 : y0, cy1 = y1 > 388 ? 388 : y1, pass;
            unsigned long sum[2];
            LzStyle st = g_style;
            for (pass = 0; pass < 2; pass++) {
                _fmemset(band, 0, TEST_ROWS * SCR_W);
                for (i = 0; i < nfc; i++) {
                    LzPoly pl[2];
                    LzLine ln[3];
                    LzFat f = fc[i];
                    int nl, j, n;
                    if (i % 4 == 3) {           /* (some end on) */
                        f.bx = f.ax;
                        f.by = f.ay;
                    }
                    n = lz_slab_setup(&f, k, &st, pl, ln, &nl);
                    for (j = 0; j < n; j++)
                        if (pass)
                            rv_poly_draw(&pl[j], band, y0, cy0, cy1, ext + 2 * 12, 12);
                        else
                            lz_poly_draw(&pl[j], band, y0, y0, y1, ext + 2 * 12, 12, 376);
                    for (j = 0; j < nl; j++)
                        if (pass)
                            rv_lines_draw(&ln[j], 1, j == 1 || nl == 1 ? UI_WHITE : UI_RED, band, y0, cy0, cy1, ext + 2 * 12,
                                          12);
                        else
                            lz_lines_draw(&ln[j], 1, j == 1 || nl == 1 ? UI_WHITE : UI_RED, band, y0, y0, y1, ext + 2 * 12,
                                          12, 376);
                }
                sum[pass] = checksum(band, &drawn);
            }
            if (sum[0] != sum[1]) {
                rv_log("bolts: rv_poly_draw differs from lz_poly_draw for shape %d in rows %d-%d", k, y0, y1);
                bad = -1;
            }
        }
    /* the flight: rv_bolts_move against lz_move */
    if (!bad) {
        _fmemcpy(lc, b, sizeof(LzBolt) * 6);
        lz_move((LzBolt __far *)lc, 6, 200);
        rv_bolts_move(b, 6, 200);
        if (_fmemcmp(lc, b, sizeof(LzBolt) * 6)) {
            rv_log("bolts: rv_bolts_move differs from lz_move");
            bad = -1;
        }
    }
    rv_log("bolts: the asm %s lasers.c (%d lines, %d fat, %ld pixels compared)", bad ? "DISAGREES WITH" : "agrees with",
           nlc, nfc, drawn / 2);
out:
    if (b) _ffree(b);
    if (lc) _ffree(lc);
    if (la) _ffree(la);
    if (band) _ffree(band);
    if (fc) _ffree(fc);
    if (ext) _ffree(ext);
    return bad ? bad : drawn / 2;
}

/* the timings' work, one function a stage (the state in these) */
static LzBolt __far *t_b;
static LzLine __far *t_l;
static unsigned char __far *t_band;
static struct {                         /* (allocated for the timings only) */
    LzFat f[FAT_MAX];
    LzPass p[FAT_MAX * 3];
    LzLine core[FAT_MAX];
    unsigned char np[FAT_MAX], hascore[FAT_MAX];
    LzPoly poly[FAT_MAX * 2];
    LzLine sl[FAT_MAX * 3];
    unsigned char npoly[FAT_MAX], nsl[FAT_MAX];
    unsigned char spans[12000];        /* the blit's sprites: each bolt's rows of runs, from sp_at[i] */
    short sp_top[FAT_MAX], sp_rows[FAT_MAX], sp_left[FAT_MAX];
    unsigned short sp_at[FAT_MAX], spans_used;
} __far *t_w;
static int t_shape;
#define t_f (t_w->f)
#define t_p (t_w->p)
#define t_core (t_w->core)
#define t_np (t_w->np)
#define t_hascore (t_w->hascore)
static int t_n, t_nl, t_nf, t_y0;

static void t_place(void) { t_nl = rv_bolts_place(t_b, t_n, &g_cam, t_l, 512, t_f, FAT_MAX, (int __far *)&t_nf); }
static void t_lines(void) { rv_lines_draw(t_l, t_nl, UI_RED, t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0); }

static void t_setup(void)
{
    int i, cl;
    for (i = 0; i < t_nf; i++) {
        t_np[i] = (unsigned char)rv_fat_setup(&t_f[i], &g_style, &t_p[i * 3], &t_core[i], (int __far *)&cl);
        t_hascore[i] = (unsigned char)cl;
    }
}

static void t_passes(void)
{
    int i, k;
    for (i = 0; i < t_nf; i++) {
        for (k = 0; k < t_np[i]; k++)
            rv_pass_draw(&t_p[i * 3 + k], t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0, lz_sqrt16);
        if (t_hascore[i])
            rv_lines_draw(&t_core[i], 1, UI_WHITE, t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0);
    }
}

static void t_slab_setup(void)
{
    int i, nl;
    for (i = 0; i < t_nf; i++) {
        t_w->npoly[i] = (unsigned char)rv_slab_setup(&t_f[i], t_shape, &g_style, &t_w->poly[2 * i], &t_w->sl[3 * i],
                                                     (int __far *)&nl);
        t_w->nsl[i] = (unsigned char)nl;
    }
}

static void t_slab_draw(void)
{
    int i, k;
    for (i = 0; i < t_nf; i++) {
        for (k = 0; k < t_w->npoly[i]; k++)
            rv_poly_draw(&t_w->poly[2 * i + k], t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0);
        for (k = 0; k < t_w->nsl[i]; k++)
            rv_lines_draw(&t_w->sl[3 * i + k], 1, k == 1 || t_w->nsl[i] == 1 ? UI_WHITE : UI_RED, t_band, t_y0, t_y0,
                          t_y0 + TEST_ROWS, g_open, 0);
    }
}

void __far __cdecl rv_blit_row(unsigned char __far *row, const unsigned char __far *spans, int left, int xa, int xb);

/* each near bolt drawn alone and kept as a sprite of its own: its rows from its first to its last, from its
 * leftmost column (as a pre-rendered bolt would be stored) */
static int t_encode_each(void)
{
    int i, r, x;
    unsigned at = 0;
    for (i = 0; i < t_nf && i < FAT_MAX; i++) {
        int top = TEST_ROWS, bot = -1, left = SCR_W;
        _fmemset(t_band, 0, TEST_ROWS * SCR_W);
        for (r = 0; r < t_np[i]; r++)
            rv_pass_draw(&t_p[i * 3 + r], t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0, lz_sqrt16);
        if (t_hascore[i])
            rv_lines_draw(&t_core[i], 1, UI_WHITE, t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0);
        for (r = 0; r < TEST_ROWS; r++)
            for (x = 0; x < SCR_W; x++)
                if (t_band[(unsigned)r * SCR_W + x]) {
                    if (r < top) top = r;
                    if (r > bot) bot = r;
                    if (x < left) left = x;
                }
        t_w->sp_top[i] = (short)top;
        t_w->sp_rows[i] = (short)(bot >= top ? bot - top + 1 : 0);
        t_w->sp_left[i] = (short)left;
        t_w->sp_at[i] = (unsigned short)at;
        for (r = top; r <= bot; r++) {
            const unsigned char __far *row = t_band + (unsigned)r * SCR_W;
            int last = left;
            for (x = left; x < SCR_W;) {
                int s, n;
                if (!row[x]) { x++; continue; }
                s = x;
                while (x < SCR_W && row[x] && x - s < 255) x++;
                n = x - s;
                if (at + 4 + n > sizeof t_w->spans) return 0;
                t_w->spans[at++] = (unsigned char)(s - last);
                t_w->spans[at++] = (unsigned char)n;
                _fmemcpy(&t_w->spans[at], row + s, n);
                at += n;
                last = x;
            }
            t_w->spans[at++] = 0;
            t_w->spans[at++] = 0;
        }
    }
    t_w->spans_used = at;
    return 1;
}

static void t_blit_each(void)
{
    int i, r;
    for (i = 0; i < t_nf && i < FAT_MAX; i++) {
        const unsigned char __far *sp = &t_w->spans[t_w->sp_at[i]];
        for (r = 0; r < t_w->sp_rows[i]; r++) {
            rv_blit_row(t_band + (unsigned)(t_w->sp_top[i] + r) * SCR_W, sp, t_w->sp_left[i], 0, SCR_W);
            while (sp[0] || sp[1])      /* on to the next row's runs */
                sp += 2 + sp[1];
            sp += 2;
        }
    }
}

static void t_capsules(void)
{
    t_place();
    t_setup();
    t_passes();
}

/* microseconds x 100 a bolt for `work` over n bolts: run until half a second has gone */
static long time_of(void (*work)(void), int n)
{
    unsigned long t0 = clock240(), dt;
    long it = 0;
    do {
        work();
        it++;
    } while ((dt = clock240() - t0) < 120);
    return (long)(dt * 416667UL / (unsigned long)it / (unsigned long)(n > 0 ? n : 1));   /* 10^8 / 240 */
}

void bolts_time(long out[6])
{
    LfBasis cam;
    int i, k, nfar = 0;
    long rows = 0;
    t_b = (LzBolt __far *)_fmalloc(512 * sizeof(LzBolt));
    t_l = (LzLine __far *)_fmalloc(512 * sizeof(LzLine));
    t_band = (unsigned char __far *)_fmalloc(TEST_ROWS * SCR_W);
    t_w = _fmalloc(sizeof *t_w);
    t_y0 = SCR_H / 2 - TEST_ROWS / 2;
    open_init();
    for (k = 0; k < 6; k++)
        out[k] = 0;
    if (!t_b || !t_l || !t_band || !t_w) {
        rv_log("bolts: no memory for the timings");
        goto out;
    }
    identity(&cam);
    /* far bolts in a slab 40 rows high: 512 of them, lines */
    test_bolts(t_b, 512, 0, 700, 40);
    t_n = 512;
    camera(0, 0, 0, &cam, SCR_W / 2, SCR_H / 2, 400, t_y0, t_y0 + TEST_ROWS, BOLT_LINE16);
    out[0] = time_of(t_place, 512);
    for (i = 0; i < t_nl; i++)
        rows += t_l[i].y1 - t_l[i].y0;
    out[1] = time_of(t_lines, t_nl);
    nfar = t_nl;
    /* near bolts: twelve, 220 .. 900 km, fat */
    test_bolts(t_b, 12, 12, 300, 20);
    t_n = 12;
    out[2] = time_of(t_place, 12);
    out[3] = time_of(t_setup, t_nf);
    out[4] = time_of(t_passes, t_nf);
    rv_log("bolts: the timing's near bolts: %d of 12 on screen, radius %d..%d x16", t_nf, t_nf ? t_f[0].r : 0,
           t_nf ? t_f[t_nf - 1].r : 0);
    {
        /* the simpler shapes on the same bolts: set up and drawn; and the same capsules copied as sprite rows */
        static const char *names[5] = { "", "level/upright ends", "rectangle", "hexagon", "swept octagon" };
        long su, dr, bl;
        int sh;
        for (sh = LZ_SHAPE_AXIS; sh <= LZ_SHAPE_LAST; sh++) {
            t_shape = sh;
            su = time_of(t_slab_setup, t_nf);
            dr = time_of(t_slab_draw, t_nf);
            rv_log("bolts: a near bolt as a %s: %ld.%02ld us to set up, %ld.%02ld to draw", names[sh], su / 100, su % 100,
                   dr / 100, dr % 100);
        }
        {
            /* the capsules in two colours (BOLT_SHAPE_CAPSULE2: no dark rim, the body and the core as the
             * polygons have) */
            short rim = g_style.rim16;
            g_style.rim16 = 0x7FFF;
            su = time_of(t_setup, t_nf);
            dr = time_of(t_passes, t_nf);
            g_style.rim16 = rim;
            rv_log("bolts: a near bolt as capsules in two colours: %ld.%02ld us to set up, %ld.%02ld to draw", su / 100,
                   su % 100, dr / 100, dr % 100);
        }
        if (t_encode_each()) {
            bl = time_of(t_blit_each, t_nf);
            rv_log("bolts: the same capsules as pre-rendered sprites, each its own, copied a row at a time "
                   "(rv_blit_row, as ships_blit_view does): %ld.%02ld us a bolt (%u bytes of runs)", bl / 100, bl % 100,
                   t_w->spans_used);
        }
    }
    /* one algorithm: far bolts drawn as capsules (no line under any radius) */
    test_bolts(t_b, FAT_MAX, 0, 700, 40);
    t_n = FAT_MAX;
    camera(0, 0, 0, &cam, SCR_W / 2, SCR_H / 2, 400, t_y0, t_y0 + TEST_ROWS, 0);
    out[5] = time_of(t_capsules, FAT_MAX);
    rv_log("bolts: a far bolt %ld.%02ld us to place, %ld.%02ld to draw (%d lines of %ld.%02ld rows); a near one "
           "%ld.%02ld to place, %ld.%02ld to set up, %ld.%02ld to draw; a far one as a capsule %ld.%02ld in all",
           out[0] / 100, out[0] % 100, out[1] / 100, out[1] % 100, nfar, nfar ? rows / nfar : 0L,
           nfar ? rows * 100 / nfar % 100 : 0L, out[2] / 100, out[2] % 100, out[3] / 100, out[3] % 100,
           out[4] / 100, out[4] % 100, out[5] / 100, out[5] % 100);
out:
    if (t_b) _ffree(t_b);
    if (t_l) _ffree(t_l);
    if (t_band) _ffree(t_band);
    if (t_w) _ffree(t_w);
    t_w = 0;
    t_b = 0;
    t_l = 0;
    t_band = 0;
}
