/* lipsync.h: mouth shapes from text (lipsync.c) */
#ifndef LIPSYNC_H
#define LIPSYNC_H
#define LIP_STEP_MS 100
int lip_steps(const char __far *text, unsigned char __far *out, int max);   /* one shape per 100 ms */
#endif
