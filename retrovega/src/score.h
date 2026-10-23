/* score.h: the flight's music, chosen from what is happening (score.c) */
#ifndef SCORE_H
#define SCORE_H
void score_start(void);                 /* a flight begins: the first calm song */
void score_tick(int landing);           /* once a frame: the mood from the ships and the systems; landing: the
                                           player has called the carrier */
#endif
