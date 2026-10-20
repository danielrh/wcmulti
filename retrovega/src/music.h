/* music.h: the music engine (music.c); cues are RV_MUSIC_* (rvdata.h) */
#ifndef MUSIC_H
#define MUSIC_H
enum { MUSIC_NOW, MUSIC_BEAT, MUSIC_BAR };
int music_init(void);                   /* the songs, the MPU-401, the music clock: songs loaded */
void music_shutdown(void);
void music_record(int on);              /* tests: MIDILOG.BIN, every byte sent and when */
void music_cue(int cue, int fade_ms);   /* fade the current song out over fade_ms, then the cue's in */
void music_cue_at(int cue, int fade_ms, int at);   /* ... starting the fade on the next beat or bar */
unsigned long clock240(void);          /* the music clock: 240 ticks a second */
#endif
