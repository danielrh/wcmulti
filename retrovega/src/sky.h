/* sky.h: the far things on the sky (sky.c): nebulae, galaxies, planets, at
 * infinity, so each is one size wherever the camera is and only turns as it
 * rolls.  tools/sky.py draws each at every turn it may show at (SKY.DAT,
 * read into XMS when the game starts); a frame puts each through the camera
 * (lightfield's lf_sky: where its direction falls on the screen, how its up
 * is turned there) and draws the nearest turn with the ships' blit (half of
 * the turns are the other half upside down and mirrored; a thing that is its
 * own mirror image keeps a quarter).  Nebulae and galaxies are drawn behind
 * the stars, planets over them. */
#ifndef SKY_H
#define SKY_H

#include "lightfield.h"

#define SKY_NEBULA 0                    /* SKY.DAT's things */
#define SKY_GALAXY 1
#define SKY_GIANT 2                     /* a banded gas giant, lit from the upper left */
#define SKY_RINGED 3                    /* a ringed planet, lit from above */
#define SKY_MAX 8                       /* things on a flight's sky */

int sky_reserve(void);                  /* when the game starts, before RV.DAT (resident XMS): SKY.DAT in */
int sky_things(void);                   /* the kinds SKY.DAT has */
void sky_rows(int rows);                /* 0: each thing drawn its own way (SKY.DAT's); 1 every row, 2 doubled (half the
                                           rows, each twice), 3 interlaced (every other screen row: the low-cost look,
                                           a gas's) */
void sky_clear(void);                   /* a flight's start: an empty sky */
/* a thing on the sky: its kind, the direction it is in and the way its up points (world axes; 4096 = 1.0 keeps
 * it steady to the pixel); 0 when the sky is full or there is no such thing */
int sky_add(int thing, const LfVec *dir, const LfVec *up);

void sky_place(const LfBasis *cam, int cx, int cy, int focal);   /* once a frame */
/* per band: layer 0 (behind the stars) before them, 1 (over them) after */
void sky_draw(int layer, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);
void sky_ticks(unsigned long out[2]);   /* 240 Hz ticks placing, drawing */

#endif
