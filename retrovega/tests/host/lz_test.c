/* lz_test.c: the laser bolts' module against brute force, on the host
 * (tests/host/run.sh, built once for each of lasers.h's LZ_NEAR), and preview
 * sheets: lz_test SHEET.ppm draws bolts at every size and a few angles in the
 * flight palette's colours, lz_test SHEET.ppm SHAPES.ppm also the fat ones
 * at every size and angle, the last of each row end on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lasers.h"

static int fails;
#define CHECK(c, ...) do { if (c) printf("ok   " __VA_ARGS__); else { printf("FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static unsigned char g_band[400 * 640];

/* the colours the bolts use, from the flight palette: far lines and the body UI_RED, the middle UI_WHITE */
#define C_LINE 15
#define C_BODY 15
#define C_CORE 7
static const LzStyle g_style = { C_LINE, C_BODY, C_CORE, 0, 12 };

/* a fat bolt as the build draws them */
static void draw_fat(const LzFat *f, int y0, int y1)
{
    int k, n;
#if LZ_POLYGONS
    LzPoly pl[2];
    LzLine ln[3];
    int nl;
    n = lz_slab_setup(f, &g_style, pl, ln, &nl);
    for (k = 0; k < n; k++)
        lz_poly_draw(&pl[k], g_band, 0, y0, y1, 0, 0, 0);
    for (k = 0; k < nl; k++)
        lz_lines_draw(&ln[k], 1, k == 1 || nl == 1 ? C_CORE : C_BODY, g_band, 0, y0, y1, 0, 0, 0);
#else
    LzPass p[2];
    LzLine core;
    int cl;
    n = lz_fat_setup(f, &g_style, p, &core, &cl);
    for (k = 0; k < n; k++)
        lz_pass_draw(&p[k], g_band, 0, y0, y1, 0, 0, 0);
    if (cl)
        lz_lines_draw(&core, 1, C_CORE, g_band, 0, y0, y1, 0, 0, 0);
#endif
}

#if LZ_NEAR == LZ_NEAR_CAPSULE
/* the distance from a pixel's centre to the segment, in pixels */
static double seg_dist(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}
#endif

static void basis_identity(long *f, long *r, long *u)
{
    f[0] = 0; f[1] = 0; f[2] = 256;
    r[0] = 256; r[1] = 0; r[2] = 0;
    u[0] = 0; u[1] = 256; u[2] = 0;
}

static void write_ppm(const char *path, int w, int h)
{
    static const unsigned char pal[256][3] = {
        [0] = { 0, 0, 0 }, [2] = { 60, 60, 64 }, [7] = { 255, 255, 255 }, [15] = { 210, 44, 32 },
        [129] = { 101, 14, 0 }, [130] = { 134, 20, 0 }, [131] = { 160, 24, 0 }, [143] = { 255, 255, 255 },
    };
    FILE *f = fopen(path, "wb");
    int i;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (i = 0; i < w * h; i++)
        fwrite(pal[g_band[i]], 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    LzCam cam;
    long f[3], r[3], u[3];
    LzBolt b[8];
    LzLine lines[64];
    LzFat fats[64];
    int nl, nf, i, k, y;
    printf("     fat bolts drawn as " LZ_NEAR_NAME "\n");
    lz_init();
#if LZ_NEAR == LZ_NEAR_CAPSULE
    CHECK(lz_sqrt16[0] == 1 && lz_sqrt16[4] == 4 && lz_sqrt16[LZ_SQRT_N - 1] == LZ_RMAX16,
          "the root table (sqrt 16 = 4, sqrt %d = %d)", LZ_RMAX16 * LZ_RMAX16, LZ_RMAX16);
#endif

    /* the camera at the origin looking down +z; bolts 2 km in radius, 6 km half length */
    basis_identity(f, r, u);
    lz_camera(&cam, 0, 0, 0, f, r, u, 320, 200, 400, 32, 200, 40000, 64, 12, 0, 400);
    memset(b, 0, sizeof b);
    /* end on, 400 km ahead: a disc of radius 2 px at the centre */
    b[0].z = 256L * 400; b[0].hz = 16 * 6;
    /* across the view, 400 km ahead, left of centre: 12 km x 400 / 400 = 12 px long, level */
    b[1].x = -256L * 100; b[1].z = 256L * 400; b[1].hx = 16 * 6;
    /* behind the camera: gone */
    b[2].z = -256L * 400; b[2].hx = 16 * 6;
    /* far, 4,000 km: a line */
    b[3].z = 256L * 4000; b[3].hx = 16 * 6; b[3].hy = 16 * 3;
    /* half through the near plane: cut there */
    b[4].z = 256L * 200; b[4].hz = 16 * 6; b[4].hx = 16 * 6;
    lz_place(b, 5, &cam, lines, &nl, 64, fats, &nf, 64);
    CHECK(nf == 3 && nl == 1, "three fat bolts, one line, one culled (got %d fat, %d lines)", nf, nl);
    CHECK(fats[0].ax == 320 * 16 && fats[0].ay == 200 * 16 && fats[0].bx == fats[0].ax && fats[0].r == 32,
          "the end-on bolt is a dot at the centre, radius 2 px (%d,%d r %d)", fats[0].ax, fats[0].ay, fats[0].r);
    CHECK(fats[1].ay == fats[1].by && fats[1].bx - fats[1].ax == 12 * 16 && fats[1].ax == (320 - 100 - 6) * 16,
          "the crossing bolt runs level from x %d to %d", fats[1].ax / 16, fats[1].bx / 16);
    CHECK(fats[2].r >= 60 && fats[2].r <= 64, "the cut bolt is about 4 px in radius, no more (%d)", fats[2].r);
    CHECK(lines[0].y0 <= 200 && lines[0].y1 > 200, "the far one is a line through the centre's row (rows %d-%d)",
          lines[0].y0, lines[0].y1);

    /* lines: every row they cross shows a pixel, rows touch, for every slope */
    {
        int bad_gap = 0, bad_empty = 0, total = 0;
        srand(7);
        for (k = 0; k < 4000; k++) {
            int ax = 1600 + rand() % 6400, ay = 1600 + rand() % 3200, len = rand() % 400, ang = rand();
            int bx = ax + (int)(len * cos(ang)), by = ay + (int)(len * sin(ang)), prev_lo = -1, prev_hi = -1;
            LzLine l;
            if (by < ay || (by == ay && bx < ax)) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
            lz_line_of(ax, ay, bx, by, &l);
            memset(g_band, 0, sizeof g_band);
            lz_lines_draw(&l, 1, 1, g_band, 0, 0, 400, 0, 0, 0);
            for (y = l.y0; y < l.y1; y++) {
                int x, lo = -1, hi = -1;
                for (x = 0; x < 640; x++)
                    if (g_band[y * 640 + x]) {
                        if (lo < 0) lo = x;
                        hi = x;
                    }
                total++;
                if (lo < 0) { bad_empty++; continue; }
                if (prev_lo >= 0 && (lo > prev_hi + 1 || hi < prev_lo - 1))
                    bad_gap++;
                prev_lo = lo; prev_hi = hi;
            }
        }
        CHECK(bad_empty == 0, "4000 lines: every row crossed has a pixel (%d of %d rows empty)", bad_empty, total);
        CHECK(bad_gap == 0, "4000 lines: each row's run touches the last (%d gaps)", bad_gap);
    }

#if LZ_NEAR == LZ_NEAR_CAPSULE
    /* the capsule against brute force: a pixel is drawn iff its centre is within r of the segment, but for
     * those within a sixteenth of a pixel of the edge (the table and the truncations) */
    {
        int wrong = 0, edge = 0, total = 0, asym = 0, drawn = 0;
        srand(11);
        for (k = 0; k < 3000; k++) {
            LzFat fa, fb;
            LzPass p[2], q[2];
            static unsigned char one[400 * 640];
            int ax = 3200 + rand() % 3200, ay = 1600 + rand() % 3200, len = rand() % 600, ang = rand();
            int rr = 8 + rand() % (LZ_RMAX16 - 7);
            int bx = ax + (int)(len * cos(ang)), by = ay + (int)(len * sin(ang));
            LzStyle st = { 1, 1, 1, 0, 32767 };   /* one pass at the full radius (the core a line, not drawn) */
            LzLine core;
            int cl, x;
            if (by < ay || (by == ay && bx < ax)) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
            fa.ax = ax; fa.ay = ay; fa.bx = bx; fa.by = by; fa.r = rr;
            memset(g_band, 0, sizeof g_band);
            lz_fat_setup(&fa, &st, p, &core, &cl);
            lz_pass_draw(&p[0], g_band, 0, 0, 400, 0, 0, 0);
            for (y = (ay - rr) / 16 - 2; y <= (by + rr) / 16 + 2; y++)
                for (x = (ax < bx ? ax : bx) / 16 - rr / 16 - 2; x <= (ax < bx ? bx : ax) / 16 + rr / 16 + 2; x++) {
                    double d = seg_dist(x + 0.5, y + 0.5, ax / 16.0, ay / 16.0, bx / 16.0, by / 16.0) - rr / 16.0;
                    int in = g_band[y * 640 + x] != 0;
                    total++;
                    if (fabs(d) < 0.12) { edge++; continue; }
                    if (in != (d < 0))
                        wrong++;
                }
            /* its mirror image (x -> 639 - x) drawn must be the same picture mirrored, but for edge pixels now and then */
            memcpy(one, g_band, sizeof one);
            fb.ay = ay; fb.by = by; fb.r = rr;
            if (by == ay) { fb.ax = 10240 - bx; fb.bx = 10240 - ax; }
            else { fb.ax = 10240 - ax; fb.bx = 10240 - bx; }
            memset(g_band, 0, sizeof g_band);
            lz_fat_setup(&fb, &st, q, &core, &cl);
            lz_pass_draw(&q[0], g_band, 0, 0, 400, 0, 0, 0);
            for (y = 0; y < 400; y++)
                for (x = 0; x < 640; x++) {
                    drawn += one[y * 640 + x] != 0;
                    asym += one[y * 640 + x] != g_band[y * 640 + 639 - x];
                }
        }
        CHECK(wrong == 0, "3000 capsules: no pixel off the brute force (%d wrong, %d at the edge, of %d)", wrong, edge, total);
        CHECK(asym * 200 < drawn, "the mirror images agree but for under 0.5%% of pixels (%d of %d)", asym, drawn);
    }
#else

    /* the polygons against brute force: a pixel is drawn iff its centre is inside, but for those within a
     * tenth of a pixel of an edge; random bolts, some end on */
    {
        int wrong = 0, edge = 0, total = 0;
        srand(13);
        for (k = 0; k < 3000; k++) {
            int ax = 3200 + rand() % 3200, ay = 1600 + rand() % 3200, len = k % 8 ? rand() % 600 : k % 24, ang = rand();
            int bx = ax + (int)(len * cos(ang)), by = ay + (int)(len * sin(ang)), x, j, n;
            LzFat fa;
            LzPoly pl[2];
            LzLine ln[3];
            if (by < ay || (by == ay && bx < ax)) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
            fa.ax = ax; fa.ay = ay; fa.bx = bx; fa.by = by; fa.r = 25 + rand() % 40;
            n = lz_slab_setup(&fa, &g_style, pl, ln, &j);
            if (n != 2) { wrong++; continue; }
            memset(g_band, 0, sizeof g_band);
            lz_poly_draw(&pl[0], g_band, 0, 0, 400, 0, 0, 0);
            for (y = pl[0].top - 2; y < pl[0].bot + 2; y++)
                for (x = 0; x < 640; x++) {
                    double px = x + 0.5, py = y + 0.5, dmin = 1e9;
                    int i, pos = 0, neg = 0, in = g_band[y * 640 + x] != 0;
                    for (i = 0; i < pl[0].n; i++) {
                        double x0 = pl[0].x[i] / 16.0, y0 = pl[0].y[i] / 16.0;
                        double x1 = pl[0].x[(i + 1) % pl[0].n] / 16.0, y1 = pl[0].y[(i + 1) % pl[0].n] / 16.0;
                        double l = hypot(x1 - x0, y1 - y0), d;
                        if (l < 1e-9) continue;
                        d = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) / l;
                        if (d > 0) pos++; else if (d < 0) neg++;
                        if (fabs(d) < dmin) dmin = fabs(d);
                    }
                    if (!in && dmin > 30) continue;
                    total++;
                    if (dmin < 0.1) { edge++; continue; }
                    if (in != (pos == 0 || neg == 0))
                        wrong++;
                }
        }
        CHECK(wrong == 0, "3000 polygons: no pixel off the brute force (%d wrong, %d at an edge, of %d)", wrong, edge,
              total);
    }
#endif

    /* the preview: sizes along, angles down; then a field of bolts in perspective */
    if (argc > 1) {
        memset(g_band, 0, sizeof g_band);
        for (k = 0; k < 9; k++) {           /* radius x16: 4 .. 64 */
            int rr = k == 0 ? 6 : k * 8, a;
            for (a = 0; a < 5; a++) {
                double ang = a * M_PI / 10;
                int cx = 36 + k * 70, cy = 34 + a * 60, half = rr * 8;   /* 16 radii long: 64 x 8 px at the largest */
                LzFat fa;
                int ax = (int)(cx * 16 - half * cos(ang)), ay = (int)(cy * 16 - half * sin(ang));
                int bx = (int)(cx * 16 + half * cos(ang)), by = (int)(cy * 16 + half * sin(ang));
                if (by < ay || (by == ay && bx < ax)) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
                fa.ax = ax; fa.ay = ay; fa.bx = bx; fa.by = by; fa.r = rr;
                if (rr < 12) {
                    LzLine l;
                    lz_line_of(ax, ay, bx, by, &l);
                    lz_lines_draw(&l, 1, C_LINE, g_band, 0, 0, 400, 0, 0, 0);
                } else {
                    draw_fat(&fa, 0, 400);
                }
            }
        }
        /* a field: bolts at 250 .. 6,000 km, every way, through the camera */
        {
            static LzBolt field[400];
            static LzLine fl[400];
            static LzFat ff[400];
            srand(5);
            for (i = 0; i < 400; i++) {
                double th = rand() * 2 * M_PI / RAND_MAX, ph = acos(2.0 * rand() / RAND_MAX - 1);
                long dist = 250 + (long)(pow(rand() / (double)RAND_MAX, 2) * 5750);
                field[i].x = (long)((rand() % 2000 - 1000) * dist / 1000.0 * 256 * 0.55);
                field[i].y = (long)((rand() % 2000 - 1000) * dist / 1000.0 * 256 * 0.12);
                field[i].z = dist * 256;
                field[i].hx = (short)(192 * sin(ph) * cos(th));   /* 12 km half length, as the flight's */
                field[i].hy = (short)(192 * sin(ph) * sin(th));
                field[i].hz = (short)(192 * cos(ph));
            }
            lz_camera(&cam, 0, 0, 0, f, r, u, 320, 400 - 50, 400, 32, 200, 40000, 64, 12, 320, 400);
            lz_place(field, 400, &cam, fl, &nl, 400, ff, &nf, 400);
            lz_lines_draw(fl, nl, C_LINE, g_band, 0, 320, 400, 0, 0, 0);
            for (i = 0; i < nf; i++)
                draw_fat(&ff[i], 320, 400);
            printf("     the field: %d lines, %d fat\n", nl, nf);
        }
        write_ppm(argv[1], 640, 400);
    }
    if (argc > 2) {
        /* the fat bolts: radius growing down (16, 24, 32, 44, 54, 64 x16), angles across, the last end on;
         * 12 radii between the ends, as the flight's bolts are */
        int a;
        memset(g_band, 0, sizeof g_band);
        for (k = 0; k < 6; k++) {
            int rr = k == 0 ? 16 : k == 1 ? 24 : k == 2 ? 32 : k == 3 ? 44 : k == 4 ? 54 : 64;
            for (a = 0; a < 6; a++) {
                double ang = a * M_PI / 10 + 0.05;
                int cx = 40 + a * 100, cy = 34 + k * 62, half = a == 5 ? 0 : rr * 6;
                LzFat fa;
                int ax = (int)(cx * 16 - half * cos(ang)), ay = (int)(cy * 16 - half * sin(ang));
                int bx = (int)(cx * 16 + half * cos(ang)), by = (int)(cy * 16 + half * sin(ang));
                if (by < ay || (by == ay && bx < ax)) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
                fa.ax = ax; fa.ay = ay; fa.bx = bx; fa.by = by; fa.r = rr;
                draw_fat(&fa, 0, 400);
            }
        }
        write_ppm(argv[2], 640, 400);
    }
    printf(fails ? "LZ FAILED (%d, " LZ_NEAR_NAME ")\n" : "LZ OK (" LZ_NEAR_NAME ")\n", fails);
    return fails != 0;
}
