/* missiles.c: the flight's missiles (missiles.h).
 *
 * Each missile is bolts' shapes through the camera (lz_project, the bolts'
 * own math): the whole of it (whether it is a line, its radius, its depth),
 * its three parts (the rear quarter red, the body dark gray, the tip bright
 * red: the user's colours) and its flame (MISSILE_FLAME16 behind the tail,
 * its head the tip).  Under the bolts' line radius it is a line, gray, with
 * the flame's brightest colour on its tail's pixel; over it, each part is the
 * bolts' two nested capsules (rv_fat_setup: its colour, its middle lighter),
 * all of the whole's radius, the farthest drawn first; two fins (lines swept
 * back from the rear, across the body on the screen: a pair of lines each
 * when it is 4 px thick or more; none end on) under them; the flame is the
 * nearest baked one (flames_add_at) from the tail, the body's radius at its
 * root.  A flame pointing toward the camera is drawn last (it hides the
 * tail), one pointing away first.  The missiles are drawn farthest first.
 * Not as exact as the ships (the user: they are seen for moments). */
#include <string.h>
#include "rv.h"
#include "music.h"
#include "bolts.h"
#include "flames.h"
#include "missiles.h"

#define M_LINE 0
#define M_FAT 1
#define PARTS 3

typedef struct {
    LzPass pass[2 * PARTS];             /* each part's capsules (one or two): the rear's, the body's, the tip's */
    LzLine core[PARTS];                 /* a part's middle when too thin for a capsule */
    LzLine line[4];                     /* the far missile ([0]); or its fins */
    FlameSet fl;
    long z;                             /* its depth (sorting) */
    short tail_x, tail_y;               /* the far one's tail pixel */
    unsigned char kind, flame, head_far, nfins;
    unsigned char npass[PARTS], core_line[PARTS];
} MissileDraw;

static Missile __far g_m[MISSILE_MAX];
static MissileDraw __far g_d[MISSILE_MAX];
static int __far g_nd, __far g_nlines, __far g_nfats;
static LzCam __far g_cam;
static unsigned long __far g_t_place, __far g_t_draw, __far g_frames_on;
static const LzStyle __far g_style[PARTS] = {   /* the line, the body, its middle (palette.json's 98 a dark red, 100
                                           the brightest) */
    { UI_RED, 98, UI_RED, 0, BOLT_LINE16 }, { UI_GREY4, UI_GREY2, UI_GREY4, 0, BOLT_LINE16 },
    { UI_RED, UI_RED, 100, 0, BOLT_LINE16 }
};
static const short __far g_part[PARTS][2] = {   /* each part's middle along the missile and its half length (16ths
                                           of a km) */
    { -MISSILE_HALF16 + MISSILE_REAR16 / 2, MISSILE_REAR16 / 2 },
    { (MISSILE_REAR16 - MISSILE_TIP16) / 2, MISSILE_HALF16 - (MISSILE_REAR16 + MISSILE_TIP16) / 2 },
    { MISSILE_HALF16 - MISSILE_TIP16 / 2, MISSILE_TIP16 / 2 }
};
#define FIN_COLOUR UI_GREY4
#define TAIL_PIXEL 159                  /* the far one's flame: palette.json's pale violet (255, 180, 255) */

void missiles_init(void)
{
    _fmemset(g_m, 0, sizeof g_m);
    g_nd = g_nlines = g_nfats = 0;
    g_t_place = g_t_draw = g_frames_on = 0;
}

void missiles_ticks(unsigned long out[3])
{
    out[0] = g_t_place;
    out[1] = g_t_draw;
    out[2] = g_frames_on;
}

Missile __far *missiles_pool(void) { return g_m; }

int missiles_on_screen(int *lines, int *fats)
{
    *lines = g_nlines;
    *fats = g_nfats;
    return g_nd;
}

/* the ends as a capsule wants them: a the upper (the left when level), as lz_place orders a bolt's */
static void fat_of(const LzEnds *e, long r16, LzFat __far *f)
{
    if (e->hy < e->ty || (e->hy == e->ty && e->hx <= e->tx)) {
        f->ax = (short)e->hx; f->ay = (short)e->hy; f->bx = (short)e->tx; f->by = (short)e->ty;
    } else {
        f->ax = (short)e->tx; f->ay = (short)e->ty; f->bx = (short)e->hx; f->by = (short)e->hy;
    }
    f->r = (short)r16;
    f->pad = 0;
}

/* a bolt of the missile's: its middle `along` sixteenths of a km down the missile's way from the missile's, its
 * half axis `half` sixteenths along the way (negative: backward) */
static void part(const Missile __far *m, long along, int half, LzBolt *b)
{
    b->x = m->x + m->dx * along / 16;
    b->y = m->y + m->dy * along / 16;
    b->z = m->z + m->dz * along / 16;
    b->hx = (short)((long)m->dx * half / 256);
    b->hy = (short)((long)m->dy * half / 256);
    b->hz = (short)((long)m->dz * half / 256);
}

/* a line between two points x16, its upper end first (as lz_line_of wants) */
static void line_of(long ax, long ay, long bx, long by, LzLine __far *l)
{
    if (ay > by || (ay == by && ax > bx))
        lz_line_of((int)bx, (int)by, (int)ax, (int)ay, l);
    else
        lz_line_of((int)ax, (int)ay, (int)bx, (int)by, l);
}

/* its fins on the screen: from the body's edge 30% of the way from the tail, swept back out to 2.3 radii from its
 * middle near the tail; a second line beside each a pixel back when it is 4 px thick or more; none end on (its
 * axis on the screen under 3 radii) */
static void fins(MissileDraw __far *d, const LzEnds *e)
{
    long ax = e->hx - e->tx, ay = e->hy - e->ty, sx = ax, sy = ay, len, px, py, r = e->r16, bx, by;
    int k, sh = 0, pairs = r >= 64 ? 2 : 1, j;
    d->nfins = 0;
    while (sx > 16384 || sx < -16384 || sy > 16384 || sy < -16384) {
        sx /= 2;
        sy /= 2;
        sh++;
    }
    len = (long)lf_sqrt((unsigned long)(sx * sx + sy * sy));
    if ((len << sh) < 3 * r || !len)
        return;
    px = -sy * r / len;                 /* across it, a radius long */
    py = sx * r / len;
    for (j = 0; j < pairs; j++) {
        bx = e->tx + ax * 3 / 10 - j * sx * 16 / len;   /* (the second a pixel back) */
        by = e->ty + ay * 3 / 10 - j * sy * 16 / len;
        for (k = -1; k <= 1; k += 2)
            line_of(bx + k * px, by + k * py, e->tx + ax / 25 - j * sx * 16 / len + k * px * 23 / 10,
                    e->ty + ay / 25 - j * sy * 16 / len + k * py * 23 / 10, &d->line[d->nfins++]);
    }
}

static void place_one(const Missile __far *m, int i)
{
    MissileDraw __far *d = &g_d[g_nd];
    LzBolt b;
    LzEnds eb, en, ef;
    LzFat f;
    int cl, k, np = 0;
    part(m, 0, MISSILE_HALF16, &b);
    if (!lz_project(&b, &g_cam, &eb))
        return;
    d->z = (eb.zh + eb.zt) / 2;
    d->flame = 0;
    if (eb.r16 < g_cam.line16) {        /* far: a line, the flame a pixel on its tail */
        fat_of(&eb, 0, &f);
        if ((long)f.bx - f.ax >= 16384 || (long)f.ax - f.bx >= 16384)
            return;
        lz_line_of(f.ax, f.ay, f.bx, f.by, &d->line[0]);
        d->kind = M_LINE;
        d->tail_x = (short)(eb.tx >> 4);
        d->tail_y = (short)(eb.ty >> 4);
        d->flame = m->burn;
        g_nlines++;
        g_nd++;
        return;
    }
    d->kind = M_FAT;
    d->head_far = (unsigned char)(eb.zh > eb.zt);   /* flying away: the tip drawn first */
    for (k = 0; k < PARTS; k++) {
        d->npass[k] = d->core_line[k] = 0;
        part(m, g_part[k][0], g_part[k][1], &b);
        if (lz_project(&b, &g_cam, &en)) {
            fat_of(&en, eb.r16, &f);    /* (the whole's radius: one cylinder) */
            d->npass[k] = (unsigned char)rv_fat_setup(&f, &g_style[k], d->pass + np, &d->core[k], (int __far *)&cl);
            d->core_line[k] = (unsigned char)cl;
        }
        np += 2;
    }
    fins(d, &eb);
    if (m->burn) {
        part(m, -(MISSILE_HALF16 + MISSILE_FLAME16 / 2), -(MISSILE_FLAME16 / 2), &b);
        if (lz_project(&b, &g_cam, &ef)) {
            long tx = ef.hx - ef.tx, ty = ef.hy - ef.ty;
            while (tx > 8192 || tx < -8192 || ty > 8192 || ty < -8192) {   /* (its way kept: past 512 px it is all
                                                                               the longest anyway) */
                tx /= 2;
                ty /= 2;
            }
            flames_add_at(ef.tx, ef.ty, tx, ty, (int)eb.r16, 1, i * 613U + 71U, &d->fl);
            d->fl.front = (unsigned char)(ef.zh < ef.zt);   /* its tip nearer than its nozzle */
            d->flame = d->fl.n != 0;
        }
    }
    g_nfats++;
    g_nd++;
}

void missiles_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1)
{
    long f[3], r[3], u[3];
    int i, k;
    unsigned long t0 = clock240();
    g_nd = g_nlines = g_nfats = 0;
    for (i = 0; i < MISSILE_MAX && !g_m[i].live; i++)
        ;
    if (i == MISSILE_MAX)
        return;
    f[0] = cam->front.x; f[1] = cam->front.y; f[2] = cam->front.z;
    r[0] = cam->right.x; r[1] = cam->right.y; r[2] = cam->right.z;
    u[0] = cam->up.x; u[1] = cam->up.y; u[2] = cam->up.z;
    lz_camera(&g_cam, px, py, pz, f, r, u, cx, cy, focal, MISSILE_RADIUS16, MISSILE_ZMIN_KM, MISSILE_ZMAX_KM,
              LZ_RMAX16, BOLT_LINE16, y0, y1);
    for (i = 0; i < MISSILE_MAX; i++)
        if (g_m[i].live)
            place_one(&g_m[i], i);
    for (i = 1; i < g_nd; i++)          /* the farthest first */
        for (k = i; k > 0 && g_d[k].z > g_d[k - 1].z; k--) {
            MissileDraw t = g_d[k];
            g_d[k] = g_d[k - 1];
            g_d[k - 1] = t;
        }
    g_frames_on++;
    g_t_place += clock240() - t0;
}

/* a part's passes and its middle line when that is too thin */
static void part_draw(const MissileDraw __far *d, int k, unsigned char __far *band, int run0, int y0, int y1,
                      const short __far *ext, int ext_y0)
{
    int j;
    for (j = 0; j < d->npass[k]; j++)
        rv_pass_draw(&d->pass[2 * k + j], band, run0, y0, y1, ext, ext_y0, lz_sqrt16);
    if (d->core_line[k])
        rv_lines_draw(&d->core[k], 1, g_style[k].core, band, run0, y0, y1, ext, ext_y0);
}

void missiles_draw(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int run0, run1, i, k, by0 = y0, by1 = y1, bey0 = ext_y0;
    const short __far *bext = ext;
    unsigned char __far *band;
    unsigned long t0;
    if (!g_nd || !vid_run(&run0, &run1) || !bolts_rows(&by0, &by1, &bext, &bey0, ext_rows))
        return;
    t0 = clock240();
    band = vid_row(run0);
    for (i = 0; i < g_nd; i++) {
        const MissileDraw __far *d = &g_d[i];
        if (d->kind == M_LINE) {
            rv_lines_draw(&d->line[0], 1, g_style[1].line, band, run0, by0, by1, bext, bey0);
            if (d->flame && d->tail_y >= by0 && d->tail_y < by1 && d->tail_x >= bext[2 * (d->tail_y - bey0)]
                && d->tail_x < bext[2 * (d->tail_y - bey0) + 1])
                band[(unsigned)(d->tail_y - run0) * SCR_W + d->tail_x] = TAIL_PIXEL;
            continue;
        }
        if (d->flame && !d->fl.front)
            flames_draw(&d->fl, y0, y1, ext, ext_y0, ext_rows);
        if (d->nfins)
            rv_lines_draw(d->line, d->nfins, FIN_COLOUR, band, run0, by0, by1, bext, bey0);
        for (k = 0; k < PARTS; k++)     /* the farthest part first */
            part_draw(d, d->head_far ? PARTS - 1 - k : k, band, run0, by0, by1, bext, bey0);
        if (d->flame && d->fl.front)
            flames_draw(&d->fl, y0, y1, ext, ext_y0, ext_rows);
    }
    g_t_draw += clock240() - t0;
}
