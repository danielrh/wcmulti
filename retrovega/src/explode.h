/* explode.h: the flight's explosions (explode.c), drawn from a few cels of heat (tools/explode.py, EXPLODE.DAT).
 *
 * The user's brief (2026-10-28): compact and simple, lo-fi pixel art; a central blast going out in every direction,
 * generic enough not to tire; losing its energy from the middle first, so it spreads, and fading as it reaches its
 * edge.  The cels are not colours but heat (0 clear .. 255 white hot): eight frames of the blast at 48 x 48, two
 * variants, 37 KB in all, kept in XMS.  Every frame each explosion takes its timeline's entry (the cel, its growth,
 * its cooling and its dissolve: the tool's numbers) and draws its cel scaled through a heat-to-colour table made new
 * for that frame, the cool pixels dissolving in an ordered dither; so between two cels the picture still moves every
 * frame.  Over it, at the screen's own pixels: a flash, sparks cooling as they fly, debris tumbling out.  Each
 * explosion turns its cel one of eight ways and takes a variant.  Nothing here decides anything: fx.c starts
 * explosions from what happens in the world, and they are each machine's own pictures. */
#ifndef EXPLODE_H
#define EXPLODE_H

#include "rvhooks.h"
#include "lightfield.h"

enum { BOOM_SHIP, BOOM_CAPITAL, BOOM_MISSILE, BOOM_HIT, BOOM_SHIELD, BOOM_KINDS };
#define EXPLODE_MAX 16                  /* at once */

int16_t explode_reserve(void);          /* when the game starts (resident XMS): EXPLODE.DAT in */
int16_t explode_flight_start(void);     /* none; their buffers (0: no art, none drawn) */
void explode_flight_end(void);

/* an explosion of a kind (BOOM_*) at a place (positions), drifting at vel (positions a step, slowing), its full
 * radius in km, starting after `delay` steps; seed: its picture's own randomness (the variant, the turn, the
 * sparks) */
void explode_add(const RvVec3 *at, const RvVec3 *vel, int32_t radius_km, int16_t kind, uint16_t delay, uint16_t seed);

/* a shield taking a hit (the user's brief, 2026-10-31): no cels and no fire, a haze of light blue stipple right where
 * the shot struck, moving with the ship, its pixels new every frame.  strength: how much of that shield the hit left,
 * 0 .. 255: the more, the bigger, denser and brighter; a failing shield's is small and dim and sputters.  A ship keeps
 * a few at once: more hits take the place of its oldest */
void explode_shield(const RvVec3 *at, const RvVec3 *vel, int32_t radius_km, uint8_t strength, int16_t slot, uint16_t seed);
int16_t explode_count(void);
void explode_frame(void);               /* a step: each one older, drifted; the spent gone */

/* once a frame, after explode_frame: through the camera (the eye, its basis, the view's centre and focal length),
 * the ones on the screen sorted far to near */
void explode_place(const LfVec *eye, const LfBasis *cam, int16_t cx, int16_t cy, int16_t focal);

/* per band of rows: explode_draw_begin, then explode_draw_to before each ship (those farther than its depth,
 * positions along the view), then explode_draw_rest */
void explode_draw_begin(void);
void explode_draw_to(int32_t depth, int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows);
void explode_draw_rest(int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows);

#endif
