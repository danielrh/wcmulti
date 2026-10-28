/* sfx.h: the flight's sound effects (sfx.c): SFX.DAT's sounds (tools/sfx.py, data/sounds.csv: RV_SFX_*), mixed
 * eight voices at a time into 8-bit sound at 22,050 Hz.  On DOS a Sound Blaster (2.0 or later, the BLASTER
 * variable's port, IRQ and 8-bit DMA) plays it from an auto-initialising DMA ring the mixer keeps ahead of the
 * card; on the native build the host's audio does (hle.c, plat_pcm).  fx.c says what to play. */
#ifndef SFX_H
#define SFX_H

#include "rvtypes.h"

int16_t sfx_reserve(void);              /* when the game starts (resident XMS): SFX.DAT in */
int16_t sfx_flight_start(void);         /* the card started (0: no sound: no card, no SFX.DAT) */
void sfx_flight_end(void);              /* the card stopped */
/* a sound (RV_SFX_*) at a volume (0 .. 256) and a pitch (256 its own; 240 a little lower) */
void sfx_play(int16_t id, int16_t vol, int16_t pitch256);
void sfx_poll(void);                    /* once a frame: the sound mixed ahead of the card */
uint32_t sfx_mixed(void);               /* samples mixed this flight (the tests' count) */

#endif
