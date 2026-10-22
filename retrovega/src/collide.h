/* collide.h: ship-to-ship collisions (collide.c).
 *
 * Coarse, then fine.  Coarse: the two ships' bounding spheres (a ship's half
 * box, the size its sprites are framed by, bounds its mesh).  Fine (the
 * user's, 2026-10-21): the smaller ship's hit box (the mesh's extents,
 * axis-aligned in its own frame) against the bigger one's vitals: its core
 * (a conservative sphere: the biggest about its middle that is inside its
 * sprite at every view, tools/hitboxes.py),
 * its engines, its weapon mounts, its nose, tail and wingtips (RvShipType's
 * hit, engine and vital), each taken into the box's frame (one 3 x 3
 * product) and compared on its axes (the core: a sphere against the box,
 * the box grown by its radius, rounded: the user's "squircle").  Only when
 * that finds nothing, the bigger one's box against the smaller's vitals (a
 * fighter in the middle of a capital ship's hull, between its vitals).  A
 * hit gives the least move that frees the deepest vital: the vector that
 * rectifies the intersection and shoves the ships apart. */
#ifndef COLLIDE_H
#define COLLIDE_H

#include "lightfield.h"

/* a slot's ship: its half box in positions (256 a km): the flight draws a ship lod0_px across at full size, 2,500
 * km away at a focal length of 400, so its half box is lod0_px x 3.125 km; 0 when there is no ship there */
long collide_half(int slot);

int collide_coarse(int a, int b);       /* their bounding spheres touch */

/* coarse, then fine: 0 when no vital is in the other's box; else *push (positions) is the least move of b that
 * frees the deepest (move a by -push instead, or each by half), *depth how deep it was */
int collide_ships(int a, int b, LfVec *push, long *depth);

/* a ship's hit box and vitals in the world, for drawing them: its box's eight corners, its vitals (the core
 * first, then the engines, then the rest): how many vitals (at most 1 + 4 + 8) */
int collide_shape(int slot, LfVec corner[8], LfVec *vital, int maxv);

#endif
