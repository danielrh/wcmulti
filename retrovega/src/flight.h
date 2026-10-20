/* flight.h: the flight (flight.c): the loop's C side, the simulation, the
 * cockpit's state and the drawing benchmark (docs/engine.md, "Flight") */
#ifndef FLIGHT_H
#define FLIGHT_H

#include "rvdata.h"

/* flies the mission: loads its assets, runs the loop in RVHOOK (rvflight.inc)
 * until missionStatus is set, frees them; returns the final missionStatus */
int flight_fly(const RvMission __far *m);

/* the benchmark's result, after a flight: sprites a frame at 30 fps, and the
 * sprite's size */
int flight_bench_result(int *w, int *h);

#endif
