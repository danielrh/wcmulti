/* flight.h: the flight (flight.c): the loop's C side, the simulation, the
 * cockpit's state and the drawing benchmark (docs/engine.md, "Flight") */
#ifndef FLIGHT_H
#define FLIGHT_H

#include "rvdata.h"
#include "lightfield.h"

/* flies the mission: loads its assets, runs the loop in RVHOOK (rvflight.inc)
 * until missionStatus is set, frees them; returns the final missionStatus */
int flight_fly(const RvMission __far *m);

#ifdef RV_TESTS
/* the benchmark's result, after a flight: sprites a frame at 30 fps, and the
 * sprite's size (the test build, RVT.EXE) */
int flight_bench_result(int *w, int *h);
#endif

/* --- where a ship is on a screen: targeting, its lock, the cockpit's animations --- */

/* a camera: where it is, its frame, its projection and the part of the screen it shows */
typedef struct {
    LfVec pos;                          /* positions (256 a km) */
    LfBasis b;                          /* front, right, up (256 = 1.0) */
    int16_t cx, cy, focal;              /* the view's middle on the screen, the focal length (px) */
    int16_t vx0, vy0, vx1, vy1;         /* the part of the screen it shows: vx0 <= x < vx1, vy0 <= y < vy1 */
} RvCamera;

/* a ship as a camera sees it */
typedef struct {
    int16_t x0, y0, x1, y1;             /* the box of its sprite as drawn: x0 <= x < x1, y0 <= y < y1 (pixels); when it
                                           would not be drawn (too small, its sprite not in), the square below */
    int16_t sx, sy;                     /* its middle (its origin) on the screen */
    int16_t half;                       /* half its size there (px): (sx, sy) +- half is a box that does not change as
                                           it turns (lock brackets) */
    int16_t dir;                        /* the way to it on the screen, 0..1023 clockwise from up, even behind the
                                           camera (an arrow at the view's edge when it is off it) */
    int32_t X, Y, Z;                    /* it from the camera: right, up, ahead (sixteenths of a km) */
    uint8_t ahead;                      /* in front of the camera (else only dir, X, Y, Z mean anything) */
    uint8_t visible;                    /* ahead and its box touches the camera's part of the screen */
    uint8_t exact;                      /* drawn exactly this frame (the box the turned image's) */
    uint8_t pad;
} EntBox;

void flight_camera(RvCamera *cam);      /* the player's view: the camera the flight is drawn with */
int16_t flight_ent_box(int16_t slot, const RvCamera *cam, EntBox *out);   /* 0: no ship in that slot */

#endif
