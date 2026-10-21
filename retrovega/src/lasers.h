/* lasers.h: laser bolts, from the world to the screen (lasers.c; the fast
 * paths are lowlevel.asm's rv_bolts_*).
 *
 * A bolt is a capsule: a segment along its flight with a radius round it, so
 * it shows from any angle (end on it is a disc).  Each frame the bolts go
 * through the camera once (lz_place): a bolt whose radius on screen is under
 * a pixel or so becomes a LINE, one pixel wide, drawn as a run of pixels per
 * row (never less than a pixel a row: a bolt is always visible); a bigger one
 * is FAT: up to three nested capsules painted over each other (a dark red
 * rim, a red body, a white core), each row one span.  The draws then run per
 * band (the band buffer's rows), clipped to the cockpit's opening.
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
    short r2;                           /* 12: the radius x16, squared (at most 128^2) */
    short la, lb, ra, rb;               /* 14: x16 rows where the left / right boundary leaves arc a for the body,
                                           and the body for arc b */
    short colour;                       /* 22 */
    long c0, m;                         /* 24: the middle line's x (16.16 px) at row top's centre, and per row */
    long hw;                            /* 32: the half width a row cuts through the body, 16.16 px */
    short r, pad;                       /* 36: the radius x16 */
} LzPass;

/* a convex polygon on screen (40 bytes; the asm reads this layout): n
 * vertices x16 in order round it, rows top .. bot-1 (those whose centres it
 * covers), one colour.  The simpler bolts (lz_slab_setup) are two of these,
 * a red outline and a white middle painted over it, or lines. */
#define LZ_POLY_MAX 8
typedef struct {
    short n, colour;                    /* 0 */
    short top, bot;                     /* 4 */
    short x[LZ_POLY_MAX], y[LZ_POLY_MAX];   /* 8, 24 */
} LzPoly;

/* the shapes of the near bolts */
#define LZ_SHAPE_CAPSULE 0              /* the nested capsules (lz_fat_setup) */
#define LZ_SHAPE_AXIS 1                 /* ends level for a steep bolt, upright for a shallow one: no root */
#define LZ_SHAPE_RECT 2                 /* the rectangle square to its flight */
#define LZ_SHAPE_HEX 3                  /* the rectangle with pointed ends: an angular pill */

/* the look */
typedef struct {
    unsigned char line, rim, body, core;   /* palette indices: far lines; the fat bolt's rim, body, core */
    short body16, rim16;                /* radius x16 from which a fat bolt has a body round its core line,
                                           and a rim round its body (else fewer colours) */
} LzStyle;

#define LZ_SQRT_N 4097                  /* lz_sqrt16[k] = sqrt(4 k + 1.5) rounded, for k = (r^2 - d^2) / 4 (x16) */
extern unsigned char LZ_FAR lz_sqrt16[LZ_SQRT_N];
void lz_init(void);                     /* the table */

/* the camera: position, basis (256 = 1.0), the projection, the bolts' size and
 * where they may show (rows y0 .. y1-1 of the screen, all its columns) */
void lz_camera(LzCam LZ_FAR *c, long px, long py, long pz, const long *front, const long *right, const long *up,
               int cx, int cy, int focal, long radius16, long zmin_km, long zmax_km, int rmax16, int line16,
               int y0, int y1);

/* the bolts through the camera: lines and fat ones out, the counts back.
 * The reference of rv_bolts_place (lowlevel.asm), which does the same to the bit. */
void lz_place(const LzBolt LZ_FAR *b, int n, const LzCam LZ_FAR *c, LzLine LZ_FAR *lines, int *nlines, int maxlines,
              LzFat LZ_FAR *fats, int *nfats, int maxfats);

/* a fat bolt's passes (rim, body, core, as its size allows): returns how
 * many; a core too thin for a capsule comes back as a line (*core_line 1) */
int lz_fat_setup(const LzFat LZ_FAR *f, const LzStyle *st, LzPass LZ_FAR *pass, LzLine LZ_FAR *core, int *core_line);

/* every bolt flies k positions for each sixteenth of a km of its half axis (rv_bolts_move's reference) */
void lz_move(LzBolt LZ_FAR *b, int n, long k);

/* a fat bolt as polygons (LZ_SHAPE_AXIS, _RECT, _HEX): wider than 3 px, the
 * red outline and the white middle (half as wide) to paint over it, returns 2;
 * narrower, three lines side by side (red, white, red) in lines[], returns 0
 * with *nlines 3 */
int lz_slab_setup(const LzFat LZ_FAR *f, int shape, const LzStyle *st, LzPoly LZ_FAR *poly, LzLine LZ_FAR *lines,
                  int *nlines);

/* a line through two points x16 (a fat bolt's core) */
void lz_line_of(int ax, int ay, int bx, int by, LzLine LZ_FAR *out);

/* --- the draws: into the band's rows (row y at band + (y - band_y0) * 640),
 * rows y0 .. y1-1, each clipped to its row's extent ext[2k], ext[2k+1]
 * (k = y - ext_y0, rows ext_y0 .. ext_y0+ext_rows-1; NULL: the whole width) */
void lz_lines_draw(const LzLine LZ_FAR *l, int n, unsigned char colour, unsigned char LZ_FAR *band, int band_y0,
                   int y0, int y1, const short LZ_FAR *ext, int ext_y0, int ext_rows);
void lz_poly_draw(const LzPoly LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0, int ext_rows);
void lz_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext,
                  int ext_y0, int ext_rows);

#ifdef __WATCOMC__
/* lasers.asm: the same, fast */
int __far __cdecl rv_bolts_place(const LzBolt __far *b, int n, const LzCam __far *cam, LzLine __far *lines,
                                 int maxlines, LzFat __far *fats, int maxfats, int __far *nfats);
void __far __cdecl rv_lines_draw(const LzLine __far *l, int n, int colour, unsigned char __far *band, int band_y0,
                                 int y0, int y1, const short __far *ext, int ext_y0);
void __far __cdecl rv_pass_draw(const LzPass __far *p, unsigned char __far *band, int band_y0, int y0, int y1,
                                const short __far *ext, int ext_y0, const unsigned char __far *roots);
void __far __cdecl rv_bolts_move(LzBolt __far *b, int n, long k);
int __far __cdecl rv_slab_setup(const LzFat __far *f, int shape, const LzStyle __far *st, LzPoly __far *poly,
                                LzLine __far *lines, int __far *nlines);
void __far __cdecl rv_poly_draw(const LzPoly __far *p, unsigned char __far *band, int band_y0, int y0, int y1,
                                const short __far *ext, int ext_y0);
int __far __cdecl rv_fat_setup(const LzFat __far *f, const LzStyle __far *st, LzPass __far *pass, LzLine __far *core,
                               int __far *core_line);
#endif

#endif
