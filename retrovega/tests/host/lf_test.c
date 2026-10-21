/* lf_test.c: the lightfield module against reference results, on the host
 * (tests/host/run.sh: gcc -I src lightfield.c lf_test.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lightfield.h"

static int fails;
#define CHECK(c, ...) do { if (c) printf("ok   " __VA_ARGS__); else { printf("FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static void basis(LfBasis *b, long fx, long fy, long fz, long rx, long ry, long rz, long ux, long uy, long uz)
{
    b->front.x = fx; b->front.y = fy; b->front.z = fz;
    b->right.x = rx; b->right.y = ry; b->right.z = rz;
    b->up.x = ux; b->up.y = uy; b->up.z = uz;
}

int main(void)
{
    int i, worst = 0;
    LfBasis cam, ship;
    LfVec rel;
    LfPlace p;

    /* trigonometry against libm */
    for (i = 0; i < 1024; i++) {
        int e = abs((int)lf_sin(i) - (int)lround(sin(i * M_PI / 512) * 16384));
        if (e > worst) worst = e;
    }
    CHECK(worst <= 1, "lf_sin within 1/16384 of sin (worst %d)", worst);
    worst = 0;
    for (i = 0; i < 360; i += 7) {
        double a = i * M_PI / 180;
        int got = lf_atan2((long)lround(sin(a) * 1000), (long)lround(cos(a) * 1000));
        int want = (int)lround(i * 1024.0 / 360) % 1024, e = abs(got - want);
        if (e > 512) e = 1024 - e;
        if (e > worst) worst = e;
    }
    CHECK(worst <= 2, "lf_atan2 within 2/1024 of atan2 (worst %d)", worst);
    CHECK(lf_sqrt(1000000UL) == 1000 && lf_sqrt(2) == 1 && lf_sqrt(0) == 0, "lf_sqrt");

    /* the view for a direction: nose toward the viewer, the sides, above, below */
    CHECK(lf_view_of(0, 0, 256, 5, 7) == 15, "the viewer ahead of the nose sees view 15 (band 0, heading 0)");
    CHECK(lf_view_of(256, 0, 0, 5, 7) == 18, "the viewer at +x (the ship's left) sees heading 90: view 18");
    CHECK(lf_view_of(0, 0, -256, 5, 7) == 21, "the viewer astern sees heading 180: view 21");
    CHECK(lf_view_of(-256, 0, 0, 5, 7) == 49, "the viewer at -x sees heading 270: view 49 (37 + band 2 x 5 + 2)");
    CHECK(lf_view_of(0, 256, 0, 5, 7) == 0 && lf_view_of(0, -256, 0, 5, 7) == 36, "straight above is the top, below the bottom");
    CHECK(lf_view_of(0, 128, 222, 5, 7) == 8, "30 degrees up, ahead: band +30, heading 0: view 8");
    CHECK(lf_view_of(0, -222, 128, 5, 7) == 29, "60 degrees down, ahead: band -60, heading 0: view 29");
    CHECK(lf_view_of(128, 0, 222, 5, 7) == 16, "30 degrees to the right: heading 30: view 16");
    CHECK(lf_view_of(-128, 0, -222, 5, 7) == 47, "heading 210: view 47 (37 + band 2 x 5 + 0)");

    /* placement: the camera looks down +z with up = +y (down the screen) */
    basis(&cam, 0, 0, 256, 256, 0, 0, 0, 256, 0);
    ship = cam;
    rel.x = 0; rel.y = 0; rel.z = 256 * 10;                   /* 10 km dead ahead */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.on_screen && p.sx == 320 && p.sy == 100, "a ship dead ahead sits at the centre (%d,%d)", p.sx, p.sy);
    CHECK(p.view == 21, "flying the same way, its tail is toward us: view 21 (got %d)", p.view);
    CHECK(p.roll == 0, "and it is not turned (roll %d)", p.roll);
    CHECK(p.depth == 2560, "depth is the distance along the view axis (%ld)", p.depth);
    rel.x = 256 * 5; rel.z = 256 * 10;
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.on_screen && p.sx == 520 && p.sy == 100, "5 km right at 10 km: 200 px right (%d,%d)", p.sx, p.sy);
    rel.x = 0; rel.y = 256 * 5;
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.sy == -100, "5 km along the camera's up: 200 px up the screen (%d)", p.sy);
    rel.y = 0; rel.z = -256;
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(!p.on_screen, "a ship behind the camera is not on screen");
    rel.z = 256 * 10;
    basis(&ship, 0, 0, -256, -256, 0, 0, 0, 256, 0);          /* coming at us */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.view == 15 && p.roll == 0, "a ship flying toward us shows its nose: view 15, roll 0 (got %d, %d)", p.view, p.roll);
    basis(&ship, 0, 0, 256, 0, 256, 0, -256, 0, 0);           /* rolled so its up is our left */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.roll == 768, "a ship whose up points to screen-left is turned 270 clockwise (got %d)", p.roll);
    basis(&ship, 0, 0, 256, 0, -256, 0, 256, 0, 0);           /* its up is our right */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.roll == 256, "a ship whose up points to screen-right is turned 90 clockwise (got %d)", p.roll);
    basis(&ship, 0, 0, 256, -256, 0, 0, 0, -256, 0);          /* upside down */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.roll == 512, "an upside-down ship is turned 180 (got %d)", p.roll);
    basis(&ship, 256, 0, 0, 0, 0, -256, 0, 256, 0);           /* crossing to our right: its right side faces us */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.view == 49, "a ship crossing right shows its right side, nose right: view 49 (got %d)", p.view);
    basis(&ship, 0, 256, 0, 256, 0, 0, 0, 0, -256);           /* nose up the screen, its top toward us */
    lf_place(&rel, &cam, &ship, 320, 100, 400, 5, 7, &p);
    CHECK(p.view == 0 && p.roll == 256, "nose up the screen seen from above: the top view, turned 90 so the nose (drawn left) points up (got %d, %d)", p.view, p.roll);

    /* the 15-degree lattice (11 bands, 13 headings): 145 views, the mirrored
     * headings at 145..; the pairs of views */
    CHECK(LF_LATTICE_VIEWS(5, 7) == 62 && LF_LATTICE_VIEWS(11, 13) == 266, "lattice sizes: 62 and 266");
    CHECK(lf_view_of(0, 0, 256, 11, 13) == 1 + 5 * 13, "15 degrees: ahead is band 5 (0 degrees), heading 0: view 66");
    CHECK(lf_view_of(256, 0, 0, 11, 13) == 1 + 5 * 13 + 6, "15 degrees: the left side is heading 90: view 72");
    CHECK(lf_view_of(0, 256, 0, 11, 13) == 0 && lf_view_of(0, -256, 0, 11, 13) == 1 + 11 * 13, "15 degrees: top and bottom");
    CHECK(lf_view_of(0, 69, 256, 11, 13) == 1 + 4 * 13, "15 degrees up, ahead: band 4 (+15): view 53");
    CHECK(lf_view_of(-69, 0, 256, 11, 13) == 2 + 11 * 13 + 5 * 11 + 10, "15 degrees to the right: heading 345: the last mirrored view of band 5");
    CHECK(lf_pair_of(16, 5, 7) == 37 + 2 * 5 + 4 && lf_pair_of(37 + 2 * 5 + 4, 5, 7) == 16, "30 degrees: heading 30 <-> 330 in band 0");
    CHECK(lf_pair_of(15, 5, 7) == 15 && lf_pair_of(21, 5, 7) == 21 && lf_pair_of(0, 5, 7) == 0 && lf_pair_of(36, 5, 7) == 36, "nose on, tail on, top, bottom are their own pairs");
    CHECK(lf_pair_of(1 + 5 * 13 + 1, 11, 13) == 2 + 11 * 13 + 5 * 11 + 10, "15 degrees: heading 15 <-> 345");
    {
        int v, ok = 1;
        for (v = 0; v < 266 && ok; v++)
            if (lf_pair_of(lf_pair_of(v, 11, 13), 11, 13) != v) ok = 0;
        CHECK(ok, "every view's pair's pair is itself (266 views)");
    }

    /* the scaler: a 4 x 4 picture to 2 x 2 keeps every other pixel */
    {
        static const unsigned char src[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        unsigned short map[2];
        unsigned char out[2] = { 0, 0 };
        lf_colmap(4, 2, map);
        lf_scale_row(src + 8, map, 0, 2, out);
        CHECK(map[0] == 0 && map[1] == 2 && out[0] == 9 && out[1] == 11, "4 -> 2: columns 0 and 2 (%d %d)", out[0], out[1]);
        lf_colmap(2, 5, map + 0);        /* (only two entries fit: check the first two) */
        CHECK(map[0] == 0 && map[1] == 0, "2 -> 5: the first two output columns both read column 0");
        out[0] = 7;
        lf_scale_row(src, map, 0, 1, out);
        CHECK(out[0] == 1, "a drawn pixel replaces what was there");
        {
            static const unsigned char clear[4] = { 0, 0, 0, 0 };
            out[0] = 7;
            lf_scale_row(clear, map, 0, 1, out);
            CHECK(out[0] == 7, "a clear pixel leaves it");
        }
    }

    /* the rotator: an L (3 wide, 4 tall) turned a quarter clockwise */
    {
        /* .1.   origin at (1, 1) (the 2)    turned a quarter clockwise, down becomes left
         * .2.                                and right becomes down: the stem lies leftward
         * .3.                                (4 3 2 1, the 1 right of the origin) and the
         * .45                                foot hangs below the 4 */
        static const unsigned char L[12] = { 0, 1, 0,   0, 2, 0,   0, 3, 0,   0, 4, 5 };
        unsigned char out[64];
        LfRot r;
        int y, x, ok = 1;
        lf_rotate_setup(3, 4, 1, 1, 256, 256, &r);
        CHECK(r.ow >= 4 && r.oh >= 3 && r.ow <= 6 && r.oh <= 5, "the turned L is about 4 wide and 3 tall (%dx%d, origin %d,%d)", r.ow, r.oh, r.ocx, r.ocy);
        memset(out, 0, sizeof out);
        for (y = 0; y < r.oh; y++)
            lf_rotate_row(L, 3, 4, &r, y, 0, r.ow, out + y * 8);
        for (x = 0; x < 4; x++)
            if (out[r.ocy * 8 + r.ocx + 1 - x] != 1 + x) ok = 0;
        if (out[(r.ocy + 1) * 8 + r.ocx - 2] != 5) ok = 0;
        CHECK(ok, "a quarter turn clockwise lays the stem leftward (1 right of the origin) and hangs the foot below the 4");
        if (!ok)
            for (y = 0; y < r.oh; y++) {
                for (x = 0; x < r.ow; x++) printf("%d", out[y * 8 + x]);
                printf("\n");
            }
        lf_rotate_setup(3, 4, 1, 1, 0, 128, &r);
        memset(out, 0, sizeof out);
        for (y = 0; y < r.oh; y++)
            lf_rotate_row(L, 3, 4, &r, y, 0, r.ow, out + y * 8);
        CHECK(r.ow <= 4 && r.oh <= 4 && out[r.ocy * 8 + r.ocx] != 0, "at half scale and no turn the origin pixel is drawn in a %dx%d image", r.ow, r.oh);
        lf_rotate_setup(3, 4, 1, 1, 0, 256, &r);
        memset(out, 0, sizeof out);
        for (y = 0; y < r.oh; y++)
            lf_rotate_row(L, 3, 4, &r, y, 0, r.ow, out + y * 8);
        ok = 1;
        for (y = 0; y < 4; y++)
            for (x = 0; x < 3; x++)
                if (out[(r.ocy - 1 + y) * 8 + r.ocx - 1 + x] != L[y * 3 + x]) ok = 0;
        CHECK(ok, "no turn and full scale reproduces the source");
    }
    /* the span of a turned row holds every pixel the checked kernel draws, and
     * nothing outside the source: on a solid 7 x 5 block at many turns and scales */
    {
        unsigned char block[35], out[160], ok = 1;
        int roll, sc, y, x, xs, xe;
        LfRot r;
        memset(block, 9, sizeof block);
        for (roll = 0; roll < 1024 && ok; roll += 37)
            for (sc = 64; sc <= 512 && ok; sc += 112) {
                lf_rotate_setup(7, 5, 3, 2, roll, sc, &r);
                if (r.ow > 160) continue;
                for (y = 0; y < r.oh && ok; y++) {
                    int has = lf_rotate_span(&r, 7, 5, y, 0, r.ow, &xs, &xe);
                    memset(out, 0, sizeof out);
                    lf_rotate_row(block, 7, 5, &r, y, 0, r.ow, out);
                    for (x = 0; x < r.ow; x++) {
                        int drawn = out[x] != 0, inside = has && x >= xs && x < xe;
                        if (drawn != inside) {
                            printf("     roll %d scale %d row %d column %d: drawn %d, in the span %d (%d..%d)\n", roll, sc, y, x, drawn, inside, xs, xe);
                            ok = 0;
                            break;
                        }
                    }
                }
            }
        CHECK(ok, "lf_rotate_span is exactly where the turned row draws, at every turn and scale tried");
    }
    printf("%s (%d failures)\n", fails ? "LF FAILED" : "LF OK", fails);
    return fails != 0;
}
