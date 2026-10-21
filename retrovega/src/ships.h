/* ships.h: ship views for the flight (ships.c): a type's pre-rendered set
 * SHIPnn.RVS in XMS (the mission level) in gfx.c's run-length row format,
 * its full-size views streamed from the file as the exact draw needs them,
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
    XmsBlock blk;                       /* the XMS block holding it; 0: not resident, streamed from the file */
    unsigned long at;                   /* offset of the row table (h + 1 dwords) in it, rows after; or in the file */
    unsigned long len;                  /* its bytes, for the stream */
} ShipView;

#define SET_BLOCKS 64                   /* 1 + 12 sizes + 9 sizes x 4 stored rolls = 49 */

/* one (size, roll) block of a pre-rendered set (SHIPnn.RVS, tools/rvsprites.c) */
typedef struct {
    unsigned char size, roll, loaded, pad;
    unsigned long at, length;           /* in the file */
    unsigned long index_at;             /* its view table in the set's index block */
    XmsBlock blk;                       /* its data, once loaded */
} SetBlock;

typedef struct {
    unsigned char type, asym, rvs, pad;      /* rvs: a pre-rendered set; else the .RV views at full size only */
    unsigned short nviews;
    unsigned char nb0, nh0;             /* the full-size views' lattice (the exact draw's) */
    unsigned char nb1, nh1;             /* the sets' (the copies') lattice: 5 x 7 */
    unsigned char nsizes, nrolls, roll_sizes, pad2;
    unsigned short lod0;
    unsigned short scale[16];           /* the size steps, 256 = full */
    XmsBlock blk;                       /* .RV: the views; .RVS: the index */
    int fd;                             /* .RVS: the file, open for the stream (-1 none) */
    unsigned long max_len;              /* its longest full-size view */
    int nblocks;
    SetBlock block[SET_BLOCKS];
    ShipView view[SHIP_VIEWS];          /* the full-size, unturned views (a .RVS set's block 0, not resident:
                                           blk 0, at and len in the file): a symmetric ship's mirrored
                                           headings are empty and made from their pair */
} ShipSet;

/* the lattice a set's views are in: the exact draw's (block 0) or the copies' */
void ships_lattice(int set, int exact, int *nb, int *nh);

int ships_load(unsigned type);          /* a type's set: SHIPnn.RVS (its index; the file stays open), else
                                           SHIPnn.RV decoded; -> set index, or -1 (no file, no room) */
unsigned long ships_load_blocks(unsigned long budget);   /* the stream's slots, then every open set's remaining
                                           blocks, ladder first then rolls by size, while they fit the budget
                                           (bytes): returns the bytes taken */
int ships_find(unsigned type);          /* -> loaded set index, or -1 */
const ShipSet __far *ships_set(int i);
void ships_unload(void);                /* forgets every set (the mission level is freed by the caller) */

/* the view the camera projects through: its centre on screen and its focal length */
void ships_set_camera(int cx, int cy, int focal);

/* The stream: a .RVS set's full-size views stay in the file; the one a
 * draw needs is read into one of a few XMS slots (the nearest ships' views
 * stay in them, least recently used out), at most `reads` a frame, and
 * only in the frame's plan (ships_prefetch, before the draws): a draw that
 * finds its view not in draws nothing and reports so, for the caller to
 * draw from the sets that frame.  DOSBox serves the read at host speed, so
 * it costs no frame time there; ships_stream_log says what it cost. */
void ships_stream_config(int slots, int reads);        /* defaults 6 and 2; slots up to 8; -1 keeps a value */
int ships_stream_slots(void);           /* the stream's slots that have their XMS: the ships drawn exactly are fewer */
#define SHIPS_DENSE_BUFS 2              /* the turned draw's dense copies (64 KB each, in upper memory): the ships drawn
                                           exactly are no more, or each re-expands its view every frame */
void ships_frame(void);                 /* at the top of each frame: a new read budget */
/* the full-size view the camera sees a ship by (the exact draw's), brought in now when the budget allows;
 * ships_prefetch_view a view by number (the MFD's, the benchmark's).  1 when it is in. */
int ships_prefetch(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship);
int ships_prefetch_view(int set, int v);
/* the view a ship needs marked as wanted this frame, nothing read: the plan touches every exact ship's before it
 * reads any, so a read takes a slot no ship needs now (else one ship's read evicted the next one's view, and that
 * ship was drawn from the sets for a frame: a wobble) */
int ships_touch(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship);   /* the view, -1 off screen */
void ships_touch_view(int set, int v);  /* a view by number marked so (a turning ship's last: its fallback this frame) */
void ships_stream_log(void);            /* the flight's tally: views read, ms a read, draws that had to wait */

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
 * also 2 x 1 blocks.  Returns 0 when the view is not in (not streamed yet)
 * or too big for the dense buffer: the caller draws from the sets. */
int ships_draw_exact(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, int detail,
                     int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* a set's full-size view v painted on the sky: at direction dir (the world
 * frame, any length), its up `up` in the world, turned with the sky as the
 * camera rolls (lf_sky), at scale256 / 256 of full size; the view must be
 * prefetched in the frame's plan (ships_prefetch_view).  For suns, nebulae,
 * planets.  Returns 0 when nothing is drawn (behind, or the view not in). */
int ships_draw_sky(int set, int v, const LfVec *dir, const LfVec *up, const LfBasis *cam, int scale256,
                   int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows);

/* where a ship would show and how big: lf_place through the camera, and the
 * output pixels its full-size view would cover at scale256 (for the plan) */
long ships_size_of(int set, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, LfPlace *p);

/* the plain copy of view v (full size, unturned) with the ship's origin at (x, y), clipped as above:
 * 0 when the view is not in (not prefetched) */
int ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
               const short __far *ext, int ext_y0, int ext_rows);
/* the sets' copy of view v (of the copies' lattice) at size step k, unturned, the ship's origin at (x, y):
 * resident, no slot taken (step 1, 84 %, is the first resident: step 0 is streamed); 0 when its block is not
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
