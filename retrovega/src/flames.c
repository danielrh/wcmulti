/* flames.c: the ships' engine flames (flames.h). */
#include <string.h>
#include "rv.h"
#include "bolts.h"
#include "flames.h"

#define FLAME_MIN_HALF16 64             /* a ship whose half box is under 4 px on screen has no flames drawn */
#define FLAME_LEN 5                     /* a flame's length, in its glow's radii ... */
#define FLAME_LEN_MIN 100               /* ... and at least this (1/1024ths of the half box) */

int g_flame_flicker = 0;

static LzPass __far g_fp[FLAME_PASSES];
static int g_nfp;
static unsigned char g_col[2][3];       /* [burn] the tail, the body, the core */
static unsigned g_frame;

/* the palette's entry nearest an sRGB colour (the palette as set: 6-bit) */
static unsigned char nearest(int r, int g, int b)
{
    const unsigned char *pal = shot_palette();
    long best = 0x7FFFFFFFL, d;
    int i, k = 0;
    for (i = 1; i < 256; i++) {
        int dr = pal[3 * i] * 4 - r, dg = pal[3 * i + 1] * 4 - g, db = pal[3 * i + 2] * 4 - b;
        d = (long)dr * dr * 2 + (long)dg * dg * 4 + (long)db * db * 3;
        if (d < best) {
            best = d;
            k = i;
        }
    }
    return (unsigned char)k;
}

void flames_init(void)
{
    bolts_tables();
    /* WC's flames (the user's pictures): a white-lavender root, a blue body, a deep blue-violet tail; the
     * afterburner's tail redder */
    g_col[0][0] = nearest(45, 32, 218);
    g_col[0][1] = nearest(90, 86, 227);
    g_col[0][2] = nearest(220, 219, 248);
    g_col[1][0] = nearest(190, 50, 170);
    g_col[1][1] = g_col[0][1];
    g_col[1][2] = g_col[0][2];
    rv_log("flames: colours %u %u %u, afterburner %u %u %u", g_col[0][0], g_col[0][1], g_col[0][2], g_col[1][0],
           g_col[1][1], g_col[1][2]);
}

void flames_frame(void)
{
    g_nfp = 0;
    g_frame++;
}

/* one capsule from a (x16) along t (x16) for frac/16 of it, radius r16 */
static void capsule(long ax, long ay, long tx, long ty, int frac, int r16, int colour)
{
    LzFat f;
    long bx = ax + tx * frac / 16, by = ay + ty * frac / 16;
    if (by < ay || (by == ay && bx < ax)) {   /* a: the upper end (the left of a level one) */
        long x = ax, y = ay;
        ax = bx; ay = by; bx = x; by = y;
    }
    f.ax = (short)ax; f.ay = (short)ay; f.bx = (short)bx; f.by = (short)by;
    f.r = (short)r16;
    f.pad = 0;
    rv_capsule_setup(&f, colour, &g_fp[g_nfp++]);
}

void flames_add(const RvShipType __far *t, const LfPlace *p, int nb, int nh, int scale256, int burn, FlameSet *out)
{
    LfViewRot rot;
    long half16;
    int k;
    out->first = (unsigned char)g_nfp;
    out->n = 0;
    out->front = 0;
    if (!t || !t->nengines || !p->on_screen)
        return;
    half16 = (long)t->lod0_px * scale256 / 32;   /* the half box on screen, x16 */
    if (half16 < FLAME_MIN_HALF16)
        return;
    lf_view_rot(p->view, nb, nh, &rot);
    for (k = 0; k < t->nengines && k < RV_ENGINES && g_nfp + 3 <= FLAME_PASSES; k++) {
        const signed char __far *e = t->engine[k];
        long ox, oy, oz, tx, ty, tz, ax, ay, len;
        int r16 = (int)((unsigned char)e[3] * half16 / 1024);
        len = (long)(unsigned char)e[3] * FLAME_LEN;
        if (len < FLAME_LEN_MIN)
            len = FLAME_LEN_MIN;
        if (burn)
            len *= 2;
        if (g_flame_flicker && ((g_frame + k) & 1))
            len = len * 7 / 8;
        if (r16 < BOLT_LINE16)
            r16 = BOLT_LINE16;
        if (r16 > LZ_RMAX16)
            r16 = LZ_RMAX16;
        /* the nozzle on the sprite as drawn, and the tail's way (the renderer's -z) */
        lf_sprite_point(&rot, p->roll, e[0] * half16 / 127, e[1] * half16 / 127, e[2] * half16 / 127, &ox, &oy, &oz);
        lf_sprite_point(&rot, p->roll, 0, 0, -len * half16 / 1024, &tx, &ty, &tz);
        ax = p->sx * 16L + 8 + ox;
        ay = p->sy * 16L + 8 + oy;
        out->front = (unsigned char)(tz > 0);
        /* a capsule is the same width all along, so the taper is made of three: a thin dark tail the whole
         * length, a wide blue root over its first part, a white core at the nozzle */
        capsule(ax, ay, tx, ty, 16, r16 / 2 >= BOLT_LINE16 ? r16 / 2 : BOLT_LINE16, g_col[burn != 0][0]);
        capsule(ax, ay, tx, ty, 7, r16, g_col[burn != 0][1]);
        if (r16 * 7 / 16 >= BOLT_LINE16)
            capsule(ax, ay, tx, ty, burn ? 5 : 4, r16 * 7 / 16, g_col[burn != 0][2]);
    }
    out->n = (unsigned char)(g_nfp - out->first);
}

void flames_draw(const FlameSet *f, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int run0, run1, k;
    if (!f->n || !vid_run(&run0, &run1) || !bolts_rows(&y0, &y1, &ext, &ext_y0, ext_rows))
        return;
    for (k = f->first; k < f->first + f->n; k++)
        rv_pass_draw(&g_fp[k], vid_row(run0), run0, y0, y1, ext, ext_y0, lz_sqrt16);
}
