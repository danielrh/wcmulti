/* collide.h: collisions with ships (collide.c).
 *
 * A ship's hull is its three silhouettes (the user's pick, 2026-10-21): from
 * above, the side and the front, 64 x 64 bit masks over its hit box (the
 * mesh's extents, axis-aligned in its own frame; tools/hitboxes.py's
 * SHAPES.DAT); a point is in the hull when it is in the box and solid in all
 * three (a type without silhouettes: its box).  Ship to ship: coarse (the
 * two bounding spheres), then fine (the user's): the smaller ship's hull
 * against the bigger one's vitals (its core sphere: the biggest about its
 * middle inside its sprite at every view, to 95%; its engines, weapon
 * mounts, nose, tail and wingtips), each taken into the hull's frame by one
 * 3 x 3 product; only when that finds nothing, the bigger one's hull against
 * the smaller's vitals (a fighter in a capital ship, between its vitals).  A
 * point (a blaster shot), a segment (a bolt from where it was to where it
 * is) against one ship's hull.  A hit gives the least move that frees the
 * deepest point: the vector that rectifies the intersection and shoves the
 * ships apart. */
#ifndef COLLIDE_H
#define COLLIDE_H

#include "lightfield.h"

int16_t collide_reserve(void);          /* when the game starts (resident XMS): SHAPES.DAT in */
void collide_flight_end(void);          /* the silhouettes kept for the flight's types given back */

/* a slot's ship: its half box in positions (256 a km): the flight draws a ship lod0_px across at full size, 2,500
 * km away at a focal length of 400, so its half box is lod0_px x 3.125 km; 0 when there is no ship there */
int32_t collide_half(int16_t slot);

int16_t collide_coarse(int16_t a, int16_t b);   /* their bounding spheres touch */

/* coarse, then fine: 0 when no vital is in the other's box; else *push (positions) is the least move of b that
 * frees the deepest (move a by -push instead, or each by half), *depth how deep it was */
int16_t collide_ships(int16_t a, int16_t b, LfVec *push, int32_t *depth);

/* a world point in the ship in a slot: 1 when it is, and *push (if asked) the least move of the point out */
int16_t collide_point(int16_t slot, const LfVec *p, LfVec *push);

/* a segment (a bolt from where it was, a, to where it is, b) against the ship in a slot: 1 when it goes in, *hit
 * (if asked) the first point of it in the hull and *t1024 (if asked) how far along it that is, 1024ths of a to b.
 * Its ends within 32,768 km of the ship */
int16_t collide_segment(int16_t slot, const LfVec *a, const LfVec *b, LfVec *hit, int16_t *t1024);

/* a ship's hit box and vitals in the world, for drawing them: its box's eight corners, its vitals (the core
 * first, then the engines, then the rest): how many vitals (at most 1 + 4 + 8) */
int16_t collide_shape(int16_t slot, LfVec corner[8], LfVec *vital, int16_t maxv);

#endif
