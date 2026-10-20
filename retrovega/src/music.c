/* music.c: the music engine's interface.  Until the sequencer arrives
 * (docs/engine.md: Standard MIDI Files, MPU-401 first, fast fades) a cue
 * only logs, so the game already asks for music where it will play. */
#include "rv.h"
#include "music.h"

static int g_cue = -1;

void music_cue(int cue, int fade_ms)
{
    if (cue == g_cue)
        return;
    g_cue = cue;
    rv_log("music: cue %d, fade %d ms", cue, fade_ms);
}
