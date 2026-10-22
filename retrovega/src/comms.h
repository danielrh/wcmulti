/* comms.h: the comm link on a cockpit VDU (comms.c).
 *
 * A speaker's face over a grainy green-and-white link (the user's brief, 2026-10-22): every human pilot in the
 * same helmet, their own face behind its visor, their callsign on it, the eyes blinking and glancing, the
 * regulator's light lit as they speak (or, built RV_COMMS=face, the bare face's mouth speaking the line in
 * lipsync.c's shapes); the Aera and Rlaan pilots anonymous, each in a helmet of their own, their lights for eyes
 * and their speech lights doing the same.  The art is tools/comms.py's, in RV.DAT: its voices (comm_cast), each
 * a base picture (the pilots' shared helmet, or an alien's own; cols x 40 in the link's shades), a window over it
 * (what differs from pilot to pilot) and patches after that (eyes half, shut, left, right; then its mouths), each
 * patch only the pixels that change.
 *
 * The picture is put together once when a comm starts, in the flight palette's entries with the scan lines in
 * (each odd row through a darker ramp), and patched when the eyes or the mouth change; a frame copies its rows to
 * the VDU (an XMS move a row) and sprinkles a few grains (a shade up or down).  Centred on the VDU: cropped where
 * it is smaller, the link's background round it where it is bigger. */
#ifndef RV_COMMS_H
#define RV_COMMS_H

#include "rvasset.h"

#define COMMS_W 112
#define COMMS_H 80

void comms_flight_start(void);          /* the voices, the picture's buffer and the ramp (with the flight's palette in) */
void comms_flight_end(void);

int comms_voices(void);                 /* how many voices */
int comms_voice(int person);            /* RV.DAT's person's (people.csv's order) voice, -1 none */
int comms_voice_for(int side, int type);   /* an alien of side (1 Aera, 2 Rlaan) flying ship type `type`, else any
                                           of that side; -1 none */

/* voice on VDU vdu (0 left, 1 right) saying text (NULL: its own line; the mouth or the lights follow it, the comm
 * ends a second after): 0 when there is no such voice */
int comms_say(int voice, int vdu, const char __far *text);
int comms_on(int vdu);                  /* a comm shows on this VDU now */
void comms_tick(void);                  /* once a frame: the mouth, the blinks, the end */

/* the comm into the run y0 .. y1-1 of VDU rect r: 1 when it drew it (the VDU's other content is not drawn) */
int comms_draw(int vdu, const RvRect __far *r, int y0, int y1);

void comms_bench(void);                 /* what a comm costs, timed, into the log (RV.EXE /commdemo) */

#endif
