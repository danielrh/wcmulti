/* rocks.c: asteroid fields (rocks.h).
 *
 * The bubble.  While the player is in a field, a fixed set of rocks is kept
 * in a sphere of ROCK_BUBBLE_KM about them, as many as the field's density
 * asks (rocks per (10,000 km)^3: ROCK_DENSITY when the mission does not say)
 * up to ROCK_MAX.  Once a frame every rock goes through the camera
 * (rv_rocks_place, rocks.asm): the ones on screen out, the ones that have
 * left the sphere (the player has flown on past them) to a list from which
 * each is put back on the sphere's far side, where the player's way goes
 * (cosine-weighted about it: as rocks of an endless field would come in),
 * inside the field or not at all; the ones within reach to a list tested
 * against the player's hit box.  A rock past ROCK_FADE of the bubble's
 * radius shrinks, to nothing at its edge, so a rock comes in from a point
 * and none blinks into being.  A field the player has only just come near
 * gets its rocks the same way, from the far side, as they fly into it.
 *
 * Every machine has its own: the rocks are no ship slots and nothing about
 * them is sent, they come from a random stream of this module's own (never
 * randomSeed, the play's shared one), and only the player's own ship hits
 * them.  A hit spends shields and armour through do_damage (the thunk: the
 * layer sees it), the field's slot as its source; the player is put back
 * out of the rock and loses most of their speed.
 *
 * The art (ROCKS.DAT) is read into XMS at its own level (XMS_HAZARD, above
 * the mission's: the archive's assets wanted meanwhile go there too) when
 * the player comes within the bubble of a field, and given back when they
 * are well away; so is the conventional memory (the pool, the lists, the
 * small sprites: about 30 KB at ROCK_MAX).  The module's data and the log's
 * formats are far (DGROUP is nearly full). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <malloc.h>
#include <dos.h>
#include <fcntl.h>
#include "rv.h"
#include "rvhooks.h"
#include "music.h"
#include "campaign.h"
#include "bolts.h"
#include "rocks.h"

#define FILE_HEAD 24
#define KINDS_MAX 4
#define SIZES_MAX 64
#define DMAX 512                        /* the largest diameter drawn: a ladder sprite as 4 x 4 blocks */
#define SUM_SQ(n) ((unsigned long)(n) * ((n) + 1) * (2 * (n) + 1) / 6)
#define ROCK_MAX 1200
#define ROCK_OUT 400                    /* on screen at most */
#define ROCK_REAP 96                    /* brought round in a frame at most (the rest wait a frame) */
#define ROCK_HITS 16
#define ROCK_BUBBLE_KM 9000L            /* the sphere kept full about the player */
#define ROCK_FADE 60                    /* percent of it from which a rock shrinks, to nothing at its edge */
#define ROCK_REACH_KM 700L              /* a ship's half box and some: within it and a rock's radius, tested */
#define FIELDS_MAX 7                    /* fieldTable's records */
#define FIELD_SLOT(i) (RV_CAMERA_SLOT - 1 - (i))   /* a field's local-only hazard slot */
#define ZONE_PAD_KM 2000L               /* the art stays this much past the bubble's reach of a field */
#define SHIFT_KEY 18                    /* depth keys: camera units >> 18 = 64 km a key */
#define AIM_K 720                       /* a rock set on the player's course every AIM_K / density seconds */
#define ROCK_KM_MIN 110L                /* a rock's radius: a quarter to a half of a fighter (the user, 2026-10-24) */
#define ROCK_KM_SPAN 130L
#define ROCK_SPEED_MIN 3                /* km a frame each rock flies at, toward where the player will be */
#define ROCK_SPEED_SPAN 5
#define AIM_KM 4000L                    /* ... off that point by up to this, across and up */
#define FRAG_MAX 32                     /* pieces of broken rock in flight at once */
#define FRAG_N 7                        /* a rock breaks into so many */

int g_rock_xms = 1;
static char s_file[] = "ROCKS.DAT";     /* (near: DOS wants it so) */

/* ROCKS.DAT, in XMS while near a field */
static XmsBlock __far g_blk;
static unsigned __far g_handle;
static unsigned long __far g_base;      /* the block's offset in the handle's memory */
static int __far g_kinds, __far g_frames, __far g_sframes, __far g_small, __far g_nbig, __far g_bigmax;
static unsigned char __far g_bigd[SIZES_MAX];
static unsigned long __far g_index_at;  /* the big sprites' index: u32 offsets in the file (= in the block) */
static unsigned char __far g_size_of[DMAX + 1], __far g_k_of[DMAX + 1];   /* a diameter's ladder size and blocks */
#define HDR_MAX (4 + 9 * 128 + 2)        /* a record's head and run table at most: 128 rows of 4 runs */
static unsigned char __far g_hdr[HDR_MAX];   /* a record's head and run table, fetched */
static unsigned long __far g_size;      /* ROCKS.DAT's bytes (the block's) */

/* the rocks, round the player while near a field */
static LzRock __far *__far g_r;
static int __far g_n, __far g_cap, __far g_want_n;
static LzRockOut __far *__far g_o;
static int __far g_no;
static LzBallOut __far *__far g_so;    /* the small ones on screen, as balls */
static int __far g_ns;
static short __far *__far g_order;      /* the big ones on screen, farthest first (indices into g_o) */
static int __far g_nb, __far g_next;
static unsigned char __far *__far g_smallspr;   /* every kind's small sprites (g_sframes frames, 1 .. g_small px) */
static unsigned __far g_small_set;      /* a set's bytes */
static short __far g_reap[ROCK_REAP], __far g_hit[ROCK_HITS];
static LzCam __far g_cam;
static LzRockCam __far g_rc;
static unsigned long __far g_seed = 99991UL;
static unsigned short __far g_aim_at;   /* the frame the next rock is set on the player's course */
static long __far g_kb[3];              /* the player tossed aside by a rock: positions a frame, dying away */
static LzRock __far g_frag[FRAG_MAX];   /* the pieces of the rocks the player broke */
static unsigned char __far g_frag_life[FRAG_MAX];   /* frames each has left */
static int __far g_nfrag;

/* the fields */
typedef struct {
    long x, y, z, r_km;
    int density;
    unsigned char type, slot;
} Field;
static Field __far g_f[FIELDS_MAX];
static int __far g_nf, __far g_active = -1;   /* the field whose rocks are out (-1: none, no art) */
static int __far g_wanted;

/* the tally */
static unsigned long __far g_t_place, __far g_t_small, __far g_t_big, __far g_t_keep, __far g_t_blocks;
static unsigned long __far g_frames_in, __far g_sum_live, __far g_sum_small, __far g_sum_big, __far g_rows;
static unsigned long __far g_respawned, __far g_hits_n, __far g_aimed, __far g_blocks_n;
static int __far g_max_live, __far g_max_on;

static unsigned rnd(void)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (unsigned)(g_seed >> 16) & 0x7FFF;
}

void rocks_want(int on) { g_wanted = on; }
const char *rocks_file(void) { return g_wanted ? s_file : 0; }

unsigned long rocks_room_kb(void)
{
    int fd;
    unsigned long size;
    if (!g_wanted || _dos_open(s_file, O_RDONLY, &fd) != 0)
        return 0;
    size = lseek(fd, 0, SEEK_END);
    _dos_close(fd);
    return size / 1024 + 64;
}

/* --- the art --- */

static int art_in(void)
{
    static const char __far s_magic[] = "RVROCKS", __far s_none[] = "rocks: no ROCKS.DAT", __far s_other[] = "rocks: ROCKS.DAT is not this build's",
        __far s_in[] = "rocks: %d kinds, %d frames, 1 .. %d px small, %d sizes to %d: %lu KB in XMS in %lu ms",
        __far s_mem[] = "rocks: no XMS for ROCKS.DAT (%lu KB)";
    unsigned char *b = rv_filebuf;
    unsigned long size, at, t0 = clock240();
    unsigned got;
    int fd, i, d;
    if (_dos_open(s_file, O_RDONLY, &fd) != 0) {
        rv_logf(s_none);
        return 0;
    }
    if (_dos_read(fd, b, 160, &got) != 0 || got < FILE_HEAD || _fmemcmp(b, s_magic, 8) != 0
        || (b[8] | b[9] << 8) != 2) {
        rv_logf(s_other);
        _dos_close(fd);
        return 0;
    }
    g_kinds = b[10] | b[11] << 8;
    g_frames = b[12] | b[13] << 8;
    g_sframes = b[14] | b[15] << 8;
    g_small = b[16] | b[17] << 8;
    g_nbig = b[18] | b[19] << 8;
    g_bigmax = b[20] | b[21] << 8;
    if (g_kinds < 1 || g_kinds > KINDS_MAX || g_nbig < 1 || g_nbig > SIZES_MAX || g_bigmax > 128 || g_small != LZ_BALL_SMALL
        || (g_frames & (g_frames - 1)) || g_sframes < 1 || g_frames % g_sframes || FILE_HEAD + g_nbig > 160) {
        rv_logf(s_other);
        _dos_close(fd);
        return 0;
    }
    _fmemcpy(g_bigd, b + FILE_HEAD, g_nbig);
    g_small_set = (unsigned)SUM_SQ(g_small);
    g_index_at = FILE_HEAD + ((g_nbig + 1) & ~1) + (unsigned long)g_kinds * g_sframes * g_small_set;
    size = lseek(fd, 0, SEEK_END);
    g_size = size;
    g_blk = xms_alloc(size + 2, XMS_HAZARD);
    if (!g_blk) {
        rv_logf(s_mem, size / 1024);
        _dos_close(fd);
        return 0;
    }
    lseek(fd, 0, SEEK_SET);
    for (at = 0; at < size; at += 32768U) {
        unsigned part = size - at < 32768U ? (unsigned)(size - at) : 32768U;
        if (_dos_read(fd, gfx_scratch(), part, &got) != 0 || got != part)
            break;
        xms_put(g_blk, at, gfx_scratch(), (part + 1) & ~1U);
    }
    _dos_close(fd);
    {
        unsigned h;
        unsigned long off;
        xms_where(g_blk, &h, &off);
        g_handle = h;
        g_base = off;
    }
    /* a diameter's ladder size (the nearest), and past the ladder its blocks: the fewest that reach it */
    for (d = 1; d <= DMAX; d++) {
        int k = d <= g_bigmax ? 1 : (d + g_bigmax - 1) / g_bigmax, want = (d + k / 2) / k, best = 0;
        for (i = 1; i < g_nbig; i++)
            if (abs(g_bigd[i] - want) < abs(g_bigd[best] - want))
                best = i;
        g_size_of[d] = (unsigned char)best;
        g_k_of[d] = (unsigned char)(k > 4 ? 4 : k);
    }
    rv_logf(s_in, g_kinds, g_frames, g_small, g_nbig, g_bigmax, size / 1024, (clock240() - t0) * 1000 / 240);
    return 1;
}

/* --- the pool --- */

static void pool_free(void)
{
    if (g_r) _ffree(g_r);
    if (g_o) _ffree(g_o);
    if (g_so) _ffree(g_so);
    if (g_order) _ffree(g_order);
    if (g_smallspr) _ffree(g_smallspr);
    g_r = 0;
    g_o = 0;
    g_so = 0;
    g_order = 0;
    g_smallspr = 0;
    g_n = g_cap = g_no = g_ns = g_nb = 0;
}

static int pool_alloc(int want)
{
    static const char __far s_room[] = "rocks: room for %d rocks (%d asked), %lu KB of DOS memory left";
    unsigned long sbytes = (unsigned long)g_kinds * g_sframes * g_small_set;
    g_smallspr = (unsigned char __far *)_fmalloc((size_t)sbytes);
    g_o = (LzRockOut __far *)_fmalloc(ROCK_OUT * sizeof(LzRockOut));
    g_so = (LzBallOut __far *)_fmalloc(ROCK_OUT * sizeof(LzBallOut));
    g_order = (short __far *)_fmalloc(ROCK_OUT * sizeof(short));
    if (!g_smallspr || !g_o || !g_so || !g_order)
        return 0;
    xms_get(g_blk, FILE_HEAD + ((g_nbig + 1) & ~1), g_smallspr, sbytes);
    if (want > ROCK_MAX)
        want = ROCK_MAX;
    while (want >= 16 && (g_r = (LzRock __far *)_fmalloc((size_t)want * sizeof(LzRock))) == 0)
        want = want * 3 / 4;
    g_cap = g_r ? want : 0;
    rv_logf(s_room, g_cap, g_want_n, bolts_dos_free() / 1024);
    return g_cap > 0;
}

/* --- the fields --- */

static void field_slot(int i, int on)
{
    int s = FIELD_SLOT(i);
    const Field __far *f = &g_f[i];
    unsigned char *rec = fieldTable + fieldTable_SIZE * i;
    if (!on) {
        maybeEntityType[s] = 0;
        rvFlags[s] = 0;
        memset(rec, 0, fieldTable_SIZE);
        return;
    }
    maybeEntityType[s] = f->type;
    gInstanceShipTypes[s] = f->type == RV_FIELD_ASTEROID ? 0x16 : 0x17;
    parentShipForShip[s] = 0xFF;
    rvController[s] = 4;                /* (frozen: nothing flies it) */
    rvFlags[s] = 2;                     /* a local-only hazard: every machine has its own */
    gPositionVector[s].x = f->x;
    gPositionVector[s].y = f->y;
    gPositionVector[s].z = f->z;
    gMaybeShipRadius[s] = (unsigned short)(f->r_km > 65535L ? 65535U : (unsigned)f->r_km);
    /* the record the layer reads: type, position, radius (km), density */
    *(unsigned short *)(rec + 0) = f->type;
    *(long *)(rec + 2) = f->x;
    *(long *)(rec + 6) = f->y;
    *(long *)(rec + 10) = f->z;
    *(long *)(rec + 14) = f->r_km;
    rec[18] = (unsigned char)f->density;
    rec[19] = 0;
}

int rocks_field_add(int type, long x, long y, long z, long radius_km, int density)
{
    static const char __far s_add[] = "rocks: field %d (%Fs) at %ld,%ld,%ld km, %ld km across, %d rocks per (10,000 km)^3";
    static const char __far s_ast[] = "asteroids", __far s_mine[] = "mines";
    Field __far *f;
    if (g_nf >= FIELDS_MAX)
        return -1;
    f = &g_f[g_nf];
    f->type = (unsigned char)type;
    f->x = x;
    f->y = y;
    f->z = z;
    f->r_km = radius_km;
    f->density = density > 0 ? density : ROCK_DENSITY;
    f->slot = (unsigned char)FIELD_SLOT(g_nf);
    field_slot(g_nf, 1);
    rv_logf(s_add, g_nf, type == RV_FIELD_ASTEROID ? s_ast : s_mine, x / 256, y / 256, z / 256, 2 * radius_km, f->density);
    fieldCount = (unsigned short)++g_nf;
    return g_nf - 1;
}

/* the player's distance from a field's middle, past its edge: km (negative inside) */
static long beyond(const Field __far *f)
{
    long dx = (gPositionVector[0].x - f->x) / 256 / 16, dy = (gPositionVector[0].y - f->y) / 256 / 16,
        dz = (gPositionVector[0].z - f->z) / 256 / 16, m;
    /* (sixteen km a step: the squares stay in 31 bits to 740,000 km) */
    m = labs(dx) > labs(dy) ? labs(dx) : labs(dy);
    if (labs(dz) > m)
        m = labs(dz);
    if (m > 40000L)
        return m * 16 - f->r_km;
    return (long)lf_sqrt((unsigned long)(dx * dx + dy * dy + dz * dz)) * 16 - f->r_km;
}

/* a point (positions) inside a field */
static int in_field(const Field __far *f, long x, long y, long z)
{
    long dx = (x - f->x) / 256 / 16, dy = (y - f->y) / 256 / 16, dz = (z - f->z) / 256 / 16, r = f->r_km / 16;
    if (labs(dx) > r || labs(dy) > r || labs(dz) > r)
        return 0;
    return (unsigned long)(dx * dx + dy * dy + dz * dz) <= (unsigned long)(r * r);
}

/* a new rock's looks: its kind, its radius (110 .. 240 km: a quarter to a half of a fighter), its spin's phase and
 * speed (a turn in one to four seconds, either way) */
static void rock_looks(LzRock __far *r)
{
    unsigned spin;
    r->kind = (unsigned char)(rnd() % g_kinds);
    r->r8 = (unsigned char)((ROCK_KM_MIN + (long)(rnd() % (ROCK_KM_SPAN + 1))) / 8);
    r->phase = (unsigned char)(rnd() & (g_frames - 1));
    spin = 1 + rnd() % 4;
    r->spin = (signed char)((rnd() & 1) ? spin : -(int)spin);
    r->pad = 0;
}

/* its velocity (the user, 2026-10-24): toward where the player will be when it gets there (their position and
 * velocity, and its time to them at its speed and theirs), off that point by up to AIM_KM across and up at random
 * when `off` (so most fly close by, now and then one straight in), at ROCK_SPEED_MIN .. +SPAN km a frame */
static void aim(LzRock __far *r, const LfBasis *cam, int off)
{
    long s = ROCK_SPEED_MIN + (long)(rnd() % (ROCK_SPEED_SPAN + 1)), dx, dy, dz, d, t, a = 0, b = 0;
    dx = (gPositionVector[0].x - r->x) / 256;   /* (km) */
    dy = (gPositionVector[0].y - r->y) / 256;
    dz = (gPositionVector[0].z - r->z) / 256;
    d = (long)lf_sqrt((unsigned long)(dx * dx + dy * dy + dz * dz));
    t = d / (s + (long)(speedLo[0] / 20) + 1);   /* frames to meet, head on */
    dx += gVelocityVector[0].x * t / 256;
    dy += gVelocityVector[0].y * t / 256;
    dz += gVelocityVector[0].z * t / 256;
    if (off) {
        a = (long)(rnd() % (2 * AIM_KM + 1)) - AIM_KM;
        b = (long)(rnd() % (2 * AIM_KM + 1)) - AIM_KM;
    }
    dx += (a * cam->right.x + b * cam->up.x) / 256;
    dy += (a * cam->right.y + b * cam->up.y) / 256;
    dz += (a * cam->right.z + b * cam->up.z) / 256;
    d = (long)lf_sqrt((unsigned long)(dx * dx + dy * dy + dz * dz));
    if (d < 1)
        d = 1;
    r->vx = (short)(dx * s * 256 / d);
    r->vy = (short)(dy * s * 256 / d);
    r->vz = (short)(dz * s * 256 / d);
}

/* a rock somewhere on the bubble's far side, about the way the player goes; 0 when that is outside the field */
static int rock_ahead(LzRock __far *r, const LfBasis *cam, const Field __far *f)
{
    int tries;
    for (tries = 0; tries < 3; tries++) {
        long a, b, w, dx, dy, dz, R = ROCK_BUBBLE_KM * 31 / 32;
        do {                            /* a point of the unit disc, raised to the hemisphere: cosine-weighted */
            a = (long)(rnd() % 2049) - 1024;
            b = (long)(rnd() % 2049) - 1024;
        } while (a * a + b * b > 1024L * 1024);
        w = (long)lf_sqrt((unsigned long)(1024L * 1024 - a * a - b * b));
        /* the way in positions per km: (a right + b up + w front) / 1024, each 256 = 1.0 */
        dx = (a * cam->right.x + b * cam->up.x + w * cam->front.x) >> 6;
        dy = (a * cam->right.y + b * cam->up.y + w * cam->front.y) >> 6;
        dz = (a * cam->right.z + b * cam->up.z + w * cam->front.z) >> 6;
        r->x = gPositionVector[0].x + dx * R / 4096 * 256;
        r->y = gPositionVector[0].y + dy * R / 4096 * 256;
        r->z = gPositionVector[0].z + dz * R / 4096 * 256;
        if (in_field(f, r->x, r->y, r->z)) {
            rock_looks(r);
            aim(r, cam, 1);
            return 1;
        }
    }
    return 0;
}

/* a rock on the player's course: at the bubble's edge dead ahead (where a rock grows in from nothing), off the
 * line by less than its radius and the ship's half box, so flying on straight is flying into it: the field's
 * dodging, now and then whatever the rocks' own spread would bring (0 when ahead is outside the field) */
static int rock_on_course(LzRock __far *r, const LfBasis *cam, const Field __far *f)
{
    long a, b, R = ROCK_BUBBLE_KM * 15 / 16;
    rock_looks(r);
    a = (long)(rnd() % 513) - 256;      /* (within half its radius of the line, either way: km) */
    b = (long)(rnd() % 513) - 256;
    a = a * r->r8 * 4 / 256;
    b = b * r->r8 * 4 / 256;
    r->x = gPositionVector[0].x + cam->front.x * R + cam->right.x * a + cam->up.x * b;
    r->y = gPositionVector[0].y + cam->front.y * R + cam->right.y * a + cam->up.y * b;
    r->z = gPositionVector[0].z + cam->front.z * R + cam->right.z * a + cam->up.z * b;
    aim(r, cam, 0);                     /* (at where the player will be, dead on) */
    return in_field(f, r->x, r->y, r->z);
}

/* the bubble filled, as the flight starts in a field: anywhere in it, in the field, not on the player */
static void fill(const Field __far *f, const LfBasis *cam)
{
    int tries = 0;
    while (g_n < g_want_n && g_n < g_cap && tries++ < g_want_n * 4) {
        long x, y, z, R = ROCK_BUBBLE_KM;
        LzRock __far *r = &g_r[g_n];
        x = (long)(rnd() % 2001) - 1000;
        y = (long)(rnd() % 2001) - 1000;
        z = (long)(rnd() % 2001) - 1000;
        if (x * x + y * y + z * z > 1000000L || x * x + y * y + z * z < 40000L)   /* (none within a fifth) */
            continue;
        r->x = gPositionVector[0].x + x * R / 1000 * 256;
        r->y = gPositionVector[0].y + y * R / 1000 * 256;
        r->z = gPositionVector[0].z + z * R / 1000 * 256;
        if (!in_field(f, r->x, r->y, r->z))
            continue;
        rock_looks(r);
        aim(r, cam, 1);
        g_n++;
    }
}

#ifdef RV_TESTS
static void rocks_time(void);
#endif

static void field_enter(int i, int filled, const LfBasis *cam)
{
    static const char __far s_in[] = "rocks: near field %d: %d rocks kept round the player (%Fs)",
        __far s_full[] = "the bubble filled", __far s_edge[] = "they come in ahead";
    const Field __far *f = &g_f[i];
    /* (the bubble's volume, (4/3) pi R^3, in thousandths of (10,000 km)^3) */
    long vol1000 = 4189L * (ROCK_BUBBLE_KM / 100) * (ROCK_BUBBLE_KM / 100) / 1000 * (ROCK_BUBBLE_KM / 100) / 1000;
    g_want_n = (int)((long)f->density * vol1000 / 1000);
    if (g_want_n < 1)
        g_want_n = 1;
    dat_level(XMS_HAZARD);              /* (first: the art goes at the new level, the archive's reads after it) */
    if (!art_in() || !pool_alloc(g_want_n)) {
        pool_free();
        xms_free_level(XMS_HAZARD);
        dat_level(XMS_MISSION);
        g_blk = 0;
        g_active = -2 - i;              /* (tried: not again until the player leaves) */
        return;
    }
    g_seed = 99991UL + 7919UL * i + frameCounter;
    g_n = 0;
    if (filled)
        fill(f, cam);
#ifdef RV_TESTS
    {
        static int __far checked;
        if (!checked++) {
#ifndef RV_NO_TWINS                     /* (DOS's RVT.EXE has no twins: RVP.EXE /twins checks the place there) */
            rocks_selftest();
#endif
            rocks_time();
        }
    }
#endif
    g_active = i;
    g_nfrag = 0;
    g_aim_at = (unsigned short)(frameCounter + 30L * AIM_K / f->density);
    rv_logf(s_in, i, g_want_n, filled ? s_full : s_edge);
}

static void field_leave(void)
{
    static const char __far s_out[] = "rocks: away from field %d: its rocks and art given back";
    if (g_active >= 0)
        rv_logf(s_out, g_active);
    pool_free();
    if (g_blk) {
        xms_free_level(XMS_HAZARD);
        dat_level(XMS_MISSION);
    }
    g_blk = 0;
    g_active = -1;
    g_nfrag = 0;
    fieldRockTally = 0;
}

void rocks_flight_start(void)
{
    int i;
    g_nf = 0;
    g_active = -1;
    g_kb[0] = g_kb[1] = g_kb[2] = 0;
    fieldCount = 0;
    fieldRockTally = 0;
    currentField = 0;
    memset(entitiesToDespawn, 0xFF, entitiesToDespawn_SIZE);   /* (no rock is a slot) */
    for (i = 0; i < FIELDS_MAX; i++)
        memset(fieldTable + fieldTable_SIZE * i, 0, fieldTable_SIZE);
    g_t_place = g_t_small = g_t_big = g_t_keep = g_t_blocks = 0;
    g_frames_in = g_sum_live = g_sum_small = g_sum_big = g_rows = g_respawned = g_hits_n = g_aimed = g_blocks_n = 0;
    g_max_live = g_max_on = 0;
}

void rocks_flight_end(void)
{
    int i;
    field_leave();
    for (i = 0; i < g_nf; i++)
        field_slot(i, 0);
    g_nf = 0;
    fieldCount = 0;
    currentField = 0;
}

/* the layer's "rocks off" (the thunk's body): this machine's rocks gone, the fields forgotten */
void __far __cdecl clearFields_impl(void)
{
    static const char __far s_clear[] = "rocks: the fields cleared";
    rocks_flight_end();
    rv_logf(s_clear);
}

/* --- a frame --- */

/* a rock the player hit breaks (the user, 2026-10-24): FRAG_N pieces a third its size, from about where it was,
 * flung out every way and going on with three quarters of the player's velocity (so they are seen spreading
 * ahead), each spinning fast, gone in one to two seconds; never hit */
static void shatter(const LzRock __far *r)
{
    long R = 8L * r->r8;
    int k;
    for (k = 0; k < FRAG_N && g_nfrag < FRAG_MAX; k++) {
        LzRock __far *f = &g_frag[g_nfrag];
        long ox = (long)(rnd() % 2001) - 1000, oy = (long)(rnd() % 2001) - 1000, oz = (long)(rnd() % 2001) - 1000;
        *f = *r;
        f->r8 = (unsigned char)(r->r8 / 4 + 1 + rnd() % (r->r8 / 4 + 1));
        f->x = r->x + ox * R / 1000 * 128;  /* (within half its radius) */
        f->y = r->y + oy * R / 1000 * 128;
        f->z = r->z + oz * R / 1000 * 128;
        f->vx = (short)(gVelocityVector[0].x * 3 / 4 + ox * 6 * 256 / 1000);   /* (6 km a frame outward at most) */
        f->vy = (short)(gVelocityVector[0].y * 3 / 4 + oy * 6 * 256 / 1000);
        f->vz = (short)(gVelocityVector[0].z * 3 / 4 + oz * 6 * 256 / 1000);
        f->phase = (unsigned char)(rnd() & (g_frames - 1));
        f->spin = (signed char)((rnd() & 1) ? 4 : -4);
        g_frag_life[g_nfrag++] = (unsigned char)(30 + rnd() % 30);
    }
}

/* the player's hits: each rock within reach against their ship's hit box (a sphere against a box, in the ship's
 * frame, km) */
static void hits(int n, const LfBasis *cam)
{
    static const char __far s_hit[] = "rocks: frame %u: a %d km rock (kind %d) hit the player at %lu kps, %d%% head on: "
        "%u damage";
    const RvShipType __far *t = ship_type(gInstanceShipTypes[0]);
    long h[3], H;
    int i, k;
    if (!t || maybeEntityType[0] != 0xC)
        return;
    H = (long)t->lod0_px * 800 / 256;   /* (collide.c's half box, km) */
    for (k = 0; k < 3; k++)
        h[k] = H * t->hit[k] / 127;
    for (i = 0; i < n; i++) {
        LzRock __far *r = &g_r[g_hit[i]];
        long rx = (r->x - gPositionVector[0].x) / 16, ry = (r->y - gPositionVector[0].y) / 16,
            rz = (r->z - gPositionVector[0].z) / 16, p[3], c[3], d[3], R = 8L * r->r8, d2, dl;
        unsigned long speed = speedLo[0];
        int head;
        unsigned short qty;
        /* into the ship's frame, km (sixteenths x 256-scaled axes / 4096) */
        p[0] = (rx * cam->right.x + ry * cam->right.y + rz * cam->right.z) / 4096;
        p[1] = (rx * cam->up.x + ry * cam->up.y + rz * cam->up.z) / 4096;
        p[2] = (rx * cam->front.x + ry * cam->front.y + rz * cam->front.z) / 4096;
        for (k = 0; k < 3; k++) {
            c[k] = p[k] < -h[k] ? -h[k] : p[k] > h[k] ? h[k] : p[k];
            d[k] = p[k] - c[k];
        }
        d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (d2 >= R * R)
            continue;
        dl = (long)lf_sqrt((unsigned long)d2);
        if (dl < 1)
            dl = 1;
        g_hits_n++;
        /* the harm: the speed it was hit at, the more head on the more, and the rock's size */
        head = (int)(d[2] * 100 / dl);
        if (head < 20)
            head = 20;
        qty = (unsigned short)(speed * head / 100 * R / 4000 + 1);
        ((RvVec3 *)tempVector)->x = gPositionVector[0].x + (c[0] * cam->right.x + c[1] * cam->up.x + c[2] * cam->front.x);
        ((RvVec3 *)tempVector)->y = gPositionVector[0].y + (c[0] * cam->right.y + c[1] * cam->up.y + c[2] * cam->front.y);
        ((RvVec3 *)tempVector)->z = gPositionVector[0].z + (c[0] * cam->right.z + c[1] * cam->up.z + c[2] * cam->front.z);
        rv_logf(s_hit, frameCounter, (int)(2 * R), r->kind, speed, head, qty);
        do_damage((unsigned short)g_f[g_active].slot, 0, qty, (unsigned short)(unsigned)tempVector);
        /* tossed aside (the user, 2026-10-24): across the player's way, away from the rock (dead ahead: a side
         * at random), at a speed of its own dying away over a second; theirs is kept */
        {
            long tx = -d[0], ty = -d[1], tl, kb = (long)speed / 20 + 30;   /* (km a frame) */
            if (labs(tx) + labs(ty) < R / 4) {
                tx = (rnd() & 1) ? R : -R;
                ty = (long)(rnd() % (2 * R + 1)) - R;
            }
            tl = (long)lf_sqrt((unsigned long)(tx * tx + ty * ty));
            if (tl < 1)
                tl = 1;
            g_kb[0] = (tx * cam->right.x + ty * cam->up.x) * kb / tl;
            g_kb[1] = (tx * cam->right.y + ty * cam->up.y) * kb / tl;
            g_kb[2] = (tx * cam->right.z + ty * cam->up.z) * kb / tl;
        }
        /* the rock breaks: its pieces fly, and it is brought round ahead (or sent out of the bubble, keep's then) */
        shatter(r);
        if (!rock_ahead(r, cam, &g_f[g_active]))
            r->x = gPositionVector[0].x + 0x3000000L;
    }
}

/* the reaped ones brought round ahead (or out of the set where that is outside the field), and the set topped up
 * to the field's number from the far side */
static void keep(const LfBasis *cam, int nreap)
{
    const Field __far *f = &g_f[g_active];
    int i, top = 0;
    /* (the highest index first: a removal moves the last rock into the hole) */
    /* now and then the first one round goes on the player's course instead (only while they fly) */
    if (nreap && speedLo[0] > 20 && (short)(frameCounter - g_aim_at) >= 0) {
        int k = g_reap[0];
        LzRock was = g_r[k];
        if (rock_on_course(&g_r[k], cam, f)) {
            g_aimed++;
            g_reap[0] = g_reap[--nreap];
        } else
            g_r[k] = was;               /* (ahead is out of the field: brought round below as the others) */
        g_aim_at = (unsigned short)(frameCounter + 30L * AIM_K / f->density / 2 + rnd() % (30L * AIM_K / f->density));
    }
    for (i = 0; i < nreap; i++) {
        int k, j = i;
        for (k = i + 1; k < nreap; k++)
            if (g_reap[k] > g_reap[j])
                j = k;
        k = g_reap[j];
        g_reap[j] = g_reap[i];
        g_reap[i] = (short)k;
        if (!rock_ahead(&g_r[k], cam, f))
            g_r[k] = g_r[--g_n];
        g_respawned++;
    }
    while (g_n < g_want_n && g_n < g_cap && top++ < 8)
        if (rock_ahead(&g_r[g_n], cam, f))
            g_n++;
    fieldRockTally = (unsigned short)g_n;
}

/* the depth keys: farthest first (a counting sort on 64 km steps) */
static void order_big(void)
{
    static unsigned short __far count[256];
    static unsigned char __far key[ROCK_OUT];
    int i, k, at;
    _fmemset(count, 0, sizeof count);
    for (i = 0; i < g_nb; i++) {
        long z = g_o[g_order[i]].z >> SHIFT_KEY;
        key[i] = (unsigned char)(255 - (z > 255 ? 255 : z));
        count[key[i]]++;
    }
    for (k = 0, at = 0; k < 256; k++) {
        int c = count[k];
        count[k] = (unsigned short)at;
        at += c;
    }
    {
        static short __far tmp[ROCK_OUT];
        for (i = 0; i < g_nb; i++)
            tmp[count[key[i]]++] = g_order[i];
        _fmemcpy(g_order, tmp, g_nb * sizeof(short));
    }
}

void rocks_frame(const LfBasis *cam, int cx, int cy, int focal, int y0, int y1)
{
    unsigned long t0 = clock240();
    long f3[3], r3[3], u3[3];
    int i, near_one = -1, nreap, nhit;
    /* which field the player is near, in, or away from (every 16 frames is plenty) */
    if ((frameCounter & 15) == 0 || g_active == -1) {
        currentField = 0;
        for (i = 0; i < g_nf; i++) {
            long out = beyond(&g_f[i]);
            if (out < 0)
                currentField = (unsigned short)(unsigned)(fieldTable + fieldTable_SIZE * i);
            /* near: the bubble reaches it (the one whose rocks are out keeps them a little farther) */
            if (g_f[i].type == RV_FIELD_ASTEROID && out < ROCK_BUBBLE_KM + (g_active == i ? ZONE_PAD_KM : 0)
                && (near_one < 0 || i == g_active))
                near_one = i;
        }
        if (g_active >= 0 && near_one != g_active)
            field_leave();
        if (g_active <= -2 && near_one != -2 - g_active)
            g_active = -1;              /* (left the one whose art would not come in) */
        if (g_active == -1 && near_one >= 0)
            field_enter(near_one, frameCounter < 2 && beyond(&g_f[near_one]) < 0, cam);
    }
    g_ns = g_nb = g_no = 0;
    if (g_kb[0] || g_kb[1] || g_kb[2]) {    /* (tossed aside by a rock, dying away) */
        gPositionVector[0].x += g_kb[0];
        gPositionVector[0].y += g_kb[1];
        gPositionVector[0].z += g_kb[2];
        for (i = 0; i < 3; i++)
            g_kb[i] = labs(g_kb[i]) < 64 ? 0 : g_kb[i] * 7 / 8;
    }
    if (g_active < 0 || !g_r)
        return;
    f3[0] = cam->front.x; f3[1] = cam->front.y; f3[2] = cam->front.z;
    r3[0] = cam->right.x; r3[1] = cam->right.y; r3[2] = cam->right.z;
    u3[0] = cam->up.x; u3[1] = cam->up.y; u3[2] = cam->up.z;
    lz_camera(&g_cam, gPositionVector[0].x, gPositionVector[0].y, gPositionVector[0].z, f3, r3, u3, cx, cy, focal, 0,
              100, 70000L, 0, 0, y0, y1);
    g_rc.dk = (unsigned long)focal * 65536UL * 16;   /* 2 x 8 km x 4,096 x focal x 16 / 16: d x16 = r8 dk / Z */
    g_rc.far2 = (unsigned long)ROCK_BUBBLE_KM * ROCK_BUBBLE_KM;
    g_rc.fade2 = g_rc.far2 / 10000 * ROCK_FADE * ROCK_FADE;
    g_rc.reach = ROCK_REACH_KM * 4096;
    g_rc.anim = frameCounter;
    g_rc.mask = (unsigned short)(g_frames - 1);
    g_rc.dmax = DMAX;
    g_rc.maxreap = ROCK_REAP;
    g_rc.maxclose = ROCK_HITS;
    g_no = rv_rocks_place(g_r, g_n, &g_cam, &g_rc, g_o, ROCK_OUT, g_reap, g_hit);
    nreap = g_rc.nreap;
    nhit = g_rc.nclose;
    if (g_nfrag) {                      /* the broken rocks' pieces: flown and placed the same way, never hit or reaped */
        LzRockCam rc = g_rc;
        rc.maxreap = rc.maxclose = 0;
        g_no += rv_rocks_place(g_frag, g_nfrag, &g_cam, (LzRockCam __far *)&rc, g_o + g_no, ROCK_OUT - g_no, g_reap,
                               g_hit);
        for (i = g_nfrag - 1; i >= 0; i--)
            if (--g_frag_life[i] == 0) {
                g_frag[i] = g_frag[--g_nfrag];
                g_frag_life[i] = g_frag_life[g_nfrag];
            }
    }
    g_t_place += clock240() - t0;
    /* small ones as balls, big ones in depth order */
    for (i = 0; i < g_no; i++) {
        const LzRockOut __far *o = &g_o[i];
        if (o->d <= (unsigned)g_small) {
            LzBallOut __far *b = &g_so[g_ns++];
            int d = o->d, f = o->frame * g_sframes / g_frames;
            b->x = (short)(o->x - (d - 1) / 2);
            b->y = (short)(o->y - (d - 1) / 2);
            b->d = (unsigned char)d;
            b->set = 0;
            b->pad = 0;
            b->spr = g_smallspr + (unsigned)((o->kind * g_sframes + f) * g_small_set + SUM_SQ(d - 1));
        } else
            g_order[g_nb++] = (short)i;
    }
    order_big();
    t0 = clock240();
    if (nhit)
        hits(nhit, cam);
    keep(cam, nreap);
    g_t_keep += clock240() - t0;
    g_frames_in++;
    g_sum_live += g_n;
    g_sum_small += g_ns;
    g_sum_big += g_nb;
    if (g_n > g_max_live) g_max_live = g_n;
    if (g_no > g_max_on) g_max_on = g_no;
}

int rocks_on_screen(int *small, int *big)
{
    *small = g_ns;
    *big = g_nb;
    return g_ns + g_nb;
}

int rocks_live(void) { return g_active >= 0 ? g_n : 0; }

/* --- the draws --- */

void rocks_draw_small(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    unsigned long t0 = clock240();
    int run0, run1;
    if (g_ns && vid_run(&run0, &run1) && bolts_rows(&y0, &y1, &ext, &ext_y0, ext_rows))
        rv_balls_draw(g_so, g_ns, vid_row(run0), run0, y0, y1, ext, ext_y0);
    g_t_small += clock240() - t0;
}

void rocks_draw_begin(void) { g_next = 0; }

/* a big sprite's record: its head and run table into g_hdr (one move: as much as there is room for, the pixels'
 * first bytes with it); its pixels' offset and (copy) their bytes returned */
static unsigned long record(int kind, int frame, int s, unsigned long *len)
{
    unsigned char e[4];
    unsigned long rec, n;
    unsigned i, rows, at;
    xms_get(g_blk, g_index_at + 4UL * (((unsigned long)kind * g_frames + frame) * g_nbig + s), e, 4);
    rec = e[0] | ((unsigned long)e[1] << 8) | ((unsigned long)e[2] << 16) | ((unsigned long)e[3] << 24);
    n = g_size - rec < HDR_MAX ? g_size - rec : HDR_MAX;
    xms_get(g_blk, rec, g_hdr, n);
    rows = g_hdr[0] | g_hdr[1] << 8;
    if (len) {
        for (i = 0, at = 4, *len = 0; i < rows; i++) {
            unsigned runs = g_hdr[at++];
            for (; runs; runs--, at += 2)
                *len += g_hdr[at + 1];
        }
    }
    return rec + 4 + (g_hdr[2] | g_hdr[3] << 8);
}

static void draw_big(const LzRockOut __far *o, int y0, int y1, const short __far *ext, int ext_y0, int run0)
{
    int d = o->d > DMAX ? DMAX : o->d, s = g_size_of[d], k = g_k_of[d], ds = g_bigd[s], D = ds * k, top, left;
    unsigned long pix, len;
    top = o->y - (D - 1) / 2;
    if (top >= y1 || top + D <= y0)
        return;
    left = o->x - (D - 1) / 2;
    if (left >= SCR_W || left + D <= 0)
        return;
    if (k == 1 && g_rock_xms) {
        pix = record(o->kind, o->frame, s, 0);
        rv_rocks_xms(g_hdr + 4, ds, left, top, g_handle, g_base + pix, vid_row(run0), run0, y0, y1, ext, ext_y0);
        g_rows += ds;
        return;
    }
    {
        unsigned long t0 = clock240();
        pix = record(o->kind, o->frame, s, &len);
        xms_get(g_blk, pix, gfx_scratch(), len);
        rv_rocks_copy(g_hdr + 4, ds, left, top, k, gfx_scratch(), vid_row(run0), run0, y0, y1, ext, ext_y0);
        g_rows += ds;
        if (k > 1) {
            g_t_blocks += clock240() - t0;
            g_blocks_n++;
        }
    }
}

void rocks_draw_to(long depth, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    unsigned long t0;
    int run0, run1;
    long z = depth * 16;                /* (positions to camera units) */
    if (g_next >= g_nb || g_o[g_order[g_next]].z <= z)
        return;
    t0 = clock240();
    if (vid_run(&run0, &run1) && bolts_rows(&y0, &y1, &ext, &ext_y0, ext_rows))
        while (g_next < g_nb && g_o[g_order[g_next]].z > z)
            draw_big(&g_o[g_order[g_next++]], y0, y1, ext, ext_y0, run0);
    g_t_big += clock240() - t0;
}

void rocks_draw_rest(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    rocks_draw_to(-0x7FFFFFFL, y0, y1, ext, ext_y0, ext_rows);
}

void rocks_report(void)
{
    static const char __far s_r[] = "rocks: %lu frames in a field: %lu rocks kept on average (%d at most), on screen "
        "%lu small and %lu big (%d at most), %lu brought round ahead (%lu on the player's course), %lu hits",
        __far s_ms[] = "rocks: ms a frame: placed %lu.%02lu, small drawn %lu.%02lu, big drawn %lu.%02lu (%lu rows, %Fs; "
        "of it the nearest, past 128 px, as blocks %lu.%02lu in %lu.%02lu band draws), kept and hit %lu.%02lu",
        __far s_xms[] = "a move a row", __far s_copy[] = "copied";
    unsigned long f = g_frames_in ? g_frames_in : 1;
#define MS(t) ((t) * 100000UL / 240 / f / 100), ((t) * 100000UL / 240 / f % 100)
    if (!g_frames_in)
        return;
    rv_logf(s_r, g_frames_in, g_sum_live / f, g_max_live, g_sum_small / f, g_sum_big / f, g_max_on, g_respawned, g_aimed, g_hits_n);
    rv_logf(s_ms, MS(g_t_place), MS(g_t_small), MS(g_t_big), g_rows / f, g_rock_xms ? s_xms : s_copy, MS(g_t_blocks),
            g_blocks_n / f, g_blocks_n * 100 / f % 100, MS(g_t_keep));
#undef MS
}

#ifdef RV_TESTS
/* --- rv_rocks_place checked (its C reference, lz_rocks_place, is twins.c's: the host draws with it) --- */

/* what a big rock's row costs each way (logged): one rock of each of a few sizes drawn again and again into a
 * band of its own, the whole width open */
static void rocks_time(void)
{
    static const char __far s_t[] = "rocks: a %d px rock: %ld.%02ld us a row moved by XMS, %ld.%02ld copied (%ld.%02ld with "
        "its one move), %ld.%02ld as 2 x 2 blocks a source row (%ld.%02ld a screen row)",
        __far s_mem[] = "rocks: no memory for the timings";
    static const char __far s_p[] = "rocks: placing %d rocks: %ld.%02ld us a rock (%d of them on screen)";
    static const unsigned char __far want[3] = { 32, 64, 128 };
    int rows = 64, w, i, k;
    unsigned char __far *band = (unsigned char __far *)_fmalloc((unsigned)rows * SCR_W);
    short __far *ext = (short __far *)_fmalloc(rows * 2 * sizeof(short));
    if (!band || !ext) {
        rv_logf(s_mem);
        goto out;
    }
    for (i = 0; i < rows; i++) {
        ext[2 * i] = 0;
        ext[2 * i + 1] = SCR_W;
    }
    if (g_n) {                          /* the set as it is, through a camera where the player is, looking down +z */
        long f3[3] = { 0, 0, 256 }, r3[3] = { 256, 0, 0 }, u3[3] = { 0, 256, 0 };
        unsigned long t0 = clock240(), it = 0, us;
        int on = 0;
        LzRockCam rc = g_rc;
        lz_camera(&g_cam, gPositionVector[0].x, gPositionVector[0].y, gPositionVector[0].z, f3, r3, u3, SCR_W / 2, 140,
                  400, 0, 100, 70000L, 0, 0, 20, 260);
        rc.dk = 400UL * 65536UL * 16;
        rc.far2 = (unsigned long)ROCK_BUBBLE_KM * ROCK_BUBBLE_KM;
        rc.fade2 = rc.far2 / 10000 * ROCK_FADE * ROCK_FADE;
        rc.reach = ROCK_REACH_KM * 4096;
        rc.mask = (unsigned short)(g_frames - 1);
        rc.dmax = DMAX;
        rc.maxreap = ROCK_REAP;
        rc.maxclose = ROCK_HITS;
        do {
            on = rv_rocks_place(g_r, g_n, &g_cam, (LzRockCam __far *)&rc, g_o, ROCK_OUT, g_reap, g_hit);
            it++;
        } while (clock240() - t0 < 60);
        us = (clock240() - t0) * 416667UL / it / g_n;
        rv_logf(s_p, g_n, us / 100, us % 100, on);
    }
    for (w = 0; w < 3; w++) {
        int s = g_size_of[want[w]], ds = g_bigd[s], n;
        unsigned long pix, len, t0, it, dt[4];
        pix = record(0, 0, s, &len);
        n = ds < rows ? ds : rows;      /* (the rows that fall in the band) */
        for (k = 0; k < 4; k++) {
            t0 = clock240();
            it = 0;
            do {
                if (k == 0)
                    rv_rocks_xms(g_hdr + 4, ds, 100, 0, g_handle, g_base + pix, band, 0, 0, rows, ext, 0);
                else {
                    if (k == 2 || it == 0)
                        xms_get(g_blk, pix, gfx_scratch(), len);
                    rv_rocks_copy(g_hdr + 4, ds, 100, 0, k == 3 ? 2 : 1, gfx_scratch(), band, 0, 0, rows, ext, 0);
                }
                it++;
            } while (clock240() - t0 < 60);
            /* hundredths of a microsecond a row */
            dt[k] = (clock240() - t0) * 416667UL / it / (k == 3 ? (unsigned long)(rows + 1) / 2 : (unsigned long)n);
        }
        rv_logf(s_t, ds, dt[0] / 100, dt[0] % 100, dt[1] / 100, dt[1] % 100, dt[2] / 100, dt[2] % 100,
                dt[3] / 100, dt[3] % 100, dt[3] / 200, dt[3] / 2 % 100);
    }
out:
    if (band) _ffree(band);
    if (ext) _ffree(ext);
}

#ifndef RV_NO_TWINS
long rocks_selftest(void)
{
    static const char __far s_mem[] = "rocks: no memory for the self-test",
        __far s_n[] = "rocks: THE PLACING DISAGREES: asm %d on screen, %d reaped, %d close; C %d, %d, %d",
        __far s_one[] = "rocks: rock %d: C %d,%d d %u k %d f %d z %ld; asm %d,%d d %u k %d f %d z %ld",
        __far s_ok[] = "rocks: the asm agrees with the C (%d of %d rocks on screen, %d reaped, %d close)";
#define TN 600
    LzRock __far *r = (LzRock __far *)_fmalloc(2 * TN * sizeof(LzRock)), __far *r2 = r + TN;   /* (placing flies them:
                                           the C and the asm each its own copy) */
    LzRockOut __far *oc = (LzRockOut __far *)_fmalloc(2 * ROCK_OUT * sizeof(LzRockOut)), __far *oa = oc + ROCK_OUT;
    short __far *lists = (short __far *)_fmalloc(4 * 64 * sizeof(short));
    LzRockCam rcc, rca;
    long f3[3] = { 0, 0, 256 }, r3[3] = { 256, 0, 0 }, u3[3] = { 0, 256, 0 };
    int i, nc, na, bad = 0;
    unsigned long seed = g_seed;
    if (!r || !oc || !lists) {
        rv_logf(s_mem);
        bad = -2;
        goto out;
    }
    /* a turned camera (front a little up and right, rolled), rocks every way round it, some at the edges */
    f3[0] = 60; f3[1] = 40; f3[2] = 246;
    r3[0] = 247; r3[1] = -30; r3[2] = -55;
    u3[0] = -38; u3[1] = 250; u3[2] = -33;
    lz_camera(&g_cam, 1000000L, -2000000L, 3000000L, f3, r3, u3, SCR_W / 2, 180, 400, 0, 100, 70000L, 0, 0, 40, 320);
    for (i = 0; i < TN; i++) {
        long s = i < 40 ? 1500 : i < 80 ? 40000 : 12000;   /* (some within reach, some far out of the bubble) */
        r[i].x = 1000000L + ((long)(rnd() % 2001) - 1000) * s / 1000 * 256;
        r[i].y = -2000000L + ((long)(rnd() % 2001) - 1000) * s / 1000 * 256;
        r[i].z = 3000000L + ((long)(rnd() % 2001) - 1000) * s / 1000 * 256;
        r[i].kind = (unsigned char)(rnd() & 3);
        r[i].r8 = (unsigned char)(10 + rnd() % 116);
        r[i].phase = (unsigned char)(rnd() & 31);
        r[i].spin = (signed char)((int)(rnd() % 9) - 4);
        r[i].vx = (short)((int)(rnd() % 4001) - 2000);
        r[i].vy = (short)((int)(rnd() % 4001) - 2000);
        r[i].vz = (short)((int)(rnd() % 4001) - 2000);
        r[i].pad = 0;
    }
    rcc.dk = 400UL * 65536UL * 16;
    rcc.far2 = (unsigned long)ROCK_BUBBLE_KM * ROCK_BUBBLE_KM;
    rcc.fade2 = rcc.far2 / 10000 * ROCK_FADE * ROCK_FADE;
    rcc.reach = ROCK_REACH_KM * 4096;
    rcc.anim = 12345;
    rcc.mask = 31;
    rcc.dmax = DMAX;
    rcc.maxreap = 64;
    rcc.maxclose = 64;
    rca = rcc;
    _fmemcpy(r2, r, TN * sizeof(LzRock));
    nc = lz_rocks_place(r, TN, &g_cam, (LzRockCam __far *)&rcc, oc, ROCK_OUT, lists, lists + 64);
    na = rv_rocks_place(r2, TN, &g_cam, (LzRockCam __far *)&rca, oa, ROCK_OUT, lists + 128, lists + 192);
    if (nc != na || _fmemcmp(r, r2, TN * sizeof(LzRock)) || rcc.nreap != rca.nreap || rcc.nclose != rca.nclose
        || _fmemcmp(lists, lists + 128, rcc.nreap * sizeof(short))
        || _fmemcmp(lists + 64, lists + 192, rcc.nclose * sizeof(short))) {
        rv_logf(s_n, na, rca.nreap, rca.nclose, nc, rcc.nreap, rcc.nclose);
        bad = -1;
    }
    for (i = 0; i < nc && i < na && !bad; i++)
        if (_fmemcmp(&oc[i], &oa[i], sizeof(LzRockOut))) {
            rv_logf(s_one, i, oc[i].x, oc[i].y, oc[i].d, oc[i].kind, oc[i].frame, oc[i].z, oa[i].x, oa[i].y, oa[i].d,
                    oa[i].kind, oa[i].frame, oa[i].z);
            bad = -1;
        }
    if (!bad)
        rv_logf(s_ok, na, TN, rca.nreap, rca.nclose);
out:
    g_seed = seed;
    if (r) _ffree(r);
    if (oc) _ffree(oc);
    if (lists) _ffree(lists);
    return bad ? bad : TN;
#undef TN
}
#endif /* RV_NO_TWINS */
#endif /* RV_TESTS */
