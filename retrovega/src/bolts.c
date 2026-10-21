/* bolts.c: the flight's laser bolts (bolts.h).
 *
 * The pool is two far arrays: the bolts (24 bytes each, the world) and the
 * lines the camera makes of them (20 bytes), as many as memory allows.  Once
 * a frame rv_bolts_place puts every bolt through the camera: the far ones
 * (under BOLT_LINE16 of radius on screen) come out as lines, the near ones as
 * fat bolts, whose capsules rv_fat_setup prepares (they are few: a dozen).
 * The bands then draw the lines (rv_lines_draw, one call for all) and the fat
 * bolts smallest first, each its passes then its core line.
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
static LzPass __far g_pass[FAT_MAX * 2];
static LzLine __far g_core[FAT_MAX];
static struct { unsigned char first, n, core; } __far g_fat_of[FAT_MAX];   /* its passes, and its core line (0xFF none) */
static short __far g_open[SCR_H * 2];   /* the whole width, for a draw without the cockpit's extents */
static LzStyle __far g_style = { UI_RED, UI_RED, UI_WHITE, 0, BOLT_LINE16 };   /* far lines and bodies red, the middle
                                           white */
static unsigned long g_t_place, g_t_far, g_t_near, g_t_move;

/* the whole width every row, and the roots' table: for draws without the cockpit's extents */
void bolts_tables(void)
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
    bolts_tables();
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

/* the fat ones' passes, the smallest (the farthest) first so the nearest paint last */
static void fat_setup(void)
{
    int i, k, np = 0, nc = 0;
    for (i = 1; i < g_nf; i++)
        for (k = i; k > 0 && g_fat[k].r < g_fat[k - 1].r; k--) {
            LzFat t = g_fat[k];
            g_fat[k] = g_fat[k - 1];
            g_fat[k - 1] = t;
        }
    for (i = 0; i < g_nf; i++) {
        int cl;
        g_fat_of[i].first = (unsigned char)np;
        g_fat_of[i].n = (unsigned char)rv_fat_setup(&g_fat[i], &g_style, &g_pass[np], &g_core[nc], (int __far *)&cl);
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
int bolts_rows(int *y0, int *y1, const short __far **ext, int *ext_y0, int ext_rows)
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
    if (g_nl && vid_run(&run0, &run1) && bolts_rows(&y0, &y1, &ext, &ext_y0, ext_rows))
        rv_lines_draw(g_l, g_nl, g_style.line, vid_row(run0), run0, y0, y1, ext, ext_y0);
    g_t_far += clock240() - t0;
}

void bolts_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int run0, run1, i, k;
    unsigned long t0 = clock240();
    if (g_nf && vid_run(&run0, &run1) && bolts_rows(&y0, &y1, &ext, &ext_y0, ext_rows)) {
        unsigned char __far *band = vid_row(run0);
        for (i = 0; i < g_nf; i++) {
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

/* a fat bolt (end_on: its b moved onto its a) drawn by lasers.c, as the build draws them */
static void fat_c(const LzFat __far *src, int end_on, unsigned char __far *band, int y0, int y1,
                  const short __far *ex)
{
    LzFat f = *src;
    LzStyle st = g_style;
    int k, n;
    LzPass p[2];
    LzLine core;
    int cl;
    if (end_on) {
        f.bx = f.ax;
        f.by = f.ay;
    }
    n = lz_fat_setup(&f, &st, p, &core, &cl);
    for (k = 0; k < n; k++)
        lz_pass_draw(&p[k], band, y0, y0, y1, ex, 12, 376);
    if (cl)
        lz_lines_draw(&core, 1, UI_WHITE, band, y0, y0, y1, ex, 12, 376);
}

/* ... and by the asm, rows cy0 .. cy1-1 with the extents ax from row e0 */
static void fat_asm(const LzFat __far *src, int end_on, unsigned char __far *band, int y0, int cy0, int cy1,
                    const short __far *ax, int e0)
{
    LzFat f = *src;
    int k, n;
    LzPass p[2];
    LzLine core;
    int cl;
    if (end_on) {
        f.bx = f.ax;
        f.by = f.ay;
    }
    n = rv_fat_setup(&f, &g_style, p, &core, (int __far *)&cl);
    for (k = 0; k < n; k++)
        rv_pass_draw(&p[k], band, y0, cy0, cy1, ax, e0, lz_sqrt16);
    if (cl)
        rv_lines_draw(&core, 1, UI_WHITE, band, y0, cy0, cy1, ax, e0);
}

/* a fat bolt's set-up, the asm's records against lasers.c's (logged when they differ) */
static int setup_agrees(const LzFat *f, LzStyle *st)
{
    LzPass pc[2], pa[2];
    LzLine cc, ca;
    int clc, cla, n1, n2, w;
    lz_capsule_setup(f, 99, &pc[0]);   /* (one capsule, as the flames are made) */
    rv_capsule_setup(f, 99, &pa[0]);
    if (_fmemcmp(&pc[0], &pa[0], sizeof(LzPass))) {
        rv_log("bolts: rv_capsule_setup differs for %d,%d-%d,%d r %d", f->ax, f->ay, f->bx, f->by, f->r);
        return 0;
    }
    n1 = lz_fat_setup(f, st, pc, &cc, &clc);
    n2 = rv_fat_setup(f, &g_style, pa, &ca, (int __far *)&cla);
    if (n1 == n2 && clc == cla && !_fmemcmp(pc, pa, n1 * sizeof(LzPass)) && !(clc && _fmemcmp(&cc, &ca, sizeof cc)))
        return 1;
    rv_log("bolts: rv_fat_setup differs for %d,%d-%d,%d r %d: %d passes (C %d), core line %d (C %d)", f->ax, f->ay,
           f->bx, f->by, f->r, n2, n1, cla, clc);
    for (w = 0; w < n1 && w < n2; w++)
        if (_fmemcmp(&pc[w], &pa[w], sizeof(LzPass)))
            rv_log("bolts: pass %d: C top %d bot %d la %d lb %d ra %d rb %d c0 %ld m %ld hw %ld; asm %d %d %d %d %d %d %ld "
                   "%ld %ld", w, pc[w].top, pc[w].bot, pc[w].la, pc[w].lb, pc[w].ra, pc[w].rb, pc[w].c0, pc[w].m,
                   pc[w].hw, pa[w].top, pa[w].bot, pa[w].la, pa[w].lb, pa[w].ra, pa[w].rb, pa[w].c0, pa[w].m, pa[w].hw);
    return 0;
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
    bolts_tables();
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
    /* the fat bolts' set-ups, asm against C, at their own radii and every other, some end on */
    for (i = 0; i < nfc && !bad; i++)
        for (k = 12; k <= 64 && !bad; k += 4) {
            LzFat f = fc[i];
            LzStyle st = g_style;
            f.r = (short)k;
            if (i % 4 == 3) {
                f.bx = f.ax;
                f.by = f.ay;
            }
            if (!setup_agrees(&f, &st))
                bad = -1;
        }
    /* the draws, band by band (40 rows), with and without the extents: the lines and the fat bolts */
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
        for (i = 0; i < nfc; i++)
            fat_c(&fc[i], i % 4 == 3, band, y0, y1, ex);
        s1 = checksum(band, &drawn);
        _fmemset(band, 0, TEST_ROWS * SCR_W);
        if (cy0 < cy1) {
            rv_lines_draw(la, nla, UI_RED, band, y0, cy0, cy1, ax, e0);
            for (i = 0; i < nfa; i++)
                fat_asm(&fa[i], i % 4 == 3, band, y0, cy0, cy1, ax, e0);
        }
        s2 = checksum(band, &drawn);
        if (s1 != s2) {
            rv_log("bolts: the draws differ in rows %d-%d (%s the extents)", y0, y1, ex ? "with" : "without");
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
    LzPass p[FAT_MAX * 2];
    LzLine core[FAT_MAX];
    unsigned char np[FAT_MAX], hascore[FAT_MAX];
    unsigned char spans[12000];        /* the blit's sprites: each bolt's rows of runs, from sp_at[i] */
    short sp_top[FAT_MAX], sp_rows[FAT_MAX], sp_left[FAT_MAX];
    unsigned short sp_at[FAT_MAX], spans_used;
} __far *t_w;
#define t_f (t_w->f)
static int t_n, t_nl, t_nf, t_y0;

static void t_place(void) { t_nl = rv_bolts_place(t_b, t_n, &g_cam, t_l, 512, t_f, FAT_MAX, (int __far *)&t_nf); }
static void t_lines(void) { rv_lines_draw(t_l, t_nl, UI_RED, t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0); }

/* the fat bolts set up, as the build draws them */
static void t_setup(void)
{
    int i, n;
    for (i = 0; i < t_nf; i++) {
        t_w->np[i] = (unsigned char)rv_fat_setup(&t_f[i], &g_style, &t_w->p[i * 2], &t_w->core[i], (int __far *)&n);
        t_w->hascore[i] = (unsigned char)n;
    }
}

/* fat bolt i drawn into the timings' band */
static void t_draw_one(int i)
{
    int k;
    for (k = 0; k < t_w->np[i]; k++)
        rv_pass_draw(&t_w->p[i * 2 + k], t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0, lz_sqrt16);
    if (t_w->hascore[i])
        rv_lines_draw(&t_w->core[i], 1, UI_WHITE, t_band, t_y0, t_y0, t_y0 + TEST_ROWS, g_open, 0);
}

static void t_draw(void)
{
    int i;
    for (i = 0; i < t_nf; i++)
        t_draw_one(i);
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
        t_draw_one(i);
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

static void t_all_fat(void)
{
    t_place();
    t_setup();
    t_draw();
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
    bolts_tables();
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
    out[4] = time_of(t_draw, t_nf);
    rv_log("bolts: the timing's near bolts: %d of 12 on screen, radius %d..%d x16", t_nf, t_nf ? t_f[0].r : 0,
           t_nf ? t_f[t_nf - 1].r : 0);
    if (t_encode_each()) {              /* the same bolts pre-rendered, each a sprite of its own */
        long bl = time_of(t_blit_each, t_nf);
        rv_log("bolts: the same bolts as pre-rendered sprites, each its own, copied a row at a time "
               "(rv_blit_row, as ships_blit_view does): %ld.%02ld us a bolt (%u bytes of runs)", bl / 100, bl % 100,
               t_w->spans_used);
    }
    /* one algorithm: far bolts drawn fat (no line under any radius) */
    test_bolts(t_b, FAT_MAX, 0, 700, 40);
    t_n = FAT_MAX;
    camera(0, 0, 0, &cam, SCR_W / 2, SCR_H / 2, 400, t_y0, t_y0 + TEST_ROWS, 0);
    out[5] = time_of(t_all_fat, FAT_MAX);
    rv_log("bolts: a far bolt %ld.%02ld us to place, %ld.%02ld to draw (%d lines of %ld.%02ld rows); a near one "
           "%ld.%02ld to place, %ld.%02ld to set up, %ld.%02ld to draw; a far one drawn so %ld.%02ld in all",
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
