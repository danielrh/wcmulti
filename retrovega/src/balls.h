/* balls.h: the spherical projectiles (balls.c): a pool of balls in the
 * world, flown, put through the camera once a frame (lasers.asm's
 * rv_balls_place) and drawn band by band from sprites at every diameter
 * (rv_balls_draw): tools/balls.py draws each kind, frame and diameter 1 ..
 * dmax into BALLS.DAT, read into XMS when the game starts; the small ones
 * (the far balls, nearly all of them) are kept in conventional memory too,
 * the bigger ones fetched once a frame as they are needed.  A far ball is
 * one pixel of its colour. */
#ifndef BALLS_H
#define BALLS_H

#include "lasers.h"
#include "lightfield.h"

#define BALL_SLUG 0                     /* a silver mass driver slug (one frame) */
#define BALL_PLASMA 1                   /* a glowing blue ball, its light swirling (four frames) */
#define BALL_FIRE 2                     /* a searing red and white ball, energy crackling out of it (four) */
#define BALL_STAR 3                     /* a shining green ball with four spikes, turning (four) */
#define BALL_KINDS 4
#define BALL_ZMIN_KM 100                /* the near plane: a ball nearer is not drawn */
#define BALL_ZMAX_KM 40000

int balls_reserve(void);                /* when the game starts, before RV.DAT (resident XMS): BALLS.DAT in */
int balls_init(int want, unsigned keep_kb);   /* room for `want` balls, or as many as memory allows keeping keep_kb */
void balls_free(void);
LzBall __far *balls_pool(void);
void balls_set_count(int n);
int balls_count(void);
void balls_move(void);                  /* every ball flies a frame of its velocity */

/* once a frame: through the camera (its position and basis, the view's
 * centre and focal length; the balls may show in rows y0 .. y1-1), their
 * sprites found; anim the animation's step */
void balls_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1,
                 unsigned anim);
int balls_on_screen(int *small, int *big);

/* per band: the small (far) ones, before the ships; the big (near) ones, after */
void balls_draw_far(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);
void balls_draw_near(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

long balls_selftest(void);              /* the asm against lasers.c: -1 on a mismatch, else the pixels compared */
void balls_time(void);                  /* what a ball costs (logged) */
void balls_ticks(unsigned long out[3]); /* 240 Hz ticks in place, far draws, near draws */

#endif
