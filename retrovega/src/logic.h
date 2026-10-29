/* logic.h: the game's logic in flight (logic.c): the step the flight loop runs once a frame, and the bodies of the
 * thunks the layer hooks (spec 5.1) that are play rather than drawing.  docs/logic.md is the guide. */
#ifndef LOGIC_H
#define LOGIC_H

#include "rvdata.h"

#define LOGIC_HZ 30                     /* steps a second: the flight draws a frame a step and waits out the rest of
                                           1 / LOGIC_HZ s (with the layer, its frame pacing sets the rate instead) */

/* the pilot's controls this step (flight.c reads them before the step) */
typedef struct {
    int8_t steer_x;                     /* -9 .. 9: + turns the nose right */
    int8_t steer_y;                     /* -9 .. 9: + pulls the nose up (the down arrow, a stick pulled back) */
    int8_t roll;                        /* -9 .. 9: + lifts the right wing (E), - the left (Q) */
    uint8_t guns;                       /* the trigger held: Space, the left button in the view, a script's hold fire */
    uint8_t missile;                    /* Enter pressed this step */
    uint8_t afterburner;                /* Tab held while there is fuel (flight.c's systems: g_sys.burning) */
} PilotInput;
extern PilotInput __far g_pilot;

/* */
void logic_prepare_spawn(int16_t ship_index);
void logic_start(const RvMission __far *m);   /* the flight begins: its ships into the slots (before their art loads) */
void logic_frame(void);                 /* one step (frameCounter is already one more) */
void logic_hud(void);                   /* a band of the view is drawn: draw over it with hud.h's calls */
void logic_end(void);                   /* the flight is over (missionStatus says how) */
int16_t logic_autopilot(void);          /* the A key, between steps: 1 when it flew the player on (to the next nav
                                           point), 0 when it may not (enemies near, nowhere to go) */

#endif
