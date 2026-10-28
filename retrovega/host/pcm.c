/* pcm.c: the game's mixed sound effects on the host (pcm.h).
 *
 * The core mixes its voices (src/sfx.c) as the host's clock asks (hle.c) and hands each block over stamped with
 * the time its first sample is due; the blocks follow one another, sample for sample, so one start time and a count
 * place every sample.  A ring of three seconds holds them; the audio thread reads the sample due at each of its
 * frames, nearest, and adds it to both channels.  A block whose time does not follow the last (the game stalled and
 * skipped ahead, or a new flight) starts the stream again. */
#include <string.h>
#include "pcm.h"

#define RING (1 << 16)                  /* samples: three seconds at 22,050 Hz */
#define GAIN 150                        /* an 8-bit sample to 16 bits, under the music */

static int8_t g_ring[RING];
static double g_t0;                     /* the time of the stream's sample 0, us */
static uint32_t g_w;                    /* samples in the stream */
static uint16_t g_rate;
static int g_on;

void pcm_put(uint64_t at_us, uint16_t rate, const uint8_t *s, int n)
{
    double k;
    int i;
    if (n <= 0 || !rate)
        return;
    k = ((double)at_us - g_t0) * rate / 1e6;
    if (!g_on || rate != g_rate || k < (double)g_w - 2 || k > (double)g_w + 2) {
        g_on = 1;                       /* (a new stream: this block its sample 0) */
        g_rate = rate;
        g_t0 = (double)at_us;
        g_w = 0;
        memset(g_ring, 0, sizeof g_ring);
    }
    for (i = 0; i < n; i++)
        g_ring[(g_w + (uint32_t)i) & (RING - 1)] = (int8_t)(s[i] - 128);
    g_w += (uint32_t)n;
}

void pcm_mix(int16_t *out, int n, double out_rate, double t0_us)
{
    int i, c;
    if (!g_on)
        return;
    for (i = 0; i < n; i++) {
        double k = (t0_us + i * 1e6 / out_rate - g_t0) * g_rate / 1e6;
        int32_t v;
        uint32_t j;
        if (k < 0 || k >= (double)g_w || k < (double)g_w - (RING - 4096))
            continue;
        j = (uint32_t)k;
        v = g_ring[j & (RING - 1)] * GAIN;
        for (c = 0; c < 2; c++) {
            int32_t s = out[2 * i + c] + v;
            out[2 * i + c] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
    }
}
