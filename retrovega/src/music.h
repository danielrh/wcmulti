/* music.h: the music engine (music.c); cues are RV_MUSIC_* (rvdata.h) */
#ifndef MUSIC_H
#define MUSIC_H
enum { MUSIC_NOW, MUSIC_BEAT, MUSIC_BAR };
enum { MUSIC_AUTO, MUSIC_OPL3, MUSIC_MPU401, MUSIC_OFF };
void music_device(int want);            /* before music_init: MUSIC_AUTO is the OPL3, else the MPU-401 */
int music_init(void);                   /* the songs, the synthesizer, the music clock: songs loaded */
void music_shutdown(void);
void music_record(int on);              /* tests: OPLLOG.BIN or MIDILOG.BIN, everything sent and when */
void music_poll(void);                  /* the main loop's: writes out what was recorded */
void music_cue(int cue, int fade_ms);   /* fade the current song out over fade_ms, then the cue's in */
void music_cue_at(int cue, int fade_ms, int at);   /* ... starting the fade on the next beat or bar */
void music_cue_body(int cue, int fade_ms, int at);   /* ... from its loop's marker: past its intro (the battles' chain) */
void music_sting(int cue);              /* a sting, once: then the song it cut short goes on from there */
unsigned music_loops(void);             /* times the playing song has come round since it began */
int music_playing(void);                /* the cue sounding now (-1 none; a sting) */
unsigned long clock240(void);          /* the music clock: 240 ticks a second */
#endif
