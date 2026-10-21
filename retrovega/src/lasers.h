/* lasers.h: laser bolts, from the world to the screen (lasers.c; the fast
 * paths are lowlevel.asm's rv_bolts_*).
 *
 * A bolt is a capsule: a segment along its flight with a radius round it, so
 * it shows from any angle (end on it is a disc).  Each frame the bolts go
 * through the camera once (lz_place): a bolt whose radius on screen is under
 * a pixel or so becomes a LINE, one pixel wide, drawn as a run of pixels per
 * row (never less than a pixel a row: a bolt is always visible); a bigger one
 * is FAT: a red capsule with a white core capsule (or core line) painted
 * over it, each row one span.  The draws then run per band (the band
 * buffer's rows), clipped to the cockpit's opening.
 *
 * Units: positions as the slots keep them (256 = 1 km); the bolts' half
 * lengths in sixteenths of a km; on screen 1/16 pixel ("x16"), and the line's
 * running x in 16.16 pixels.  Plain C with no DOS in it: tests/host builds it
 * with gcc (lz_test.c) and the flight checks the asm against it. */
#ifndef LASERS_H
#define LASERS_H

#ifdef __WATCOMC__
#define LZ_FAR __far
#else
#define LZ_FAR

#endif

/* a bolt in the world (18 bytes; the asm reads this layout) */
typedef struct {
    long x, y, z;                       /* its middle, positions */
    short hx, hy, hz;                   /* the middle to its head, sixteenths of a km */
} LzBolt;

/* the camera as the place routine wants it (lz_camera fills it; the asm
 * reads this layout).  "Camera units" are positions x 16 (4096 a km): the
 * relative position is taken in sixteenths of a km and dotted with the basis
 * unscaled (256 = 1.0). */
typedef struct {
    long bx, by, bz;                    /* 0: the camera's position less 2^24 (the range check's bias) */
    long f[3], r[3], u[3];              /* 12: front, right, up (256 = 1.0) */
    long f16;                           /* 48: the focal length x 16 */
    long cx16, cy16;                    /* 52: the projection's centre, x16 */
    long zmin, zmax;                    /* 60: the near plane (bolts are cut there) and the far cull, camera units */
    long rf;                            /* 68: the bolts' radius (camera units) x f16 */
    short rmax16;                       /* 72: the largest radius drawn (x16): a bolt nearer is drawn this size */
    short line16;                       /* 74: under this radius (x16) a bolt is a line */
    short xmin16, xmax16;               /* 76: the box a bolt must touch to be kept (x16) */
    short ymin16, ymax16;               /* 80 */
} LzCam;                                /* 84 bytes */

/* a line on screen (20 bytes): rows y0 .. y1-1; x' = x + 1/2 - 1/65536 in
 * 16.16, so a row's run is the pixels [x'_top >> 16, x'_bottom >> 16) */
typedef struct {
    short y0, y1;                       /* 0 */
    long ax, bx;                        /* 4: x' at the two ends, a the upper (on row y0), b on row y1-1 */
    long x1;                            /* 12: x' where the line leaves row y0 (its bottom edge) */
    long m;                             /* 16: x' per row; its sign is the line's way (a one-row line: 0 or -1) */
} LzLine;

/* a fat bolt on screen (12 bytes): its ends x16, a the upper (or the left of a level one), the radius x16 */
typedef struct {
    short ax, ay, bx, by, r, pad;
} LzFat;

/* one of a fat bolt's nested capsules, ready to draw (lz_fat_setup; 40 bytes, the asm reads this layout) */
typedef struct {
    short top, bot;                     /* 0: rows top .. bot-1 */
    short ax, ay, bx, by;               /* 4: the ends, x16 */
    short r2;                           /* 12: the radius x16, squared (at most LZ_RMAX16^2) */
    short la, lb, ra, rb;               /* 14: x16 rows where the left / right boundary leaves arc a for the body,
                                           and the body for arc b */
    short colour;                       /* 22 */
    long c0, m;                         /* 24: the middle line's x (16.16 px) at row top's centre, and per row */
    long hw;                            /* 32: the half width a row cuts through the body, 16.16 px */
    short r, pad;                       /* 36: the radius x16 */
} LzPass;

/* an engine's flame (lz_flame_setup; 32 bytes, the asm reads this layout).
 * Where a pixel is in it is two numbers linear on the screen, 16.16 at pixel
 * (0, 0) and per column and row: s along it (0 the nozzle, 65536 the tip)
 * and e across it (65536 the root's radius); kk the flame's length over that
 * radius, for the round root (s < 0).  Mode 1: end on, a disc (s down it and
 * e across, both in radii).  seed: this frame's noise. */
typedef struct {
    long s00, sx, sy;                   /* 0 */
    long e00, ex, ey;                   /* 12 */
    long kk;                            /* 24 */
    short mode, seed;                   /* 28 */
} LzFlame;

/* a flame's look (1,280 bytes, paragraph aligned for the asm, which reads
 * this layout): the profile (s in sixteenths down, r in sixteenths of the
 * radius across: intensity 1..255, 0 outside: tools/flamelut.py), the
 * stipple (a pixel of intensity t is ramp_a[t] where x + y is odd, ramp_b[t]
 * where even), and the noise: r's jitter in sixteenths and the intensity's */
typedef struct {
    unsigned char lut[256];             /* 0 */
    unsigned char ramp_a[256], ramp_b[256];   /* 256, 512 */
    signed char nr[256], nv[256];       /* 768, 1024 */
} LzFlameLook;

/* --- spherical projectiles, "balls" (balls.c; their sprites tools/balls.py's BALLS.DAT) --- */

/* a ball in the world (20 bytes; the asm reads this layout) */
typedef struct {
    long x, y, z;                       /* 0: its middle, positions */
    short vx, vy, vz;                   /* 12: its velocity, positions a frame (256 a km: 128 km a frame at most) */
    unsigned char kind, phase;          /* 18: what it is (BALLS.DAT's), its animation's start */
} LzBall;

/* a kind of ball as the camera needs it (16 bytes; the asm reads this layout) */
typedef struct {
    long rf;                            /* 0: its radius (camera units) x f16, as LzCam.rf: rf / Z = radius x16 */
    long z1;                            /* 4: rf / 12: past this Z it is one pixel (the asm's shortcut) */
    unsigned char set, frames, mask;    /* 8: its first sprite set, its animation's frames (a power of 2), less 1 */
    unsigned char pad[5];
} LzBallKind;

/* a ball on screen (12 bytes on DOS; the asm reads this layout) */
typedef struct {
    short x, y;                         /* 0: its sprite's top left */
    unsigned char d, set;               /* 4: its diameter (1 .. dmax), its sprite set (the kind's first + frame) */
    unsigned short pad;                 /* 6 */
    const unsigned char LZ_FAR *spr;    /* 8: its d x d pixels, 0 clear (place's for a small one; 0 for a big one) */
} LzBallOut;

/* the small sprites, every set's sizes 1 .. LZ_BALL_SMALL in a block (set-major, each set's in order of size):
 * place points a small ball at its own; a bigger one's sprite is the caller's to find */
#define LZ_BALL_SMALL 8
#define LZ_BALL_SMALL_SET 204                  /* a set's small sprites' bytes: 1 + 4 + ... + 64 */

/* the look */
typedef struct {
    unsigned char line, body, core, pad;   /* palette indices: far lines; the fat bolt's body, its middle */
    short body16;                       /* the core's radius x16 from which it is a capsule, not a line */
} LzStyle;

#define LZ_RMAX16 128                   /* the widest capsule: 8 px of radius (x16; the engines' flames' footprints) */
#define LZ_SQRT_N (LZ_RMAX16 * LZ_RMAX16 / 4 + 1)   /* lz_sqrt16[k] = sqrt(4 k + 1.5) rounded, for k = (r^2 - d^2) / 4
                                                   (x16), up to r = LZ_RMAX16: 4,097 bytes */
extern unsigned char LZ_FAR lz_sqrt16[LZ_SQRT_N];
void lz_init(void);                     /* the roots' table */

/* the camera: position, basis (256 = 1.0), the projection, the bolts' size and
 * where they may show (rows y0 .. y1-1 of the screen, all its columns) */
void lz_camera(LzCam LZ_FAR *c, long px, long py, long pz, const long *front, const long *right, const long *up,
               int cx, int cy, int focal, long radius16, long zmin_km, long zmax_km, int rmax16, int line16,
               int y0, int y1);

/* one bolt (or anything of its shape: a missile's body, its nose, its flame) through the camera, as lz_place
 * puts each: 0 when it is not drawn (past the far cull, wholly before the near plane, off the screen); else its
 * ends on screen x16, the head's (b + h) and the tail's (an end before the near plane brought to it along the
 * bolt), their depths (camera units) and the radius x16 (at the middle's depth, at most rmax16) */
typedef struct {
    long hx, hy, tx, ty;
    long zh, zt;
    long r16;
} LzEnds;
int lz_project(const LzBolt LZ_FAR *b, const LzCam LZ_FAR *c, LzEnds LZ_FAR *e);

/* the bolts through the camera: lines and fat ones out, the counts back.
 * The reference of rv_bolts_place (lowlevel.asm), which does the same to the bit. */
void lz_place(const LzBolt LZ_FAR *b, int n, const LzCam LZ_FAR *c, LzLine LZ_FAR *lines, int *nlines, int maxlines,
              LzFat LZ_FAR *fats, int *nfats, int maxfats);

/* a fat bolt's passes (the body, and the core if it is thick enough): returns
 * how many; a core too thin for a capsule comes back as a line (*core_line 1) */
int lz_fat_setup(const LzFat LZ_FAR *f, const LzStyle *st, LzPass LZ_FAR *pass, LzLine LZ_FAR *core, int *core_line);

/* one capsule of f's ends and radius (at most LZ_RMAX16), in one colour */
void lz_capsule_setup(const LzFat LZ_FAR *f, int colour, LzPass LZ_FAR *p);

/* an engine's flame: its nozzle (ax, ay) and its tail's way (tx, ty), x16 on the screen, its root's radius r16,
 * this frame's noise seed: *f, and *fp the capsule round it (a footprint for lz_capsule_setup: its rows and spans
 * are the pixels the flame's shading visits) */
void lz_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame LZ_FAR *f, LzFat LZ_FAR *fp);

/* the balls through the camera (its near plane, far cull and screen box): each one on screen out, its diameter
 * the radius on screen x 2 rounded (at least 1 pixel, at most dmax), its frame (anim + phase) & mask.  The
 * small ones (LZ_BALL_SMALL across at most) go to out's front, their sprites in `small`; the big ones to its
 * back (out[maxout-1], out[maxout-2] ...), their sprites 0.  Returns how many small, and big x 65536
 * (rv_balls_place's reference) */
long lz_balls_place(const LzBall LZ_FAR *b, int n, const LzCam LZ_FAR *c, const LzBallKind LZ_FAR *k, int dmax,
                    unsigned anim, LzBallOut LZ_FAR *out, int maxout, const unsigned char LZ_FAR *small);

/* every bolt flies k positions for each sixteenth of a km of its half axis (rv_bolts_move's reference) */
void lz_move(LzBolt LZ_FAR *b, int n, long k);


/* a line through two points x16 (a fat bolt's core) */
void lz_line_of(int ax, int ay, int bx, int by, LzLine LZ_FAR *out);

/* --- the draws: into the band's rows (row y at band + (y - band_y0) * 640),
 * rows y0 .. y1-1, each clipped to its row's extent ext[2k], ext[2k+1]
 * (k = y - ext_y0, rows ext_y0 .. ext_y0+ext_rows-1; NULL: the whole width) */
void lz_lines_draw(const LzLine LZ_FAR *l, int n, unsigned char colour, unsigned char LZ_FAR *band, int band_y0,
                   int y0, int y1, const short LZ_FAR *ext, int ext_y0, int ext_rows);
void lz_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext,
                  int ext_y0, int ext_rows);
/* the balls' sprites into the band's rows y0 .. y1-1, each row clipped to its extent, 0 pixels clear */
void lz_balls_draw(const LzBallOut LZ_FAR *o, int n, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                   const short LZ_FAR *ext, int ext_y0, int ext_rows);

/* a flame through its footprint p's rows and spans, each pixel shaded (its place in the flame, the profile, the
 * noise, the stipple) */
void lz_flame_draw(const LzPass LZ_FAR *p, const LzFlame LZ_FAR *f, const LzFlameLook LZ_FAR *lk, unsigned char LZ_FAR *band,
                   int band_y0, int y0, int y1, const short LZ_FAR *ext, int ext_y0, int ext_rows);

#ifdef __WATCOMC__
/* lasers.asm: the same, fast */
int __far __cdecl rv_bolts_place(const LzBolt __far *b, int n, const LzCam __far *cam, LzLine __far *lines,
                                 int maxlines, LzFat __far *fats, int maxfats, int __far *nfats);
void __far __cdecl rv_lines_draw(const LzLine __far *l, int n, int colour, unsigned char __far *band, int band_y0,
                                 int y0, int y1, const short __far *ext, int ext_y0);
void __far __cdecl rv_bolts_move(LzBolt __far *b, int n, long k);
void __far __cdecl rv_pass_draw(const LzPass __far *p, unsigned char __far *band, int band_y0, int y0, int y1,
                                const short __far *ext, int ext_y0, const unsigned char __far *roots);
int __far __cdecl rv_fat_setup(const LzFat __far *f, const LzStyle __far *st, LzPass __far *pass, LzLine __far *core,
                               int __far *core_line);
void __far __cdecl rv_capsule_setup(const LzFat __far *f, int colour, LzPass __far *pass);
void __far __cdecl rv_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame __far *f, LzFat __far *fp);
long __far __cdecl rv_balls_place(const LzBall __far *b, int n, const LzCam __far *cam, const LzBallKind __far *k,
                                  int dmax, unsigned anim, LzBallOut __far *out, int maxout,
                                  const unsigned char __far *small);
void __far __cdecl rv_balls_draw(const LzBallOut __far *o, int n, unsigned char __far *band, int band_y0, int y0,
                                 int y1, const short __far *ext, int ext_y0);
void __far __cdecl rv_flame_draw(const LzPass __far *p, unsigned char __far *band, int band_y0, int y0, int y1,
                                 const short __far *ext, int ext_y0, const unsigned char __far *roots,
                                 const LzFlame __far *f, const LzFlameLook __far *look);
#endif

#endif
