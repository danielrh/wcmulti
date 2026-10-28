/* pcm.h: the game's mixed sound effects on the host (pcm.c): the native player (play.c) and the desktop front end
 * (front.c) take the core's 8-bit stream, each block stamped with the host's time of its first sample, and their
 * audio adds it under the OPL3 at the same lag. */
#ifndef RV_PCM_H
#define RV_PCM_H

#include <stdint.h>

/* n unsigned 8-bit samples at rate Hz, the first due at at_us (the host's clock): call it with the audio locked */
void pcm_put(uint64_t at_us, uint16_t rate, const uint8_t *s, int n);
/* adds the stream into n stereo frames of out (16-bit), frame i due at t0_us + i * 1e6 / out_rate */
void pcm_mix(int16_t *out, int n, double out_rate, double t0_us);

#endif
