/* explode.c: the flight's explosions (explode.h).
 *
 * The art is EXPLODE.DAT (tools/explode.py): the colour ramps, the timeline and the heat cels.  The cels stay in
 * XMS; the ones being drawn are fetched into a small cache.  A frame's draw of one
 * explosion: its timeline entry at age x TL / life; a table from heat to colour for that entry's cooling (heat x gain
 * picks the ramp's colour); then, row by row of the screen, the cel's row (turned one of eight ways) made colours
 * through the table, the pixels under the dissolve's threshold (dithered by a 4 x 4 ordered pattern in the cel's own
 * pixels) left clear, and the row scaled into the screen by rv_scale_row (lowlevel.asm, the ships' own; its C twin
 * on the host).  The flash, the sparks and the debris are hud.c's runs and dots, clipped as the HUD is.  All the
 * module's data is far (DGROUP is nearly full). */
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "rv.h"
#include "rvhooks.h"
#include "lightfield.h"
#include "hud.h"
#include "explode.h"

void __far __cdecl rv_scale_row(const unsigned char __far *line, const unsigned short __far *map, int xa, int xb,
                                unsigned char __far *out);

#define CACHE 4                         /* cels kept at once */
#define MAP_W 1024                      /* the widest an explosion is drawn, px */
#define CEL_MAX 64
#define TL_MAX 64
#define ZMIN16 (16 * 60)                /* the near plane: 60 km, in sixteenths of a km */

typedef struct { uint8_t cel, thr; uint16_t gain, grow, spark, debris; } Tl;   /* EXPLODE.DAT's timeline records */

typedef struct {
    int32_t x, y, z;                    /* positions */
    int32_t vx, vy, vz;                 /* positions a step */
    int32_t r16;                        /* the full radius, sixteenths of a km */
    uint16_t seed;
    uint8_t kind, life, age, wait, variant, orient;
    /* placed */
    int32_t depth;                      /* along the view, positions */
    int16_t sx, sy, rpx;                /* the screen's centre, the full radius in px */
    uint8_t on;
} Boom;

static const struct { uint8_t life, sparks, debris, ball; } __far k_kind[BOOM_KINDS] = {
    { 40, 24, 6, 1 },                   /* a fighter */
    { 75, 40, 12, 1 },                  /* a capital ship's last */
    { 26, 12, 0, 1 },                   /* a missile's burst */
    { 9, 6, 0, 0 },                     /* a hit: the flash and sparks */
};
static const uint8_t __far k_bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

static XmsBlock __far g_blk;
static uint32_t __far g_cel_at;         /* the cels' offset in the block */
static uint16_t __far g_ncel, __far g_size, __far g_nvar, __far g_ntl;
static uint8_t __far g_nramp, __far g_nspark, __far g_ndebris, __far g_dither;
static uint8_t __far g_cols[64];        /* the ramp's colours, then the sparks', then the debris' */
static Tl __far g_tl[TL_MAX];
static uint8_t __far *__far g_cache_mem;   /* CACHE cels, the flight's conventional memory (the upper is the ships'
                                           dense slab's and the band buffer's) */
static int16_t __far g_cache_key[CACHE];
static uint16_t __far g_cache_used[CACHE], __far g_tick;
static uint16_t __far *__far g_map;     /* a column map, MAP_W */
static uint8_t __far g_line[CEL_MAX];
static uint8_t __far g_tab[256];
static Boom __far g_b[EXPLODE_MAX];
static int16_t __far g_n;
static uint8_t __far g_order[EXPLODE_MAX];
static int16_t __far g_nord, __far g_next;
static int16_t __far g_ready;

int16_t explode_reserve(void)
{
    static const char __far s_magic[] = "RVBOOM", __far s_none[] = "explode: no EXPLODE.DAT: no explosions",
        __far s_in[] = "explode: %u variants of %u cels of %u x %u heat, a timeline of %u, in XMS",
        __far s_file[] = "EXPLODE.DAT", __far s_rb[] = "rb";
    char name[16], mode[4];
    FILE *f;
    uint8_t *buf = rv_filebuf;
    uint16_t ncols, i;
    uint32_t bytes, at;
    g_blk = 0;
    _fstrcpy(name, s_file);
    _fstrcpy(mode, s_rb);
    if ((f = fopen(name, mode)) == 0) {
        rv_logf(s_none);
        return 0;
    }
    if (fread(buf, 1, 22, f) != 22 || _fmemcmp(buf, s_magic, 6) || (buf[8] | buf[9] << 8) != 1) {   /* (the header:
                                           22 bytes, tools/explode.py) */
        fclose(f);
        rv_logf(s_none);
        return 0;
    }
    g_ncel = buf[10] | buf[11] << 8;
    g_size = buf[12] | buf[13] << 8;
    g_nvar = buf[14] | buf[15] << 8;
    g_ntl = buf[16] | buf[17] << 8;
    g_nramp = buf[18];
    g_nspark = buf[19];
    g_ndebris = buf[20];
    g_dither = buf[21];
    ncols = (uint16_t)(g_nramp + g_nspark + g_ndebris);
    if (!g_ncel || g_size > CEL_MAX || !g_nvar || !g_ntl || g_ntl > TL_MAX || !g_nramp || !g_nspark || !g_ndebris
        || ncols > sizeof g_cols) {
        fclose(f);
        rv_logf(s_none);
        return 0;
    }
    fread(buf, 1, (ncols + 1) & ~1, f);
    _fmemcpy(g_cols, buf, ncols);
    for (i = 0; i < g_ntl; i++) {
        fread(buf, 1, 10, f);
        g_tl[i].cel = buf[0] < g_ncel ? buf[0] : (uint8_t)(g_ncel - 1);
        g_tl[i].thr = buf[1];
        g_tl[i].gain = buf[2] | buf[3] << 8;
        g_tl[i].grow = buf[4] | buf[5] << 8;
        g_tl[i].spark = buf[6] | buf[7] << 8;
        g_tl[i].debris = buf[8] | buf[9] << 8;
    }
    bytes = (uint32_t)g_nvar * g_ncel * g_size * g_size;
    g_cel_at = 0;
    if ((g_blk = xms_alloc(bytes, XMS_RESIDENT)) == 0) {
        fclose(f);
        return 0;
    }
    for (at = 0; at < bytes;) {
        uint16_t n = (uint16_t)(bytes - at < sizeof rv_filebuf ? bytes - at : sizeof rv_filebuf);
        if (fread(buf, 1, n, f) != n)
            break;
        xms_put(g_blk, at, buf, n);
        at += n;
    }
    fclose(f);
    rv_logf(s_in, g_nvar, g_ncel, g_size, g_size, g_ntl);
    return 1;
}

int16_t explode_flight_start(void)
{
    static const char __far s_room[] = "explode: room for %d, a cache of %d cels (%u bytes)";
    int16_t i;
    explode_flight_end();
    if (!g_blk)
        return 0;
    if (!g_cache_mem)
        g_cache_mem = (uint8_t __far *)_fmalloc((size_t)CACHE * g_size * g_size);
    if (!g_map)
        g_map = (uint16_t __far *)_fmalloc(MAP_W * sizeof(uint16_t));
    if (!g_cache_mem || !g_map) {
        explode_flight_end();
        return 0;
    }
    for (i = 0; i < CACHE; i++)
        g_cache_key[i] = -1;
    g_n = g_nord = 0;
    g_ready = 1;
    rv_logf(s_room, EXPLODE_MAX, CACHE, CACHE * g_size * g_size + MAP_W * 2);
    return 1;
}

void explode_flight_end(void)
{
    if (g_cache_mem)
        _ffree(g_cache_mem);
    g_cache_mem = 0;
    if (g_map)
        _ffree(g_map);
    g_map = 0;
    g_n = g_nord = 0;
    g_ready = 0;
}

void explode_add(const RvVec3 *at, const RvVec3 *vel, int32_t radius_km, int16_t kind, uint16_t delay, uint16_t seed)
{
    Boom __far *b;
    uint32_t st = seed * UINT32_C(2654435761) + 1;
    if (!g_ready || kind < 0 || kind >= BOOM_KINDS)
        return;
    if (g_n >= EXPLODE_MAX) {           /* full: the oldest gives its place */
        int16_t i, old = 0;
        for (i = 1; i < g_n; i++)
            if (g_b[i].age > g_b[old].age)
                old = i;
        b = &g_b[old];
    } else {
        b = &g_b[g_n++];
    }
    b->x = at->x; b->y = at->y; b->z = at->z;
    b->vx = vel->x; b->vy = vel->y; b->vz = vel->z;
    b->r16 = radius_km * 16;
    b->kind = (uint8_t)kind;
    b->life = k_kind[kind].life;
    b->age = 0;
    b->wait = (uint8_t)(delay > 255 ? 255 : delay);
    st = st * UINT32_C(1103515245) + 12345;
    b->variant = (uint8_t)((st >> 16) % g_nvar);
    b->orient = (uint8_t)((st >> 20) & 7);
    b->seed = (uint16_t)(st >> 8);
    b->on = 0;
}

int16_t explode_count(void) { return g_n; }

void explode_frame(void)
{
    int16_t i;
    for (i = g_n - 1; i >= 0; i--) {
        Boom __far *b = &g_b[i];
        if (b->wait) {
            b->wait--;
            continue;
        }
        b->x += b->vx; b->y += b->vy; b->z += b->vz;
        b->vx -= b->vx / 16; b->vy -= b->vy / 16; b->vz -= b->vz / 16;   /* (the drift slows) */
        if (++b->age >= b->life)
            g_b[i] = g_b[--g_n];
    }
}

void explode_place(const LfVec *eye, const LfBasis *cam, int16_t cx, int16_t cy, int16_t focal)
{
    int16_t i, k;
    g_nord = 0;
    for (i = 0; i < g_n; i++) {
        Boom __far *b = &g_b[i];
        int32_t rx = (b->x - eye->x) >> 4, ry = (b->y - eye->y) >> 4, rz = (b->z - eye->z) >> 4;   /* (16ths of a km) */
        int32_t X, Y, Z, r, sx, sy;
        b->on = 0;
        if (b->wait || rx > INT32_C(2000000) || rx < -INT32_C(2000000) || ry > INT32_C(2000000)
            || ry < -INT32_C(2000000) || rz > INT32_C(2000000) || rz < -INT32_C(2000000))
            continue;
        X = (rx * cam->right.x + ry * cam->right.y + rz * cam->right.z) / 256;
        Y = (rx * cam->up.x + ry * cam->up.y + rz * cam->up.z) / 256;
        Z = (rx * cam->front.x + ry * cam->front.y + rz * cam->front.z) / 256;
        if (Z < ZMIN16)
            continue;
        r = b->r16 * focal / Z;
        sx = cx + X * focal / Z;
        sy = cy - Y * focal / Z;
        if (r > MAP_W / 2 - 1)
            r = MAP_W / 2 - 1;
        if (sx + r * 2 < 0 || sx - r * 2 > SCR_W || sy + r * 2 < 0 || sy - r * 2 > SCR_H)
            continue;                   /* (twice the radius: its sparks fly past it) */
        b->sx = (int16_t)sx;
        b->sy = (int16_t)sy;
        b->rpx = (int16_t)(r < 1 ? 1 : r);
        b->depth = Z * 16;
        b->on = 1;
        for (k = g_nord; k > 0 && g_b[g_order[k - 1]].depth < b->depth; k--)   /* far to near */
            g_order[k] = g_order[k - 1];
        g_order[k] = (uint8_t)i;
        g_nord++;
    }
}

/* a cel in the cache (fetched from XMS the first time) */
static const uint8_t __far *cel_get(int16_t variant, int16_t cel)
{
    int16_t key = (int16_t)(variant * g_ncel + cel), i, lru = 0;
    uint16_t n = g_size * g_size;
    g_tick++;
    for (i = 0; i < CACHE; i++)
        if (g_cache_key[i] == key) {
            g_cache_used[i] = g_tick;
            return g_cache_mem + (uint16_t)i * n;
        }
    for (i = 1; i < CACHE; i++)
        if (g_cache_key[i] < 0 || (g_cache_key[lru] >= 0 && g_cache_used[i] < g_cache_used[lru]))
            lru = i;
    xms_get(g_blk, g_cel_at + (uint32_t)key * n, g_cache_mem + (uint16_t)lru * n, n);
    g_cache_key[lru] = key;
    g_cache_used[lru] = g_tick;
    return g_cache_mem + (uint16_t)lru * n;
}

static int16_t row_clip(int16_t y, int16_t *ca, int16_t *cb, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows)
{
    if (ext) {
        const int16_t __far *e = ext + 2 * (y - ext_y0);
        if (y < ext_y0 || y >= ext_y0 + ext_rows)
            return 0;
        if (e[0] > *ca) *ca = e[0];
        if (e[1] < *cb) *cb = e[1];
    }
    return *ca < *cb;
}

/* row v of the cel as this explosion turns it, made colours: clear below the dissolve's dithered threshold */
static void make_line(const uint8_t __far *cel, int16_t v, int16_t orient, int16_t thr)
{
    int16_t n = (int16_t)g_size, u, step;
    const uint8_t __far *p;
    const uint8_t __far *bay = k_bayer + (v & 3) * 4;
    if (!(orient & 4)) {                /* rows as rows */
        p = cel + (uint16_t)((orient & 2) ? n - 1 - v : v) * n;
        step = 1;
        if (orient & 1) {
            p += n - 1;
            step = -1;
        }
    } else {                            /* the transpose: a column read as the row */
        p = cel + ((orient & 1) ? n - 1 - v : v);
        step = n;
        if (orient & 2) {
            p += (uint16_t)(n - 1) * n;
            step = -n;
        }
    }
    for (u = 0; u < n; u++, p += step) {
        uint8_t h = *p;
        g_line[u] = (uint8_t)(!h || (int16_t)h + bay[u & 3] * g_dither < thr ? 0 : g_tab[h]);
    }
}

static void disc(int16_t cx, int16_t cy, int16_t r, int16_t colour)
{
    int16_t dy, dx;
    for (dy = -r; dy <= r; dy++) {
        for (dx = r; dx > 0 && (int32_t)dx * dx + (int32_t)dy * dy > (int32_t)r * r; dx--)
            ;
        hud_run(cx - dx, cx + dx + 1, cy + dy, colour);
    }
}

static uint16_t rnd(uint32_t *st)
{
    *st = *st * UINT32_C(1103515245) + 12345;
    return (uint16_t)(*st >> 16) & 0x7FFF;
}

/* the flash, the sparks and the debris, over the fireball (the screen's own pixels, the HUD's clipping) */
static void overlays(const Boom __far *b, const Tl __far *t)
{
    const uint8_t __far *spark = g_cols + g_nramp, __far *debris = spark + g_nspark;
    int16_t R = b->rpx, age = b->age, life = b->life, i, n;
    uint32_t st = b->seed;
    if (age < 3) {                      /* the flash: a white core and a four-pointed glint */
        static const uint8_t __far k_core[3] = { 12, 16, 19 }, __far k_arm[3] = { 130, 90, 50 };
        int16_t rr = (int16_t)((int32_t)R * k_core[age] / 100), arm = (int16_t)((int32_t)R * k_arm[age] / 100);
        int16_t c = spark[0], c2 = spark[age < 2 ? 1 : 2];
        if (rr < 1) rr = 1;
        hud_run(b->sx - arm, b->sx + arm + 1, b->sy, c2);
        hud_vline(b->sx, b->sy - arm, b->sy + arm + 1, c2);
        hud_run(b->sx - arm / 2, b->sx + arm / 2 + 1, b->sy, c);
        hud_vline(b->sx, b->sy - arm / 2, b->sy + arm / 2 + 1, c);
        disc(b->sx, b->sy, rr, age < 2 ? c : c2);
    }
    for (i = 0; i < k_kind[b->kind].sparks; i++) {   /* sparks: flying out, cooling white to red */
        uint16_t a = rnd(&st), sp = rnd(&st), l = rnd(&st);
        int16_t ang = (int16_t)(a >> 5), lf = (int16_t)((int32_t)life * (90 + (int32_t)l * 115 / 32768) / 256), k;
        int32_t dist, dx, dy;
        if (age >= lf || !lf)
            continue;
        dist = (int32_t)(90 + (int32_t)sp * 230 / 32768) * t->spark / 256 * R / 256;
        dx = lf_cos(ang) * dist / 16384;
        dy = lf_sin(ang) * dist / 16384 * 205 / 256;
        k = (int16_t)((int32_t)age * g_nspark / lf);
        hud_dot((int16_t)(b->sx + dx * 225 / 256), (int16_t)(b->sy + dy * 225 / 256),
                spark[k + 2 < g_nspark ? k + 2 : g_nspark - 1]);
        hud_dot((int16_t)(b->sx + dx), (int16_t)(b->sy + dy), spark[k < g_nspark ? k : g_nspark - 1]);
    }
    if (age < 3)
        return;
    for (i = 0; i < k_kind[b->kind].debris; i++) {   /* debris: dark chunks tumbling out */
        uint16_t a = rnd(&st), sp = rnd(&st);
        int16_t ang = (int16_t)(a >> 5), x, y, w, yy, c;
        int32_t dist = (int32_t)(102 + (int32_t)sp * 205 / 32768) * t->debris / 256 * R / 256;
        if (age * 20 > life * 17 && ((age + i) & 1))
            continue;                   /* (a dithered fade at the end) */
        x = (int16_t)(b->sx + lf_cos(ang) * dist / 16384);
        y = (int16_t)(b->sy + lf_sin(ang) * dist / 16384 * 205 / 256);
        n = (age + i) % 3 ? 2 : 3;      /* the tumble: its size flickers */
        w = (int16_t)(n - ((age + i) & 1));
        c = (age + i * 5) % 9 == 0 ? debris[g_ndebris - 1] : debris[(i + age / 4) % (g_ndebris - 1)];
        for (yy = y; yy < y + n; yy++)
            hud_run(x, x + w, yy, c);
    }
}

static void draw_one(const Boom __far *b, int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0,
                     int16_t ext_rows)
{
    const Tl __far *t = &g_tl[(uint16_t)b->age * g_ntl / b->life];
    int16_t R, size, top, left, ya, yb, y, last_v = -1, h;
    const uint8_t __far *cel;
    if (k_kind[b->kind].ball && (R = (int16_t)((int32_t)b->rpx * t->grow / 256)) >= 1) {
        size = (int16_t)(2 * R);
        top = b->sy - R;
        left = b->sx - R;
        ya = y0 > top ? y0 : top;
        yb = y1 < top + size ? y1 : top + size;
        if (ya < 0) ya = 0;
        if (yb > SCR_H) yb = SCR_H;
        if (ya < yb && left < SCR_W && left + size > 0) {
            cel = cel_get(b->variant, t->cel);
            for (h = 0; h < 256; h++) {  /* heat to colour, as cool as this entry is */
                int32_t idx = (int32_t)h * t->gain / 256 * g_nramp / 256;
                g_tab[h] = g_cols[idx < g_nramp ? (int16_t)idx : g_nramp - 1];
            }
            lf_colmap((int16_t)g_size, size, g_map);
            for (y = ya; y < yb; y++) {
                int16_t ca = left > 0 ? left : 0, cb = left + size < SCR_W ? left + size : SCR_W, v;
                if (!row_clip(y, &ca, &cb, ext, ext_y0, ext_rows))
                    continue;
                v = (int16_t)((int32_t)(y - top) * g_size / size);
                if (v != last_v) {
                    make_line(cel, v, b->orient, t->thr);
                    last_v = v;
                }
                rv_scale_row(g_line, g_map, ca - left, cb - left, vid_row(y) + left);
            }
        }
    }
    overlays(b, t);
}

void explode_draw_begin(void)
{
    g_next = 0;
}

void explode_draw_to(int32_t depth, int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows)
{
    while (g_next < g_nord && g_b[g_order[g_next]].depth > depth)
        draw_one(&g_b[g_order[g_next++]], y0, y1, ext, ext_y0, ext_rows);
}

void explode_draw_rest(int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows)
{
    while (g_next < g_nord)
        draw_one(&g_b[g_order[g_next++]], y0, y1, ext, ext_y0, ext_rows);
}
