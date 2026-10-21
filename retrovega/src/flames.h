/* flames.h: the ships' engine flames (flames.c).
 *
 * Each engine of a ship type (RvShipType.engine: data/engines.csv, from
 * Vega Strike's exhaust points, in the frame its sprites are drawn in) gets a
 * flame out of its tail: three capsules from the nozzle (lasers.c's, as the
 * near laser bolts are drawn), a thin dark blue-violet tail the whole length,
 * a wide blue root over its first part and a short white core, so it tapers
 * from a white root to a dark tip as WC's flames do; end on, a disc.  The
 * nozzle and the flame's way are taken from the ship's sprite as drawn (its
 * view, roll and size: lightfield's lf_view_rot and lf_sprite_point), so the
 * flames stay on the engines the picture shows.  A flame pointing away from
 * the camera is drawn before its ship (the hull hides its root), one pointing
 * toward it after.  The afterburner's flames are twice as long, their tail
 * violet, their white core longer. */
#ifndef FLAMES_H
#define FLAMES_H

#include "rv.h"
#include "rvdata.h"
#include "lightfield.h"

#define FLAME_PASSES 96                 /* capsules a frame: the nearest ships' flames first */

typedef struct {
    unsigned char first, n;             /* its capsules in the frame's list */
    unsigned char front, pad;           /* 1: pointing toward the camera, drawn after the ship */
} FlameSet;

extern int g_flame_flicker;             /* RV.EXE /flicker: every other frame a little shorter */

void flames_init(void);                 /* at a flight's start, its palette installed: the colours, the tables */
void flames_frame(void);                /* a new frame: no flames yet */

/* a ship's flames as its sprite is drawn: its type, where lf_place put it
 * with the lattice (nb, nh) its draw uses, its size (256 full), afterburner
 * on or not; *out the capsules made (none when it has no engines, is too
 * small, or the frame's room is used up) */
void flames_add(const RvShipType __far *t, const LfPlace *p, int nb, int nh, int scale256, int burn, FlameSet *out);

/* a ship's flames into this band, rows y0 .. y1-1, clipped to the extents as the ships are */
void flames_draw(const FlameSet *f, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

#endif
