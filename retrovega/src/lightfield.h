/* lightfield.h: a ship's sprite as the camera sees it (lightfield.c).
 *
 * A ship's lightfield is its set of pre-rendered views (docs/spec.md
 * section 8: 62 directions, each a run-length sprite).  This module picks
 * the view for a camera, says where on screen the ship is and how far round
 * its sprite is turned, and holds the pixel kernels that scale and rotate a
 * view.  It is plain C with no DOS in it: tests/host/lf_test.c builds it
 * with gcc and checks it against reference results; ships.c wraps it for
 * the game (docs/engine.md, "Flight"). */
#ifndef LIGHTFIELD_H
#define LIGHTFIELD_H

#ifdef __WATCOMC__
#define LF_FAR __far
#else
#define LF_FAR
#endif

typedef struct { long x, y, z; } LfVec;         /* 256 = 1.0 for bases, 256 = 1 km for positions (as the slots keep them) */
typedef struct { LfVec front, right, up; } LfBasis;

typedef struct {
    int on_screen;                      /* 0: behind the camera, or far off the screen */
    int sx, sy;                         /* the ship's origin on screen */
    int view;                           /* 0..61: the lightfield's view for this direction */
    int roll;                           /* 0..1023: how far the view is turned on screen, clockwise */
    long depth;                         /* camera-space distance along the view axis, position units */
} LfPlace;

/* where a ship shows and which view: rel = ship - camera (positions), the
 * camera's and the ship's bases; (cx, cy) is the view's centre on screen and
 * focal the projection's focal length in pixels */
void lf_place(const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int cx, int cy, int focal, LfPlace *out);

/* the view seen from (vx, vy, vz) in the renderer's frame of the ship (+x
 * its left, +y up, +z the nose; any scale): 0 top, 1..35 the bands, 36
 * bottom, 37..61 the headings past 180 */
int lf_view_of(long vx, long vy, long vz);

/* integer trigonometry: angles 1024 a turn, 1.0 = 16384 */
long lf_sin(int a);
long lf_cos(int a);
int lf_atan2(long y, long x);           /* 0..1023 */
unsigned long lf_sqrt(unsigned long v);

/* --- the kernels, on dense 8-bit rows (0 = clear) --- */

/* the source column of every destination column (nearest neighbour) */
void lf_colmap(int src_w, int dst_w, unsigned short LF_FAR *map);

/* destination columns xa .. xb-1 of a scaled row: out[x] = line[map[x]]
 * where that is not clear (so a row can be drawn straight over a picture) */
void lf_scale_row(const unsigned char LF_FAR *line, const unsigned short LF_FAR *map, int xa, int xb,
                  unsigned char LF_FAR *out);

/* rotation (and scale) by inverse mapping, 16.16 fixed point */
typedef struct {
    int ow, oh;                         /* the turned image's size */
    int ocx, ocy;                       /* where the source origin lands in it */
    long u00, v00;                      /* the source point under output pixel (0, 0) */
    long du, dv;                        /* its change per output column */
    long dru, drv;                      /* ... and per output row */
} LfRot;

/* a w x h source with its origin at (cx, cy), turned clockwise by roll
 * (1024 a turn) and scaled by scale256 / 256 */
void lf_rotate_setup(int w, int h, int cx, int cy, int roll, int scale256, LfRot *r);

/* output row y, columns xa .. xb-1, of the turned image, from the dense
 * source (pitch w); clear source pixels and points outside it leave out[] alone */
void lf_rotate_row(const unsigned char LF_FAR *dense, int w, int h, const LfRot *r, int y, int xa, int xb,
                   unsigned char LF_FAR *out);

/* the columns xs .. xe-1 of output row y (within xa .. xb-1) whose source
 * point lies inside the w x h source: a kernel drawing only those needs no
 * bounds test.  Returns 0 when there are none. */
int lf_rotate_span(const LfRot *r, int w, int h, int y, int xa, int xb, int *xs, int *xe);

#endif
