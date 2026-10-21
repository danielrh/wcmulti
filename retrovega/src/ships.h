/* ships.h: ship views for the flight (ships.c): a type's 62 views decoded from
 * SHIPnn.RV into XMS (the mission level) in gfx.c's run-length row format,
 * the mirrored headings made at load, and the draw of one as the camera
 * sees it, through the lightfield module's placement and kernels. */
#ifndef SHIPS_H
#define SHIPS_H

#include "rv.h"
#include "lightfield.h"

#define SHIP_VIEWS 62
#define SHIP_SETS 8

typedef struct {
    unsigned short w, h;
    short ox, oy;                       /* the ship's origin in the view */
    XmsBlock blk;                       /* the XMS block holding it */
    unsigned long at;                   /* offset of the row table (h + 1 dwords) in it, rows after */
} ShipView;

#define SET_BLOCKS 48

/* one (size, roll) block of a pre-rendered set (SHIPnn.RVS, tools/rvsprites.c) */
typedef struct {
    unsigned char size, roll, loaded, pad;
    unsigned long at, length;           /* in the file */
    unsigned long index_at;             /* its view table in the set's index block */
    XmsBlock blk;                       /* its data, once loaded */
} SetBlock;

typedef struct {
    unsigned char type, nviews, asym, rvs;   /* rvs: a pre-rendered set; else the .RV views at full size only */
    unsigned char nsizes, nrolls, roll_sizes, pad;
    unsigned short lod0;
    unsigned short scale[16];           /* the size steps, 256 = full */
    XmsBlock blk;                       /* .RV: the views; .RVS: the index */
    int nblocks;
    SetBlock block[SET_BLOCKS];
    ShipView view[SHIP_VIEWS];          /* the full-size, unturned views (a .RVS set's block 0) */
} ShipSet;

int ships_load(unsigned type);          /* a type's set: SHIPnn.RVS (its index and its full-size block), else
                                           SHIPnn.RV decoded; -> set index, or -1 (no file, no room) */
unsigned long ships_load_blocks(unsigned long budget);   /* every open set's remaining blocks, ladder first then
                                           rolls by size, while they fit the budget (bytes): returns the bytes taken */
int ships_find(unsigned type);          /* -> loaded set index, or -1 */
const ShipSet __far *ships_set(int i);
void ships_unload(void);                /* forgets every set (the mission level is freed by the caller) */

/* the view the camera projects through: its centre on screen and its focal length */
void ships_set_camera(int cx, int cy, int focal);

/* THE draw: the ship of set `set` at rel = ship - camera (positions, 256 = 1
 * km), with the camera's and the ship's bases, scale256 / 256 of its full
 * size; into the run y0..y1-1, each row clipped to [xa, xb) and, when ext
 * is given, to its row's extent (ext[2 * (row - ext_y0)], ext[.. + 1]) for
 * rows ext_y0 .. ext_y0+ext_rows-1.  Returns the path taken: 0 nothing
 * drawn, 1 a plain copy of the view, 2 a scaled copy, 3 turned and scaled
 * through a dense copy. */
int ships_draw(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
               int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* the plain copy of view v (full size, unturned) with the ship's origin at (x, y), clipped as above */
void ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
                const short __far *ext, int ext_y0, int ext_rows);
/* the copy of any view record: flags 1 upside down (the rows in the other
 * order), 2 mirrored (each row's pixels in the other order), 3 both */
#define BLIT_V 1
#define BLIT_H 2
void ships_blit_view(const ShipView __far *view, int flags, int x, int y, int y0, int y1, int xa, int xb,
                     const short __far *ext, int ext_y0, int ext_rows);

/* the asm kernels checked against lightfield.c's: pixels compared, -1 on a mismatch */
long ships_selftest(void);

/* the benchmark of the three paths: view v drawn into the run being drawn
 * again and again, timed; out[k] = output pixels a millisecond of path k+1
 * (the copy; scaled to 3/4; turned 45 degrees at 3/4; the copy upside down;
 * the copy mirrored) */
void ships_time_paths(int set, int v, long out[5]);

#endif
