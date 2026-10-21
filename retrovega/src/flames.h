/* flames.h: the ships' engine flames (flames.c).
 *
 * Each engine of a ship type (RvShipType.engine: data/engines.csv, from
 * Vega Strike's exhaust points, in the frame its sprites are drawn in) gets a
 * flame out of its tail.  The flames are shaded when the flight starts
 * (lasers.c's lz_flame_draw, lasers.asm's rv_flame_draw, through a capsule's
 * rows and spans as the near laser bolts are drawn): a teardrop widest a
 * third of the way along, its intensity from a profile (tools/flamelut.py: a
 * white root, a lavender middle, a blue body, violet edges), jittered by
 * noise (ragged edges, a shimmering gradient), through a stipple of colour
 * pairs (the palette has no saturated violet-blue: alternate pixels mix two
 * it has); every size, foreshortening and way (0 .. 90 degrees, the rest
 * mirrored and flipped) in three frames of noise, kept in XMS as run-length
 * sprites.  A frame draws the nearest baked flame, its noise's frame picked
 * at random, as the ships' sprites are drawn (ships_blit_view).  End on, a
 * disc.  The nozzle and the flame's way are taken from the ship's sprite as
 * drawn (its view, roll and size: lightfield's lf_view_rot and
 * lf_sprite_point), so the flames stay on the engines the picture shows.  A
 * flame pointing away from the camera is drawn before its ship (the hull
 * hides its root), one pointing toward it after.  The
 * afterburner's flames are twice as long, whiter, their tail magenta. */
#ifndef FLAMES_H
#define FLAMES_H

#include "rv.h"
#include "rvdata.h"
#include "lightfield.h"

typedef struct {
    unsigned char first, n;             /* its flames in the frame's list */
    unsigned char front, pad;           /* 1: pointing toward the camera, drawn after the ship */
} FlameSet;


void flames_reserve(void);              /* at a flight's start, before the ships' sets take the XMS: the flames' room
                                           (the mission level) */
void flames_init(void);                 /* then, its palette installed: the looks, and every flame baked there */
void flames_frame(void);                /* a new frame: no flames yet */

/* a ship's flames as its sprite is drawn: its type, where lf_place put it
 * with the lattice (nb, nh) its draw uses, its size (256 full), afterburner
 * on or not, its slot (its noise's own); *out the flames made (none when it
 * has no engines, is too small, or the frame's room is used up) */
void flames_add(const RvShipType __far *t, const LfPlace *p, int nb, int nh, int scale256, int burn, int slot,
                FlameSet __far *out);

/* a ship's flames into this band, rows y0 .. y1-1, clipped to the extents as the ships are */
void flames_draw(const FlameSet __far *f, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

#endif
