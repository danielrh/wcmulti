/* ships.h: ship views for the flight (ships.c): a type's 62 views decoded from
 * SHIPnn.RV into XMS (the mission level) in gfx.c's run-length row format,
 * the mirrored headings made at load, and the blitter that draws one. */
#ifndef SHIPS_H
#define SHIPS_H

#include "rv.h"

#define SHIP_VIEWS 62
#define SHIP_SETS 8

typedef struct {
    unsigned short w, h;
    short ox, oy;                       /* the ship's origin in the view */
    unsigned long at;                   /* XMS offset of the row table (h + 1 dwords), rows after it */
} ShipView;

typedef struct {
    unsigned char type, nviews, asym, livery;
    unsigned short lod0;
    XmsBlock blk;
    ShipView view[SHIP_VIEWS];
} ShipSet;

int ships_load(unsigned type);          /* -> set index, or -1 (no file, no room) */
int ships_find(unsigned type);          /* -> loaded set index, or -1 */
const ShipSet __far *ships_set(int i);
void ships_unload(void);                /* forgets every set (the mission level is freed by the caller) */

/* the view of a ship seen from (vx, vy, vz) in the ship's own frame (right,
 * up, front; any scale): 0 top, 1..35 the bands, 36 bottom, 37..61 the
 * headings past 180 */
int ships_view_of(long vx, long vy, long vz);

/* draw view v with the ship's origin at (x, y): the rows y0..y1-1, each row
 * clipped to [xa, xb) and, when ext is given, to its row's extent
 * (ext[2 * (row - ext_y0)], ext[.. + 1]) for rows ext_y0 .. ext_y0+ext_rows-1 */
void ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
                const short __far *ext, int ext_y0, int ext_rows);

/* integer trigonometry (angles: 1024 a turn; 1.0 = 16384) */
long rv_sin(int a);
long rv_cos(int a);
int rv_atan2(long y, long x);           /* 0..1023 */
unsigned long rv_sqrt(unsigned long v);

#endif
