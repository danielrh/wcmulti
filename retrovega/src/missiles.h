/* missiles.h: the flight's missiles (missiles.c), drawn with the laser
 * bolts' code: a missile is a bolt twice as thick and two and a half times
 * as long, put through the camera by lasers.c's lz_project (the bolts' own
 * math, its head kept); far away a gray line with a bright pixel at its
 * tail; near, the bolts' capsules in three parts (the rear quarter red, the
 * body dark gray, the tip bright red: the user's colours), the farthest
 * drawn first, two swept fins (lines), and an engine's baked flame out of
 * its tail (flames.c: the afterburner's look, its noise new every frame).  A
 * few at a time (four expected, MISSILE_MAX at most), so the C does it all. */
#ifndef MISSILES_H
#define MISSILES_H

#include "lasers.h"
#include "lightfield.h"

#define MISSILE_MAX 6
#define MISSILE_RADIUS16 64             /* its radius, sixteenths of a km: 4 km (a bolt's 2: "thicker", the user) */
#define MISSILE_HALF16 480              /* half its length: 30 km (60 long, 7.5 of its widths; a bolt is 24) */
#define MISSILE_REAR16 240              /* its red rear: 15 km, toward the engine */
#define MISSILE_TIP16 64                /* its bright red tip: 4 km */
#define MISSILE_FLAME16 640             /* its flame: 40 km (ten of its root's radii, the body's: the longest baked) */
#define MISSILE_ZMIN_KM 200             /* the near plane, as the bolts' */
#define MISSILE_ZMAX_KM 40000

typedef struct {
    long x, y, z;                       /* its middle, positions (256 a km) */
    short dx, dy, dz;                   /* its way, 256 = 1.0 */
    unsigned char live, burn;           /* flying; its motor lit */
} Missile;

void missiles_init(void);               /* a flight's start: none flying */
Missile __far *missiles_pool(void);     /* MISSILE_MAX of them */

/* once a frame, after the ships' flames are added (flames_frame's list): through the camera (its position and
 * basis, the view's centre and focal length; rows y0 .. y1-1 may show them) */
void missiles_place(long px, long py, long pz, const LfBasis *cam, int cx, int cy, int focal, int y0, int y1);
int missiles_on_screen(int *lines, int *fats);

/* per band, after the ships: the farthest first, each clipped to the opening's extents */
void missiles_draw(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

void missiles_ticks(unsigned long out[3]);   /* 240 Hz ticks placing, drawing; frames with any flying */

#endif
