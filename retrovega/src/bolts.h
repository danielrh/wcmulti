/* bolts.h: the flight's laser bolts (bolts.c): a pool of them in the world,
 * flown, put through the camera once a frame and drawn band by band, through
 * lasers.c's module and its asm (lasers.asm). */
#ifndef BOLTS_H
#define BOLTS_H

#include "lasers.h"
#include "lightfield.h"

#define BOLT_RADIUS16 32                /* a bolt's radius, sixteenths of a km (2 km) */
#define BOLT_HALF16 96                  /* half its length (6 km): 32 x 8 px at the near plane, side on */
#define BOLT_ZMIN_KM 200                /* the near plane: a bolt nearer is cut there (so drawn at most 8 px across) */
#define BOLT_ZMAX_KM 40000
#define BOLT_RMAX16 64                  /* the radius drawn at most: 4 px */
#define BOLT_LINE16 12                  /* under 3/4 px of radius a bolt is a line */

int bolts_init(int want, unsigned keep_kb);   /* room for `want` bolts, or as many as memory allows keeping keep_kb
                                           free (a byte each for the caller too): returns the room */
void bolts_free(void);
unsigned long bolts_dos_free(void);      /* bytes in DOS's largest free block */
LzBolt __far *bolts_pool(void);
void bolts_set_count(int n);            /* how many of the pool fly */
int bolts_count(void);

void bolts_move(long k);                /* every bolt flies k positions per sixteenth of a km of its half axis */
#define BOLT_SHAPE_CAPSULE2 (LZ_SHAPE_LAST + 1)   /* the capsules in two colours: no dark rim pass */
void bolts_set_shape(int shape);        /* the near bolts' shape: LZ_SHAPE_CAPSULE (the default) .. _ROUND, or
                                           BOLT_SHAPE_CAPSULE2 */

/* once a frame: through the camera (its position and basis), the view's
 * centre and focal length; the bolts may show in rows y0 .. y1-1.  The line
 * radius (x16) is BOLT_LINE16 normally; 0 draws every bolt as a capsule
 * (the benchmark's "one algorithm"). */
void bolts_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1, int line16);
int bolts_on_screen(int *lines, int *fats);

/* per band: the lines (the far bolts), then the fat ones (the near), each
 * clipped to the opening's extents as the ships are */
void bolts_draw_far(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);
void bolts_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

/* the asm against lasers.c on random bolts: records and pixels compared;
 * -1 on a mismatch (logged), else how many pixels agreed */
long bolts_selftest(void);

/* the cost of each stage, timed on the 240 Hz clock (logged): microseconds a
 * bolt x 100 for a far bolt's place and draw, a near one's place, set-up and
 * draw, and a far one drawn as a capsule */
void bolts_time(long out[6]);

/* what the frames cost so far: 240 Hz ticks in place, far draws, near draws, flight */
void bolts_ticks(unsigned long out[4]);
void bolts_ticks_reset(void);

#endif
