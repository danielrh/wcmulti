/* ships.h: ship views for the flight (ships.c): a type's pre-rendered set
 * SHIPnn.RVS in XMS (the mission level) in gfx.c's run-length row format,
 * its full-size views (the exact draw's) resident there too,
 * the mirrored headings made at draw time, and the draw of one as the
 * camera sees it, through the lightfield module's placement and kernels. */
#ifndef SHIPS_H
#define SHIPS_H

#include "rv.h"
#include "lightfield.h"

#define SHIP_VIEWS 330                  /* view records kept: 17 x 19 (10 degrees) with its poles; a symmetric set's
                                           mirrored headings are not stored (their indices run past this and are drawn
                                           as the pair's mirror image); an asymmetric set stores them: 11 x 13 at most */
#define SHIP_SETS 8

typedef struct {
    unsigned short w, h;
    short ox, oy;                       /* the ship's origin in the view */
    XmsBlock blk;                       /* the XMS block holding it; 0: not resident (a full-size view whose block did
                                           not fit) */
    unsigned long at;                   /* offset of the row table (h + 1 dwords) in it, rows after */
} ShipView;

#define SET_BLOCKS 96                   /* 1 + 12 sizes + 9 sizes x 8 stored rolls = 85 (an asymmetric ship: x 7) */

/* one (size, roll) block of a pre-rendered set (SHIPnn.RVS, tools/rvsprites.c) */
typedef struct {
    unsigned char size, roll, loaded, pad;
    unsigned short nv;                  /* views in its table (a ladder-only set's blocks hold the lattice's own only) */
    unsigned long at, length;           /* in the file */
    unsigned long index_at;             /* its view table in the set's index block */
    XmsBlock blk;                       /* its data, once loaded: a size's rolls share one XMS block (they lie together
                                           in the file), each at its own offset in it */
    unsigned long xoff;
} SetBlock;

typedef struct {
    unsigned char type, asym, rvs, factor;
    unsigned char rvh;                  /* its file is SHIPnn.RVH (the block mode's), not .RVS: the loaders open it */   /* rvs: a pre-rendered set; else the .RV views at full size only.
                                                factor 2 or 4: a SHIPnn.RVH set, its sprites half or quarter size,
                                                drawn that many times bigger as blocks (RV.EXE /res 2) */
    unsigned short nviews;
    unsigned char nb0, nh0;             /* the full-size views' lattice (the exact draw's) */
    unsigned char nb1, nh1;             /* the sets' (the copies') lattice: 5 x 7 */
    unsigned char nsizes, nrolls, roll_sizes, pad2;
    unsigned short lod0;
    unsigned short scale[16];           /* the size steps, 256 = full */
    XmsBlock blk;                       /* .RV: the views; .RVS: the index */
    unsigned short rolls_in;            /* bit k: every roll block the set has at size k is loaded */
    int nblocks;
    SetBlock block[SET_BLOCKS];
    ShipView view[SHIP_VIEWS];          /* the full-size, unturned views (a .RVS set's block 0, not resident:
                                           blk 0, at and len in the file): a symmetric ship's mirrored
                                           headings are empty and made from their pair */
} ShipSet;

extern int g_res;                       /* RV.EXE /res N: 2 (the default) SHIPnn.RVH; 1 the sets as they were (SHIPnn.RVS), the
                                           ladder only on every view of the lattice at half size (capitals a
                                           quarter), every ship turned exactly from it as 2 x 2 (4 x 4) blocks */

/* the lattice a set's views are in: the exact draw's (block 0) or the copies' */
void ships_lattice(int set, int exact, int *nb, int *nh);

int ships_load(unsigned type);          /* a type's set: SHIPnn.RVS (its index), else SHIPnn.RV decoded; -> set
                                           index, or -1 (no file, no room) */
unsigned long ships_load_blocks(unsigned long budget);   /* the dense copies' cache, then every open set's blocks:
                                           the ladder, the full-size views, the rolls by size, while they fit the
                                           budget (bytes): returns the bytes taken.  Nothing is read in flight */
int ships_find(unsigned type);          /* -> loaded set index, or -1 */
const ShipSet __far *ships_set(int i);
void ships_unload(void);                /* forgets every set (the mission level is freed by the caller) */

/* the view the camera projects through: its centre on screen and its focal length */
void ships_set_camera(int cx, int cy, int focal);

/* where the sprite ships_draw_as (how: SHIP_AS_*) would draw would be on a screen of that projection: its box, box[0] <= x < box[2], box[1] <= y < box[3] (a sprite's record: the turned image's box when turned at draw
 * time); 0 when it would not be drawn (behind the camera, too small, its sprite not in) */
int ships_box(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, int how,
              int cx, int cy, int focal, int box[4]);

#define SHIPS_DENSE_BUFS 2              /* the turned draw's dense copies (64 KB each, in upper memory; the last eight
                                           made kept in XMS, moved back when wanted) */
void ships_draw_log(void);              /* the flight's tally: dense copies expanded, moved back from XMS */

/* THE draw: the ship of set `set` at rel = ship - camera (positions, 256 = 1
 * km), with the camera's and the ship's bases, scale256 / 256 of its full
 * size; into the run y0..y1-1, each row clipped to [xa, xb) and, when ext
 * is given, to its row's extent (ext[2 * (row - ext_y0)], ext[.. + 1]) for
 * rows ext_y0 .. ext_y0+ext_rows-1.  Returns the path taken: 0 nothing
 * drawn, 1 a plain copy of the view, 2 a scaled copy, 3 turned and scaled
 * through a dense copy. */
int ships_draw(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
               int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* the same ship drawn exactly: its full-size view turned by its true roll
 * and scaled to the size wanted, by inverse mapping from a dense copy
 * (sixteen cycles a pixel: for the nearest ships only, see flight.c's
 * plan); detail 1 every pixel, 2 every other row copied to the next, 4
 * also 2 x 1 blocks.  Returns 0 when the view is not in (its full-size
 * block did not fit) or too big for the dense buffer: the caller draws from
 * the sets. */
int ships_draw_exact(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, int detail,
                     int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* how a ship is drawn: ships_draw's returns (1 + the flips: a set's sprite at its roll step, or a coarser one),
 * and ships_draw_exact's ways */
#define SHIP_PATH_UNTURNED 5            /* a set's unturned sprite (no roll step in: the far sizes) */
#define SHIP_PATH_SCALED 6              /* (a set without copies) scaled, unturned */
#define SHIP_PATH_TURNED 7              /* (a set without copies) turned at draw time, sixteen steps */
#define SHIP_PATH_EXACT_BLIT 20         /* exactly: full size, unturned (its roll within 3 degrees of none) */
#define SHIP_PATH_EXACT 21              /* exactly: turned by its true roll */

/* how the plan wants a ship drawn */
#define SHIP_AS_SETS 0                  /* from the sets (ships_draw) */
#define SHIP_AS_EXACT 1                 /* exactly (ships_draw_exact); when it cannot be (its full-size views did not
                                           fit): from the sets */

/* a ship drawn as the plan wants it (how, and detail as ships_draw_exact's: 2 for half height, each odd row the
 * one above copied down): the path taken (SHIP_PATH_*, or ships_draw's), 0 when nothing is drawn */
int ships_draw_as(int set, int how, int detail, const LfVec *rel, const LfBasis *cam, const LfBasis *ship,
                  int scale256, int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* the sets have no turned sprites in for a ship of this size: drawn from them it would show unturned (the plan
 * draws it exactly) */
int ships_turn_wanted(int set, int scale256);

/* the view and the turn its picture shows of a ship as ships_draw_as draws it (how), made by the same choices: *p
 * placed in the lattice (*nb, *nh) of that picture, its view and roll those the picture has.  The engines' flames
 * take their nozzles and their way from it (the user: they must line up with the ship exactly, 2026-10-21).
 * Returns the path (SHIP_PATH_*, or ships_draw's), 0 when it would not be drawn (p->on_screen 0) */
int ships_drawn(int set, int how, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
                LfPlace *p, int *nb, int *nh);

/* what the last ships_draw or ships_draw_exact drew: its path (0: nothing), *view and *roll as ships_drawn gives
 * them (the flames' self-test) */
int ships_last_drawn(int *view, int *roll);

/* a set's full-size view v painted on the sky: at direction dir (the world
 * frame, any length), its up `up` in the world, turned with the sky as the
 * camera rolls (lf_sky), at scale256 / 256 of full size.  For suns, nebulae,
 * planets.  Returns 0 when nothing is drawn (behind, or the view not in). */
int ships_draw_sky(int set, int v, const LfVec *dir, const LfVec *up, const LfBasis *cam, int scale256,
                   int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* where a ship would show and how big: lf_place through the camera, and the
 * output pixels its full-size view would cover at scale256 (for the plan) */
long ships_size_of(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, LfPlace *p);

/* the plain copy of view v (full size, unturned) with the ship's origin at (x, y), clipped as above:
 * 0 when the view is not in */
int ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
               const short __far *ext, int ext_y0, int ext_rows);
/* the sets' copy of view v (of the copies' lattice) at size step k, unturned, the ship's origin at (x, y):
 * resident (step 0 is the full-size block, in the dense lattice: from step 1, 84 %); 0 when its block is not
 * loaded.  ships_view_size tells its size the same way. */
int ships_blit_size(const ShipSet __far *s, int k, int v, int x, int y, int y0, int y1, int xa, int xb,
                    const short __far *ext, int ext_y0, int ext_rows);
int ships_view_size(const ShipSet __far *s, int k, int v, int *w, int *h);
/* the copy of any view record: flags 1 upside down (the rows in the other
 * order), 2 mirrored (each row's pixels in the other order), 3 both */
#define BLIT_V 1
#define BLIT_H 2
/* two views of one size and origin (view's, and the one whose rows are at at_b in its block), each row from one or
 * the other at random by seed: the engines' flames, two frames of noise mixed row by row */
void ships_blit_mix(const ShipView __far *view, unsigned long at_b, unsigned seed, int flags, int x, int y, int y0,
                    int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);
void ships_blit_view(const ShipView __far *view, int flags, int x, int y, int y0, int y1, int xa, int xb,
                     const short __far *ext, int ext_y0, int ext_rows);
/* a view kept at its full height (step 1) or half (2, each row for two screen rows), drawn rows 1 every row, 2
 * doubled (of a full view: its even rows twice), 3 interlaced (the even screen rows from its top only) */
void ships_blit_rows(const ShipView __far *view, int flags, int step, int rows, int x, int y, int y0, int y1, int xa,
                     int xb, const short __far *ext, int ext_y0, int ext_rows);

/* the exact draw's two dense buffers (64 KB each, kept for good once taken) taken now, so that memory
 * sized after (the laser bolts' pool) leaves them their room: 0 when they do not fit */
int ships_dense_reserve(void);

/* the asm kernels checked against lightfield.c's: pixels compared, -1 on a mismatch */
long ships_selftest(void);

/* the benchmark of the three paths: view v drawn into the run being drawn
 * again and again, timed; out[k] = output pixels a millisecond of path k+1
 * (the copy; scaled to 3/4; turned 45 degrees at 3/4; the copy upside down;
 * the copy mirrored) */
void ships_time_paths(int set, int v, long out[5]);

#endif
