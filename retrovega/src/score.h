/* score.h: the flight's music, chosen from what is happening (score.c) */
#ifndef SCORE_H
#define SCORE_H
#include "rvdata.h"
void score_start(const RvMission __far *m);     /* a flight begins: the first calm song (m: its fields) */
void score_tick(int landing);           /* once a frame: the mood from the ships and the systems; landing: the
                                           player has called the carrier */
#endif
