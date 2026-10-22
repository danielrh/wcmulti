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
 * mirrored and flipped; in steps fine enough that a tail ends within half a
 * pixel of its way) in two frames of noise, kept in XMS as run-length
 * sprites.  A frame draws the nearest baked flame, each row from one frame
 * of noise or the other at random (ships_blit_mix).  End on, a disc.  The
 * nozzle and the flame's way are taken from the ship's sprite as drawn (the
 * view and turn the draw picks: ships_drawn; its size; lightfield's
 * lf_view_rot and lf_sprite_point), so the flames stay on the engines the
 * picture shows and point exactly along it (the user's rule).  A
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


void flames_reserve(void);              /* when the game starts, before RV.DAT takes its XMS: the flames' room (the
                                           resident level: XMS levels nest) */
void flames_bake(void);                 /* then, RV.DAT open: every flame baked there, once (the flight palette from
                                           a cockpit's) */
void flames_init(void);                 /* at a flight's start: the draw's tables */
void flames_frame(void);                /* a new frame: no flames yet */

/* a ship's flames as its sprite is drawn: its type, its place with the view
 * and turn its picture has (ships_drawn) in the lattice (nb, nh) of that
 * picture, its size (256 full), afterburner
 * on or not, its slot (its noise's own); *out the flames made (none when it
 * has no engines, is too small, or the frame's room is used up) */
void flames_add(const RvShipType __far *t, const LfPlace *p, int nb, int nh, int scale256, int burn, int slot,
                FlameSet __far *out);

/* a flame of its own (a missile's): its nozzle on screen and its tail's way and length there (x16), its root's
 * radius (x16), the afterburner's look or not, a number of its own (its noise's); *out as flames_add's (its
 * `front` the caller's to set) */
void flames_add_at(long x16, long y16, long tx16, long ty16, int r16, int burn, unsigned id, FlameSet __far *out);

/* the frame's flame k (a FlameSet's first + i): the way its baked flame's tail points on screen (16384ths of a turn
 * from the right, clockwise: y down) and its length (x16); 0 when it is end on (no way shows).  And a way as a
 * vector (16384 = 1.0).  For the self-test (/flametest) */
int flames_way(int k, int *way, int *len16);
void flames_way_vec(int way, long *c, long *s);

/* a ship's flames into this band, rows y0 .. y1-1, clipped to the extents as the ships are */
void flames_draw(const FlameSet __far *f, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

#endif
