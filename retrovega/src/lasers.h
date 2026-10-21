/* lasers.h: laser bolts, from the world to the screen (lasers.c; the fast
 * paths are lowlevel.asm's rv_bolts_*).
 *
 * A bolt is a capsule: a segment along its flight with a radius round it, so
 * it shows from any angle (end on it is a disc).  Each frame the bolts go
 * through the camera once (lz_place): a bolt whose radius on screen is under
 * a pixel or so becomes a LINE, one pixel wide, drawn as a run of pixels per
 * row (never less than a pixel a row: a bolt is always visible); a bigger one
 * is FAT, drawn red with a white middle painted over it in the way chosen
 * when RV.EXE is built (LZ_NEAR below: only that one is compiled).  The draws
 * then run per band (the band buffer's rows), clipped to the cockpit's
 * opening.
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

/* the fat bolts' drawing, chosen at build time (build.sh's NEAR=capsule, octagon or rect; lasers.asm the same) */
#define LZ_NEAR_CAPSULE 1               /* capsules: a red body, a white core (lz_fat_setup; the roots' table) */
#define LZ_NEAR_OCTAGON 2               /* an octagon swept along the bolt: two polygons, round ends */
#define LZ_NEAR_RECT 3                  /* the rectangle square to its flight, its ends pushed out: two polygons */
#ifndef LZ_NEAR
#define LZ_NEAR LZ_NEAR_CAPSULE
#endif
#if LZ_NEAR == LZ_NEAR_CAPSULE
#define LZ_NEAR_NAME "capsules"
#elif LZ_NEAR == LZ_NEAR_OCTAGON
#define LZ_NEAR_NAME "swept octagons"
#else
#define LZ_NEAR_NAME "rectangles"
#endif
#define LZ_POLYGONS (LZ_NEAR != LZ_NEAR_CAPSULE)

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

/* a convex polygon on screen (8 + 4 LZ_POLY_MAX bytes; the asm reads this
 * layout): n vertices x16 in order round it, rows top .. bot-1 (those whose
 * centres it covers), one colour.  A fat bolt (lz_slab_setup) is two of
 * these, a red outline and a white middle painted over it, or thinner, lines. */
#if LZ_NEAR == LZ_NEAR_OCTAGON
#define LZ_POLY_MAX 10
#else
#define LZ_POLY_MAX 4
#endif
typedef struct {
    short n, colour;                    /* 0 */
    short top, bot;                     /* 4 */
    short x[LZ_POLY_MAX], y[LZ_POLY_MAX];   /* 8, 8 + 2 LZ_POLY_MAX */
} LzPoly;

/* the look */
typedef struct {
    unsigned char line, body, core, pad;   /* palette indices: far lines; the fat bolt's body, its middle */
    short body16;                       /* (capsules) the middle's radius x16 from which it is a capsule, not a line */
} LzStyle;

#define LZ_RMAX16 64                    /* the widest a fat bolt is drawn: 4 px of radius (x16) */
#if LZ_NEAR == LZ_NEAR_CAPSULE
#define LZ_SQRT_N (LZ_RMAX16 * LZ_RMAX16 / 4 + 1)   /* lz_sqrt16[k] = sqrt(4 k + 1.5) rounded, for k = (r^2 - d^2) / 4
                                                   (x16), up to r = LZ_RMAX16: 1,025 bytes */
extern unsigned char LZ_FAR lz_sqrt16[LZ_SQRT_N];
#endif
void lz_init(void);                     /* the capsules' table (nothing for the polygons) */

/* the camera: position, basis (256 = 1.0), the projection, the bolts' size and
 * where they may show (rows y0 .. y1-1 of the screen, all its columns) */
void lz_camera(LzCam LZ_FAR *c, long px, long py, long pz, const long *front, const long *right, const long *up,
               int cx, int cy, int focal, long radius16, long zmin_km, long zmax_km, int rmax16, int line16,
               int y0, int y1);

/* the bolts through the camera: lines and fat ones out, the counts back.
 * The reference of rv_bolts_place (lowlevel.asm), which does the same to the bit. */
void lz_place(const LzBolt LZ_FAR *b, int n, const LzCam LZ_FAR *c, LzLine LZ_FAR *lines, int *nlines, int maxlines,
              LzFat LZ_FAR *fats, int *nfats, int maxfats);

#if LZ_NEAR == LZ_NEAR_CAPSULE
/* a fat bolt's passes (the body, and the core if it is thick enough): returns
 * how many; a core too thin for a capsule comes back as a line (*core_line 1) */
int lz_fat_setup(const LzFat LZ_FAR *f, const LzStyle *st, LzPass LZ_FAR *pass, LzLine LZ_FAR *core, int *core_line);
#endif

/* every bolt flies k positions for each sixteenth of a km of its half axis (rv_bolts_move's reference) */
void lz_move(LzBolt LZ_FAR *b, int n, long k);

#if LZ_POLYGONS
/* a fat bolt as polygons: wider than 3 px, the red outline and the white
 * middle (half as wide) to paint over it, returns 2; narrower, for the
 * rectangle three lines side by side (red, white, red), returns 0 with
 * *nlines 3, for the octagon the outline and a white core line, returns 1
 * with *nlines 1 (a single line is white, as the middle of three) */
int lz_slab_setup(const LzFat LZ_FAR *f, const LzStyle *st, LzPoly LZ_FAR *poly, LzLine LZ_FAR *lines, int *nlines);
#endif

/* a line through two points x16 (a fat bolt's core) */
void lz_line_of(int ax, int ay, int bx, int by, LzLine LZ_FAR *out);

/* --- the draws: into the band's rows (row y at band + (y - band_y0) * 640),
 * rows y0 .. y1-1, each clipped to its row's extent ext[2k], ext[2k+1]
 * (k = y - ext_y0, rows ext_y0 .. ext_y0+ext_rows-1; NULL: the whole width) */
void lz_lines_draw(const LzLine LZ_FAR *l, int n, unsigned char colour, unsigned char LZ_FAR *band, int band_y0,
                   int y0, int y1, const short LZ_FAR *ext, int ext_y0, int ext_rows);
#if LZ_POLYGONS
void lz_poly_draw(const LzPoly LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0, int ext_rows);
#else
void lz_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext,
                  int ext_y0, int ext_rows);
#endif

#ifdef __WATCOMC__
/* lasers.asm: the same, fast */
int __far __cdecl rv_bolts_place(const LzBolt __far *b, int n, const LzCam __far *cam, LzLine __far *lines,
                                 int maxlines, LzFat __far *fats, int maxfats, int __far *nfats);
void __far __cdecl rv_lines_draw(const LzLine __far *l, int n, int colour, unsigned char __far *band, int band_y0,
                                 int y0, int y1, const short __far *ext, int ext_y0);
void __far __cdecl rv_bolts_move(LzBolt __far *b, int n, long k);
#if LZ_POLYGONS
int __far __cdecl rv_slab_setup(const LzFat __far *f, const LzStyle __far *st, LzPoly __far *poly, LzLine __far *lines,
                                int __far *nlines);
void __far __cdecl rv_poly_draw(const LzPoly __far *p, unsigned char __far *band, int band_y0, int y0, int y1,
                                const short __far *ext, int ext_y0);
#else
void __far __cdecl rv_pass_draw(const LzPass __far *p, unsigned char __far *band, int band_y0, int y0, int y1,
                                const short __far *ext, int ext_y0, const unsigned char __far *roots);
int __far __cdecl rv_fat_setup(const LzFat __far *f, const LzStyle __far *st, LzPass __far *pass, LzLine __far *core,
                               int __far *core_line);
#endif
#endif

#endif
