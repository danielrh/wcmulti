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

#include "rvtypes.h"

#ifdef __WATCOMC__
#define LF_FAR __far
#else
#define LF_FAR
#endif

typedef struct { int32_t x, y, z; } LfVec;         /* 256 = 1.0 for bases, 256 = 1 km for positions (as the slots keep them) */
typedef struct { LfVec front, right, up; } LfBasis;

typedef struct {
    int16_t on_screen;                  /* 0: behind the camera, or far off the screen */
    int16_t sx, sy;                     /* the ship's origin on screen */
    int16_t view;                       /* 0..61: the lightfield's view for this direction */
    int16_t roll;                       /* 0..1023: how far the view is turned on screen, clockwise */
    int32_t depth;                      /* camera-space distance along the view axis, position units */
} LfPlace;

/* A lightfield's lattice: nb bands of elevation, every 180/(nb+1) degrees
 * from the top down, nh headings 0..180 every 180/(nh-1) degrees.  View 0
 * is the top, 1 + b*nh + j band b heading j, 1 + nb*nh the bottom, and the
 * headings past 180 (180 + k*step, k = 1..nh-2) follow at 2 + nb*nh + b*(nh-2)
 * + (k-1): stored for an asymmetric ship, the mirror images of the others'
 * for a symmetric one.  WC1's 37 views are (5, 7); the flight's denser
 * lattice for the nearest ships is (11, 13): 145 views, 266 asymmetric. */
#define LF_LATTICE_VIEWS(nb, nh) (2 + (nb) * (nh) + (nb) * ((nh) - 2))

/* where a ship shows and which view of lattice (nb, nh): rel = ship - camera
 * (positions), the camera's and the ship's bases; (cx, cy) is the view's
 * centre on screen and focal the projection's focal length in pixels */
void lf_place(const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int16_t cx, int16_t cy, int16_t focal,
              int16_t nb, int16_t nh, LfPlace *out);

/* A direction of the sky, any length in the world frame, through the camera:
 * where it shows (sx, sy and on_screen as lf_place puts a ship; view unused,
 * depth the farthest) and the turn on screen, clockwise, of a sprite painted
 * there whose up in the world is `up` ((0, 256, 0), the world's own, for
 * most): so it turns with the sky as the player rolls.  For suns, nebulae,
 * planets, and the stars themselves. */
void lf_sky(const LfVec *dir, const LfVec *up, const LfBasis *cam, int16_t cx, int16_t cy, int16_t focal, LfPlace *out);

/* the view of lattice (nb, nh) seen from (vx, vy, vz) in the renderer's frame
 * of the ship (+x its left, +y up, +z the nose; any scale) */
int16_t lf_view_of(int32_t vx, int32_t vy, int32_t vz, int16_t nb, int16_t nh);

/* the opposite heading's view (the one whose mirror image a view is): a
 * view at heading 0 or 180, the top and the bottom are their own */
int16_t lf_pair_of(int16_t v, int16_t nb, int16_t nh);

/* a view's turn: rows the picture's x (right), its y (up) and the way to the
 * viewer, in the renderer's frame of the ship (shipviews.view_matrix); 16384
 * = 1.0 */
typedef struct { int32_t m[9]; } LfViewRot;
void lf_view_rot(int16_t v, int16_t nb, int16_t nh, LfViewRot *r);

/* a point of the ship (the renderer's frame, any scale up to 2^16) as its
 * view's sprite shows it turned by roll (clockwise, 1024 a turn, as lf_place
 * gives): its offset from the ship's origin on the screen (x right, y down)
 * and its height toward the viewer, the same scale.  Where the engines' flames
 * start and which way they point. */
void lf_sprite_point(const LfViewRot *r, int16_t roll, int32_t x, int32_t y, int32_t z, int32_t *sx, int32_t *sy,
                     int32_t *sz);

/* integer trigonometry: angles 1024 a turn, 1.0 = 16384 */
int32_t lf_sin(int16_t a);
int32_t lf_cos(int16_t a);
int16_t lf_atan2(int32_t y, int32_t x);   /* 0..1023 */
uint32_t lf_sqrt(uint32_t v);

/* --- the kernels, on dense 8-bit rows (0 = clear) --- */

/* the source column of every destination column (nearest neighbour) */
void lf_colmap(int16_t src_w, int16_t dst_w, uint16_t LF_FAR *map);

/* destination columns xa .. xb-1 of a scaled row: out[x] = line[map[x]]
 * where that is not clear (so a row can be drawn straight over a picture) */
void lf_scale_row(const uint8_t LF_FAR *line, const uint16_t LF_FAR *map, int16_t xa, int16_t xb,
                  uint8_t LF_FAR *out);

/* rotation (and scale) by inverse mapping, 16.16 fixed point */
typedef struct {
    int16_t ow, oh;                     /* the turned image's size */
    int16_t ocx, ocy;                   /* where the source origin lands in it */
    int32_t u00, v00;                   /* the source point under output pixel (0, 0) */
    int32_t du, dv;                     /* its change per output column */
    int32_t dru, drv;                   /* ... and per output row */
} LfRot;

/* a w x h source with its origin at (cx, cy), turned clockwise by roll
 * (1024 a turn) and scaled by scale256 / 256 */
void lf_rotate_setup(int16_t w, int16_t h, int16_t cx, int16_t cy, int16_t roll, int16_t scale256, LfRot *r);

/* output row y, columns xa .. xb-1, of the turned image, from the dense
 * source (pitch w); clear source pixels and points outside it leave out[] alone */
void lf_rotate_row(const uint8_t LF_FAR *dense, int16_t w, int16_t h, const LfRot *r, int16_t y, int16_t xa, int16_t xb,
                   uint8_t LF_FAR *out);

/* the columns xs .. xe-1 of output row y (within xa .. xb-1) whose source
 * point lies inside the w x h source: a kernel drawing only those needs no
 * bounds test.  Returns 0 when there are none. */
int16_t lf_rotate_span(const LfRot *r, int16_t w, int16_t h, int16_t y, int16_t xa, int16_t xb, int16_t *xs,
                       int16_t *xe);

#endif
