/* logic.h: the game's logic in flight (logic.c): the step the flight loop runs once a frame, and the bodies of the
 * thunks the layer hooks (spec 5.1) that are play rather than drawing.  docs/logic.md is the guide. */
#ifndef LOGIC_H
#define LOGIC_H

#include "rvdata.h"

#define LOGIC_HZ 30                     /* steps a second: the flight draws a frame a step and waits out the rest of
                                           1 / LOGIC_HZ s (with the layer, its frame pacing sets the rate instead) */

/* the pilot's controls this step (flight.c reads them before the step) */
typedef struct {
    signed char steer_x;                /* -9 .. 9: + turns the nose right */
    signed char steer_y;                /* -9 .. 9: + pulls the nose up (the down arrow, a stick pulled back) */
    signed char roll;                   /* -9 .. 9: + lifts the right wing (E), - the left (Q) */
    unsigned char guns;                 /* the trigger held: Space, the left button in the view, a script's hold fire */
    unsigned char missile;              /* Enter pressed this step */
    unsigned char afterburner;          /* Tab held while there is fuel (flight.c's systems: g_sys.burning) */
} PilotInput;
extern PilotInput __far g_pilot;

void logic_start(const RvMission __far *m);   /* the flight begins: its ships into the slots (before their art loads) */
void logic_frame(void);                 /* one step (frameCounter is already one more) */
void logic_hud(void);                   /* a band of the view is drawn: draw over it with hud.h's calls */
void logic_end(void);                   /* the flight is over (missionStatus says how) */

#endif
