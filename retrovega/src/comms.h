/* comms.h: the comm link on a cockpit VDU (comms.c).
 *
 * A pilot's face over a grainy green-and-white link (the user's brief,
 * 2026-10-22): every human pilot in the same helmet, their own face behind
 * its visor, their callsign on it, the eyes blinking and glancing, the mouth
 * speaking the line (lipsync.c's shapes).  The art is tools/comms.py's, in
 * RV.DAT: one shared helmet picture (W x H in the link's shades), and for
 * each pilot a window (what differs from pilot to pilot) and twelve patches
 * (eyes half, shut, left, right; mouths 1 .. 8), read in as a comm starts.
 *
 * The picture is put together once when a comm starts, in the flight
 * palette's entries with the scan lines in (each odd row through a darker
 * ramp), and patched when the eyes or the mouth change; a frame copies its
 * rows to the VDU (a rep movsd a row) and sprinkles a few grains (a shade
 * up or down).  Centred on the VDU: cropped where it is smaller, the link's
 * background round it where it is bigger. */
#ifndef RV_COMMS_H
#define RV_COMMS_H

#include "rvasset.h"

#define COMMS_W 112
#define COMMS_H 80

void comms_flight_start(void);          /* the picture's buffer and the ramp (call with the flight's palette in) */
void comms_flight_end(void);

/* person (RV.DAT's people, people.csv's order) on VDU vdu (0 left, 1 right) saying text (the mouth follows it;
 * the comm ends a second after): 0 when the person has no comm art */
int comms_say(int person, int vdu, const char __far *text);
int comms_on(int vdu);                  /* a comm shows on this VDU now */
void comms_tick(void);                  /* once a frame: the mouth, the blinks, the end */

/* the comm into the run y0 .. y1-1 of VDU rect r: 1 when it drew it (the VDU's other content is not drawn) */
int comms_draw(int vdu, const RvRect __far *r, int y0, int y1);

#endif
