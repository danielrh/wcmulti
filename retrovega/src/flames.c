/* flames.c: the ships' engine flames (flames.h). */
#include <string.h>
#include <malloc.h>
#include <dos.h>
#include "rv.h"
#include "bolts.h"
#include "music.h"
#include "ships.h"
#include "flames.h"

#define FLAME_MAX 32                    /* flames a frame: the nearest ships' first */
#define FLAME_MIN_HALF16 64             /* a ship whose half box is under 4 px on screen has no flames drawn */
#define FLAME_LEN 5                     /* a flame's length, in its root's radii (the glow's: the user's WC1 flames are
                                           ~1/12 of the ship's length across) ... */
#define FLAME_LEN_MIN 100               /* ... and at least this (1/1024ths of the half box) */

/* the baked flames (flames_bake, once when the game starts: the user's wish): every look x foreshortening x
 * direction x size x frame, shaded by rv_flame_draw and kept in XMS (the resident level) as run-length sprites
 * (the ships' format), drawn by ships_blit_mix */
#define BK_RHO 6                        /* foreshortenings: the tail's length on screen over the root's radius */
#define BK_SIZE 6                       /* the root's radius */
#define BK_FRAMES 2                     /* the noise's frames: each row of a flame from one or the other, at random each
                                           frame (the user's idea: two frames give a new flame every frame) */
#define BK_BYTES (1600UL * 1024)        /* XMS for them (1,375 KB used) */
#define BK_STRIP 24                     /* rows shaded at a time, in the scratch */
static const unsigned char __far g_rho[2][BK_RHO] = { { 0, 1, 2, 3, 4, 5 }, { 0, 2, 4, 6, 8, 10 } };
static const short __far g_size16[BK_SIZE] = { 16, 24, 36, 54, 80, 112 };
/* the tail's way, 0 .. 90 degrees down-right (the rest mirrored, flipped), in steps fine enough that the tail's end
 * is within half a pixel of the way asked for (the user: the flames must line up exactly with their ship, 2026-10-21):
 * a flame L pixels long, k steps to the quarter turn, misses by at most L pi / 4k; k at least 8 */
static unsigned char __far g_nang[2][BK_RHO][BK_SIZE];     /* its steps k (k + 1 ways baked; 0: end on, one) */
static unsigned short __far g_first[2][BK_RHO][BK_SIZE];   /* its first baked flame's number (over BK_FRAMES) */
static unsigned g_count;                /* baked flames in all (each BK_FRAMES frames) */

typedef struct {                        /* a baked flame (12 bytes, in XMS) */
    short w, h, ox, oy;                 /* its size, and its nozzle in it */
    unsigned long at;                   /* its row table in the sprites' block */
} BakedFlame;

/* the profile (tools/flamelut.py): intensity by s (sixteenths along, rows) and r (sixteenths of the radius) */
static const unsigned char __far g_flame_lut[256] = {
    255, 255, 255, 241, 220, 194, 164, 132,  98,  65,  36,   0,   0,   0,   0,   0,
    255, 255, 251, 239, 223, 204, 182, 157, 130, 102,  74,  48,  28,   0,   0,   0,
    252, 249, 243, 233, 221, 206, 188, 168, 146, 122,  98,  74,  51,  32,   0,   0,
    241, 238, 233, 225, 215, 202, 187, 170, 152, 132, 111,  89,  68,  48,  31,   0,
    230, 228, 223, 216, 207, 196, 183, 168, 152, 134, 115,  95,  76,  57,  40,  27,
    219, 217, 213, 206, 198, 188, 176, 162, 147, 131, 113,  95,  77,  59,  43,  29,
    208, 206, 202, 196, 188, 178, 167, 154, 139, 124, 107,  90,  73,  56,  40,  28,
    197, 195, 191, 185, 177, 167, 156, 143, 129, 113,  97,  80,  64,  48,  34,   0,
    185, 183, 179, 173, 165, 155, 143, 130, 115, 100,  83,  67,  51,  36,  26,   0,
    174, 172, 167, 161, 152, 141, 128, 114,  98,  82,  65,  49,  34,   0,   0,   0,
    163, 160, 155, 147, 137, 124, 110,  94,  77,  59,  43,  29,   0,   0,   0,   0,
    152, 148, 142, 132, 119, 104,  86,  68,  49,  33,   0,   0,   0,   0,   0,   0,
    140, 136, 126, 113,  95,  75,  54,  34,   0,   0,   0,   0,   0,   0,   0,   0,
    129, 121, 105,  83,  58,  33,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    116,  97,  64,  28,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     89,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/* the stipple's nine levels, dark to bright, as pairs of colours (sRGB) shown on alternate pixels: the palette has
 * no saturated violet-blue, so a level mixes two it has (chosen by tools/flame search against the user's WC1
 * flames, 2026-10-21; the nearest entries are taken when the flight starts) */
static const unsigned char __far g_levels[2][9][6] = {
    {   {  12,   4,  85,   12,   4,  85 }, {  12,   4,  85,  113,  32, 170 }, {  52,  72, 170,   36,  85, 190 },
        {  52,  72, 170,   44, 101, 234 }, {  76, 105, 250,   76, 105, 250 }, {  56, 133, 255,  165, 109, 165 },
        { 129, 178, 255,  214, 149, 214 }, { 218, 218, 222,  129, 178, 255 }, { 230, 230, 230,  230, 230, 230 } },
    {   {  16,   4,  28,   16,   4,  28 }, {  80,   0,  80,   80,   0,  80 }, { 113,  32, 170,  113,  32, 170 },
        { 113,  32, 170,  113,  32, 170 }, {  76, 105, 250,  165, 109, 165 }, { 129, 178, 255,  214, 149, 214 },
        { 129, 178, 255,  255, 178, 255 }, { 230, 230, 230,  230, 230, 230 }, { 230, 230, 230,  230, 230, 230 } },
};

static LzFlameLook __far *g_look;       /* [2]: normal, afterburner; paragraph aligned (the asm reads it at offset 0) */
static void __far *g_look_mem;
static XmsBlock g_bk_rows, g_bk_hdr;   /* the sprites, their headers */
static unsigned long g_bk_used;
static struct {                         /* this frame's flames */
    ShipView v;                         /* frame 0's rows (and the size and nozzle both frames share) */
    unsigned long at_b;                 /* frame 1's */
    short x, y;
    unsigned seed;                      /* which rows from which */
    unsigned char flags;
    short way, len16;                   /* its baked tail's way on screen (16384ths), its length x16 (the test's) */
} __far g_fl[FLAME_MAX];
static int g_nf, k_ok;
static unsigned g_frame; static unsigned short g_rand = 1;   /* (16 bits: it wraps as on DOS) */

/* the flight palette's entry nearest an sRGB colour (6-bit; below the cockpits' own colours, so the same in
 * every cockpit) */
static unsigned char nearest(const unsigned char __far *pal, int r, int g, int b)
{
    long best = 0x7FFFFFFFL, d;
    int i, k = 0;
    for (i = 1; i < RV_CP_FIRST; i++) {
        int dr = pal[3 * i] * 4 - r, dg = pal[3 * i + 1] * 4 - g, db = pal[3 * i + 2] * 4 - b;
        d = (long)dr * dr * 2 + (long)dg * dg * 4 + (long)db * db * 3;
        if (d < best) {
            best = d;
            k = i;
        }
    }
    return (unsigned char)k;
}

/* the looks: the profile, each look's stipple from the palette, the noise */
static int looks(const unsigned char __far *pal)
{
    unsigned long lin;
    unsigned short seed = 12345;        /* (16 bits: the generator wraps as on DOS) */
    int k, t;
    if (!g_look_mem)
        g_look_mem = _fmalloc(2 * sizeof(LzFlameLook) + 16);
    if (!g_look_mem)
        return 0;
#ifdef RV_HOST                          /* (a flat address: 16-aligned, as the paragraph is) */
    (void)lin;
    g_look = (LzFlameLook __far *)(((uintptr_t)g_look_mem + 15) & ~(uintptr_t)15);
#else
    lin = ((unsigned long)FP_SEG(g_look_mem) << 4) + FP_OFF(g_look_mem) + 15;
    g_look = (LzFlameLook __far *)MK_FP((unsigned)(lin >> 4), 0);
#endif
    for (k = 0; k < 2; k++) {
        unsigned char a[9], b[9];
        LzFlameLook __far *lk = &g_look[k];
        for (t = 0; t < 9; t++) {
            const unsigned char __far *c = g_levels[k][t];
            a[t] = nearest(pal, c[0], c[1], c[2]);
            b[t] = nearest(pal, c[3], c[4], c[5]);
        }
        _fmemcpy(lk->lut, g_flame_lut, 256);
        for (t = 0; t < 256; t++) {
            lk->ramp_a[t] = a[t * 9 >> 8];
            lk->ramp_b[t] = b[t * 9 >> 8];
            /* the noise: the edge's jitter -3 .. 3 sixteenths, the intensity's -24 .. 40 */
            seed = seed * 25173U + 13849U;
            lk->nr[t] = (signed char)((seed >> 8) % 7 - 3);
            seed = seed * 25173U + 13849U;
            lk->nv[t] = (signed char)((seed >> 8) % 65 - 24);
        }
    }
    return 1;
}

/* the ways' steps for every look, foreshortening and size, and where each one's flames start */
static void layout(void)
{
    int look, rho, size;
    g_count = 0;
    for (look = 0; look < 2; look++)
        for (rho = 0; rho < BK_RHO; rho++)
            for (size = 0; size < BK_SIZE; size++) {
                /* k >= L pi / (4 x 1/2): L = rho x size16 / 16 pixels, pi / 32 = 201 / 2048 */
                unsigned k = (unsigned)(((unsigned long)g_rho[look][rho] * g_size16[size] * 201 + 2047) / 2048);
                if (!g_rho[look][rho])
                    k = 0;
                else if (k < 8)
                    k = 8;
                g_nang[look][rho][size] = (unsigned char)k;
                g_first[look][rho][size] = (unsigned short)g_count;
                g_count += k + 1;
            }
}

static unsigned bake_id(int look, int rho, int ang, int size, int frame)
{
    return (g_first[look][rho][size] + ang) * BK_FRAMES + frame;
}

/* way number ang of k, down-right from the right: 16384ths of a turn (finer than lf_atan2's 1024ths, which alone
 * would put the longest flames' tails half a pixel off) */
static int way_of(int ang, int k)
{
    return k ? (int)(((long)ang * 4096 + k / 2) / k) : 0;
}

/* a way (16384ths of a turn) as a vector, 16384 = 1.0: the sine table's steps interpolated */
static void way_vec(int w, long *c, long *s)
{
    int a = w >> 4, f = w & 15;
    long c0 = lf_cos(a), s0 = lf_sin(a);
    *c = c0 + ((lf_cos(a + 1) - c0) * f >> 4);
    *s = s0 + ((lf_sin(a + 1) - s0) * f >> 4);
}

/* atan(i / 256), i = 0 .. 256, in 16384ths of a turn (atan 1 = 2048) */
static const short __far g_atan[257] = {
       0,   10,   20,   31,   41,   51,   61,   71,   81,   92,  102,  112,  122,  132,  142,  153,
     163,  173,  183,  193,  203,  213,  224,  234,  244,  254,  264,  274,  284,  294,  304,  314,
     324,  334,  344,  354,  364,  374,  384,  394,  404,  414,  424,  434,  444,  454,  464,  473,
     483,  493,  503,  513,  523,  532,  542,  552,  562,  571,  581,  591,  600,  610,  620,  629,
     639,  648,  658,  667,  677,  687,  696,  705,  715,  724,  734,  743,  753,  762,  771,  781,
     790,  799,  808,  818,  827,  836,  845,  854,  863,  872,  882,  891,  900,  909,  918,  927,
     936,  944,  953,  962,  971,  980,  989,  997, 1006, 1015, 1024, 1032, 1041, 1050, 1058, 1067,
    1075, 1084, 1092, 1101, 1109, 1118, 1126, 1135, 1143, 1151, 1160, 1168, 1176, 1184, 1193, 1201,
    1209, 1217, 1225, 1233, 1241, 1249, 1257, 1265, 1273, 1281, 1289, 1297, 1305, 1313, 1321, 1328,
    1336, 1344, 1352, 1359, 1367, 1374, 1382, 1390, 1397, 1405, 1412, 1420, 1427, 1435, 1442, 1449,
    1457, 1464, 1471, 1478, 1486, 1493, 1500, 1507, 1514, 1521, 1529, 1536, 1543, 1550, 1557, 1564,
    1571, 1577, 1584, 1591, 1598, 1605, 1612, 1618, 1625, 1632, 1638, 1645, 1652, 1658, 1665, 1671,
    1678, 1684, 1691, 1697, 1704, 1710, 1717, 1723, 1729, 1736, 1742, 1748, 1754, 1761, 1767, 1773,
    1779, 1785, 1791, 1798, 1804, 1810, 1816, 1822, 1828, 1833, 1839, 1845, 1851, 1857, 1863, 1869,
    1874, 1880, 1886, 1892, 1897, 1903, 1909, 1914, 1920, 1925, 1931, 1937, 1942, 1948, 1953, 1958,
    1964, 1969, 1975, 1980, 1985, 1991, 1996, 2001, 2007, 2012, 2017, 2022, 2027, 2033, 2038, 2043,
    2048
};

/* the way of (x, y), down-right (x, y >= 0), 16384ths of a turn: the table between its steps (finer than lf_atan2's
 * 1024ths, which it floors to: half a pixel on the longest flames), one division */
static int way16(long x, long y)
{
    long t;
    int i, f, a;
    if (x <= 0)
        return y > 0 ? 4096 : 0;
    while (x > 0x1FFFF || y > 0x1FFFF) {   /* (the division's x 16384 must fit) */
        x >>= 1;
        y >>= 1;
    }
    t = ((y < x ? y : x) * 16384 + (y < x ? x : y) / 2) / (y < x ? x : y);   /* 0 .. 16384: tan from the nearer axis */
    i = (int)(t >> 6);
    f = (int)(t & 63);
    a = g_atan[i] + (i < 256 ? (int)(((long)(g_atan[i + 1] - g_atan[i]) * f + 32) >> 6) : 0);
    return y < x ? a : 4096 - a;
}

/* one flame shaded and kept: its tail at way w (16384ths of a turn, down-right), rho radii long, the root's radius
 * size16; shaded BK_STRIP rows at a time into the scratch, each row run-length coded (skip, count, pixels .. 0, 0) */
static int bake_one(int look, int rho, int w, int size16, int seed, BakedFlame *out)
{
    unsigned char __far *band = gfx_scratch(), __far *rows = band + BK_STRIP * SCR_W;
    unsigned long __far *tab = (unsigned long __far *)(rows + 12288);
    unsigned room = 32768U - BK_STRIP * SCR_W - 12288 - 2048, len = 0;
    long c, sn, tx, ty;
    int cx = 16, cy = 16, top, bot, left, right, y, r;
    LzFlame fl;
    LzFat fp;
    LzPass pp;
    way_vec(w, &c, &sn);
    tx = (long)rho * size16 * c >> 14;
    ty = (long)rho * size16 * sn >> 14;
    rv_flame_setup(cx * 16 + 8, cy * 16 + 8, (int)tx, (int)ty, size16, seed, &fl, &fp);
    rv_capsule_setup(&fp, 0, &pp);
    top = pp.top;
    bot = pp.bot;
    left = ((fp.ax < fp.bx ? fp.ax : fp.bx) - fp.r) / 16 - 1;
    right = ((fp.ax < fp.bx ? fp.bx : fp.ax) + fp.r) / 16 + 2;
    if (left < 0) left = 0;
    if (right > SCR_W) right = SCR_W;
    if (top < 0) top = 0;
    if (bot - top > 120)
        bot = top + 120;
    out->w = (short)(right - left);
    out->h = (short)(bot - top);
    out->ox = (short)(cx - left);
    out->oy = (short)(cy - top);
    for (y = top; y < bot; y += BK_STRIP) {
        int y1 = y + BK_STRIP < bot ? y + BK_STRIP : bot, y0 = y, ey0 = 0;
        const short __far *ext = 0;
        for (r = y; r < y1; r++)        /* (the flame's columns: the rest of the scratch is not read) */
            _fmemset(band + (unsigned)(r - y) * SCR_W + left, 0, right - left);
        if (bolts_rows(&y0, &y1, &ext, &ey0, 0))
            rv_flame_draw(&pp, band, y, y0, y1, ext, ey0, lz_sqrt16, &fl, &g_look[look]);
        for (r = y; r < y1; r++) {
            const unsigned char __far *q = band + (unsigned)(r - y) * SCR_W + left, __far *end = q + (right - left),
                __far *last = q, __far *st;
            tab[r - top] = len;
            for (;;) {
                unsigned n;
                while (q < end && !*q)
                    q++;
                if (q >= end)
                    break;
                for (st = q; q < end && *q && q - st < 255; q++)
                    ;
                n = (unsigned)(q - st);
                if (len + 4 + n > room)
                    return 0;
                rows[len++] = (unsigned char)(st - last);
                rows[len++] = (unsigned char)n;
                _fmemcpy(rows + len, st, n);
                len += n;
                last = q;
            }
            rows[len++] = 0;
            rows[len++] = 0;
        }
    }
    tab[bot - top] = len;
    if (len & 1)
        rows[len++] = 0;
    if (g_bk_used + 4UL * (bot - top + 1) + len > BK_BYTES)
        return 0;
    out->at = g_bk_used;
    xms_put(g_bk_rows, g_bk_used, tab, 4UL * (bot - top + 1));
    xms_put(g_bk_rows, g_bk_used + 4UL * (bot - top + 1), rows, len);
    g_bk_used += 4UL * (bot - top + 1) + len;
    return 1;
}

void flames_reserve(void)
{
    layout();
    g_bk_used = 0;
    g_bk_rows = xms_alloc(BK_BYTES, XMS_RESIDENT);
    g_bk_hdr = g_bk_rows ? xms_alloc((unsigned long)g_count * BK_FRAMES * sizeof(BakedFlame), XMS_RESIDENT) : 0;
    if (!g_bk_hdr)
        g_bk_rows = 0;
}

void flames_init(void)
{
    bolts_tables();                     /* (the roots' table the bake made is the same: the draw needs none) */
}

void flames_bake(void)
{
    unsigned long t0 = clock240();
    int look, rho, ang, size, frame, n = 0, lost = 0;
    const RvCockpit __far *cp = dat_cockpit(0);
    unsigned char __far *pal = (unsigned char __far *)_fmalloc(768);
    bolts_tables();
    if (!g_bk_hdr || !cp || !pal) {
        rv_log("flames: no %s for the baked flames", !g_bk_hdr ? "XMS" : !cp ? "cockpit palette" : "memory");
        g_bk_hdr = g_bk_rows = 0;
        if (pal) _ffree(pal);
        return;
    }
    dat_read(dat_asset(cp->palette)->at, pal, 768);   /* (the flight palette: any cockpit's below RV_CP_FIRST) */
    k_ok = looks(pal);
    _ffree(pal);
    if (!k_ok) {
        rv_log("flames: no memory for the looks");
        g_bk_hdr = g_bk_rows = 0;
        return;
    }
    for (look = 0; look < 2; look++)
        for (rho = 0; rho < BK_RHO; rho++)
            for (size = 0; size < BK_SIZE; size++)
                for (ang = 0; ang <= g_nang[look][rho][size]; ang++)   /* (end on, k = 0: the way does not show) */
                    for (frame = 0; frame < BK_FRAMES; frame++) {
                        BakedFlame b;
                        if (!bake_one(look, g_rho[look][rho], way_of(ang, g_nang[look][rho][size]), g_size16[size],
                                      (frame * 89 + ang * 13 + size * 7 + rho * 31) & 255, &b)) {
                            b.w = b.h = 0;
                            b.at = 0;
                            lost++;
                        }
                        xms_put(g_bk_hdr, (unsigned long)bake_id(look, rho, ang, size, frame) * sizeof b, &b, sizeof b);
                        n++;
                    }
    rv_log("flames: %d baked in %lu ms, %lu KB (%d did not fit)", n, (clock240() - t0) * 1000 / 240, g_bk_used / 1024,
           lost);
    _ffree(g_look_mem);                 /* (the looks were for the bake) */
    g_look_mem = 0;
    g_look = 0;
}

void flames_frame(void)
{
    g_nf = 0;
    g_frame++;
}

/* one flame into the frame's list: the baked one nearest it (its size, its foreshortening, its way by quadrant, a
 * frame of noise at random), its nozzle on screen x16 (x16, y16), its tail's way on screen (tx, ty: any scale to
 * 2^16, the finer the better) and length (x16), its root's radius x16, its look (1 the afterburner's), a number of
 * its own for its noise: 0 when none fits */
static int add_one(long x16, long y16, long tx, long ty, long len16, int r16, int look, unsigned short id)
{
    BakedFlame b[BK_FRAMES];
    long atx = tx < 0 ? -tx : tx, aty = ty < 0 ? -ty : ty, l;
    int rho, ang, size, best, i, k, a;
    if (g_nf >= FLAME_MAX)
        return 0;
    for (size = 0, best = 0x7FFF, i = 0; i < BK_SIZE; i++) {
        int d = g_size16[i] > r16 ? g_size16[i] * 4 - r16 * 5 : r16 * 4 - g_size16[i] * 5;   /* (ratios: 5/4) */
        if (d < best) { best = d; size = i; }
    }
    l = len16 * 2 / g_size16[size];     /* half radii */
    for (rho = 0, best = 0x7FFF, i = 0; i < BK_RHO; i++) {
        int d = (int)(l - g_rho[look][i] * 2);
        if (d < 0) d = -d;
        if (d < best) { best = d; rho = i; }
    }
    k = g_nang[look][rho][size];
    ang = k ? (int)(((long)way16(atx, aty) * k + 2048) / 4096) : 0;   /* (the nearest of the k + 1 ways) */
    xms_get(g_bk_hdr, (unsigned long)bake_id(look, rho, ang, size, 0) * sizeof b[0], b, sizeof b);   /* (both frames) */
    if (!b[0].w || !b[1].w)
        return 0;
    g_rand = g_rand * 25173U + 13849U;
    g_fl[g_nf].seed = g_rand + id;
    g_fl[g_nf].v.w = b[0].w;
    g_fl[g_nf].v.h = b[0].h;
    g_fl[g_nf].v.ox = b[0].ox;
    g_fl[g_nf].v.oy = b[0].oy;
    g_fl[g_nf].v.blk = g_bk_rows;
    g_fl[g_nf].v.at = b[0].at;
    g_fl[g_nf].at_b = b[1].at;
    g_fl[g_nf].x = (short)(x16 >> 4);
    g_fl[g_nf].y = (short)(y16 >> 4);
    g_fl[g_nf].flags = (unsigned char)((tx < 0 ? BLIT_H : 0) | (ty < 0 ? BLIT_V : 0));
    a = way_of(ang, k);
    g_fl[g_nf].way = (short)(!k ? -1 : tx < 0 ? (ty < 0 ? 8192 + a : 8192 - a) : ty < 0 ? (16384 - a) & 16383 : a);
    g_fl[g_nf].len16 = (short)(g_rho[look][rho] * g_size16[size]);
    g_nf++;
    return 1;
}

int flames_way(int k, int *way, int *len16)
{
    if (k < 0 || k >= g_nf || g_fl[k].way < 0)
        return 0;
    *way = g_fl[k].way;
    *len16 = g_fl[k].len16;
    return 1;
}

void flames_way_vec(int way, long *c, long *s)
{
    way_vec(way, c, s);
}

void flames_add(const RvShipType __far *t, const LfPlace *p, int nb, int nh, int scale256, int burn, int slot,
                FlameSet __far *out)
{
    LfViewRot rot;
    long half16;
    int k;
    out->first = (unsigned char)g_nf;
    out->n = 0;
    out->front = 0;
    if (!g_bk_hdr || !t || !t->nengines || !p->on_screen)
        return;
    half16 = (long)t->lod0_px * scale256 / 32;   /* the half box on screen, x16 */
    if (half16 < FLAME_MIN_HALF16)
        return;
    lf_view_rot(p->view, nb, nh, &rot);
    for (k = 0; k < t->nengines && k < RV_ENGINES && g_nf < FLAME_MAX; k++) {
        const signed char __far *e = t->engine[k];
        int32_t ox, oy, oz, tx, ty, tz;     /* (lf_sprite_point's: a long on DOS) */
        long len, len16;
        int r16 = (int)((unsigned char)e[3] * half16 / 1024);
        len = (long)(unsigned char)e[3] * FLAME_LEN;
        if (len < FLAME_LEN_MIN)
            len = FLAME_LEN_MIN;
        if (burn)
            len *= 2;
        if (r16 < BOLT_LINE16)
            r16 = BOLT_LINE16;
        /* the nozzle on the sprite as drawn, and the tail's way (the renderer's -z, a whole 1.0 of it: the way to a
         * fraction of a pixel at any length) and length on screen */
        lf_sprite_point(&rot, p->roll, e[0] * half16 / 127, e[1] * half16 / 127, e[2] * half16 / 127, &ox, &oy, &oz);
        lf_sprite_point(&rot, p->roll, 0, 0, -16384, &tx, &ty, &tz);
        len16 = (long)lf_sqrt((unsigned long)(tx * tx + ty * ty)) * (len * half16 / 1024) / 16384;
        out->front = (unsigned char)(tz > 0);
        add_one(p->sx * 16L + 8 + ox, p->sy * 16L + 8 + oy, tx, ty, len16, r16, burn != 0, slot * 977U + k * 131U);
    }
    out->n = (unsigned char)(g_nf - out->first);
}

void flames_add_at(long x16, long y16, long tx16, long ty16, int r16, int burn, unsigned short id, FlameSet __far *out)
{
    out->first = (unsigned char)g_nf;
    out->front = 0;
    out->n = (unsigned char)(g_bk_hdr && add_one(x16, y16, tx16, ty16,
                                                 (long)lf_sqrt((unsigned long)(tx16 * tx16 + ty16 * ty16)), r16, burn != 0, id));
}

void flames_draw(const FlameSet __far *f, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    int k;
    for (k = f->first; k < f->first + f->n; k++)
        ships_blit_mix(&g_fl[k].v, g_fl[k].at_b, g_fl[k].seed, g_fl[k].flags, g_fl[k].x, g_fl[k].y, y0, y1, 0, SCR_W, ext,
                       ext_y0, ext_rows);
}
