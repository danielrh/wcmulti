/* fx.h: the flight's effects from what happens in the world (fx.c): explosions (explode.c) and sounds (sfx.c).
 *
 * It only watches, as the music does (score.c): the game's logic tells it nothing.  A ship whose shipAiState turns 9
 * explodes (a capital ship in bursts first), and its sprite is hidden once the flash covers it; a missile slot that
 * appears is a launch, one that ends a burst; every shot plays its weapon's sound (shots.c calls fx_shot from
 * shot_add) and every hit throws sparks (fx_hit from shot_hit), both as loud as they are near.  The pictures and
 * sounds are each machine's own and take their own randomness, never the play's. */
#ifndef FX_H
#define FX_H

#include "rvhooks.h"

void fx_flight_start(void);             /* the world as it stands taken as seen */
void fx_frame(void);                    /* after each step: what changed */
int16_t fx_hidden(int16_t slot);        /* a dying ship the explosion has replaced: not drawn */
void fx_shot(int16_t owner, uint16_t weapon_type);              /* a shot added (shots.c) */
void fx_hit(int16_t slot, const RvVec3 *where, int16_t owner);  /* a shot met a ship there (shots.c) */

#endif
