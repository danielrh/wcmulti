/* rocks.h: asteroid fields (rocks.c; the per-rock work in rocks.asm).
 *
 * A field is a hazard a nav point may have instead of ships (the user,
 * 2026-10-23): a sphere of space (its middle, its radius) full of tumbling
 * rocks at some density.  The rocks are not ship slots: each machine keeps
 * its own fixed set of them round its own player (as the hook layer has
 * WC1's rocks: every player's field is their own, nothing about a rock is
 * sent), in a bubble about the camera; a rock the player has left behind
 * (out of the bubble) comes back on the bubble's far side ahead, where it
 * grows in from nothing.  The art (tools/rocks.py's ROCKS.DAT, four kinds,
 * 32 tumble frames, every size to 8 px, then a ladder to 128) is read into
 * XMS (its own level, above the mission's) only while the player is near
 * a field, and given back when they leave.  A rock never turns with the
 * camera's roll: it only tumbles and scales.  A row of a big rock is one
 * XMS move straight into the band buffer; a small one (8 px at most) comes
 * from conventional memory as a ball does; one bigger than the ladder is
 * drawn as 2 x 2 .. 4 x 4 blocks of a ladder sprite.
 *
 * The fields are kept where the hook layer looks (rvabi.def): fieldTable,
 * fieldCount, currentField, fieldRockTally; each field has a slot of its own
 * above the ships (EntityType 9, rvFlags bit 2: a local-only hazard) that a
 * rock's hit names as its source in do_damage; clearFields takes them all
 * away (the layer's "rocks off"). */
#ifndef ROCKS_H
#define ROCKS_H

#include "lasers.h"

/* a rock in the world (16 bytes; the asm reads this layout) */
typedef struct {
    long x, y, z;                       /* 0: its middle, positions (256 a km) */
    unsigned char kind;                 /* 12: ROCKS.DAT's kind */
    unsigned char r8;                   /* 13: its radius, 8 km a step */
    unsigned char phase;                /* 14: its tumble's frame when anim is 0 */
    signed char spin;                   /* 15: quarter frames of tumble a step of anim, either way */
} LzRock;

/* the camera's rock terms (rocks.c fills them; 40 bytes, the asm reads this layout).  Camera units as LzCam's
 * (4,096 a km); a rock's distance is taken in km, squared */
typedef struct {
    unsigned long dk;                   /* 0: its diameter on screen x16 = r8 x dk / Z (a 64-bit product) */
    unsigned long far2;                 /* 4: the bubble's radius squared (km^2): a rock past it is reaped */
    unsigned long fade2;                /* 8: past this (km^2) a rock shrinks, to nothing at far2 */
    long reach;                         /* 12: within this and the rock's radius on every axis (camera units): the
                                           hit list (the player's half box, a little more) */
    unsigned short anim;                /* 16: the animation's step (the frame counter) */
    unsigned short mask;                /* 18: the tumble's frames less 1 */
    short dmax;                         /* 20: the largest diameter drawn */
    short maxreap, maxclose;            /* 22: room in the two lists */
    short nreap, nclose;                /* 26: (out) how many went in each */
    short pad[5];                       /* 30 */
} LzRockCam;

/* a rock on screen (12 bytes; the asm writes this layout) */
typedef struct {
    short x, y;                         /* 0: its middle, pixels */
    unsigned short d;                   /* 4: its diameter, pixels (1 .. dmax) */
    unsigned char kind, frame;          /* 6 */
    long z;                             /* 8: its depth, camera units */
} LzRockOut;

/* every rock through the camera: the ones on screen to out (at most maxout; their number returned), the ones out
 * of the bubble (or 2^20 sixteenths of a km away on an axis) to reap and the ones within reach to close, as
 * indices (rc's counts) */
int lz_rocks_place(const LzRock LZ_FAR *r, int n, const LzCam LZ_FAR *c, LzRockCam LZ_FAR *rc, LzRockOut LZ_FAR *out,
                   int maxout, short LZ_FAR *reap, short LZ_FAR *hitl);
int __far __cdecl rv_rocks_place(const LzRock __far *r, int n, const LzCam __far *c, LzRockCam __far *rc,
                                 LzRockOut __far *out, int maxout, short __far *reap, short __far *hitl);

/* one big rock's rows that fall in rows y0 .. y1-1 of the band (cut by the caller to the extents'), each clipped
 * to its row's extent: hdr is its record's row pairs (left + 1, length: rocks.py), (x, y) its top left on screen.
 * The XMS way: each row one move from (handle, off: its first row's pixels) into the band; the copy way: from
 * pix, a copy of its pixels, k x k blocks a pixel (k 1 .. 4) */
void __far __cdecl rv_rocks_xms(const unsigned char __far *hdr, int rows, int x, int y, unsigned handle,
                                unsigned long off, unsigned char __far *band, int band_y0, int y0, int y1,
                                const short __far *ext, int ext_y0);
void __far __cdecl rv_rocks_copy(const unsigned char __far *hdr, int rows, int x, int y, int k,
                                 const unsigned char __far *pix, unsigned char __far *band, int band_y0, int y0,
                                 int y1, const short __far *ext, int ext_y0);

#define ROCK_DENSITY 120                /* a field's rocks per (10,000 km)^3 when the mission does not say */

int rocks_field_add(int type, long x, long y, long z, long radius_km, int density);   /* -> its index, -1 none */
void rocks_flight_start(void);          /* no fields, no rocks */
void rocks_flight_end(void);            /* every field's rocks and art given back */
void rocks_want(int on);                /* the flight will want ROCKS.DAT (asked of the page with the ships' sets) */
const char *rocks_file(void);           /* "ROCKS.DAT" when wanted, else 0 */
unsigned long rocks_room_kb(void);      /* the XMS the rocks will want near a field (0 when no field is wanted): the
                                           ship sets leave it free */

/* once a frame, after the simulation step: the fields entered and left (the art read in and given back), every
 * rock through the camera (the player's frame), the ones left behind brought round ahead, the player's hits */
void rocks_frame(const LfBasis *cam, int cx, int cy, int focal, int y0, int y1);
int rocks_on_screen(int *small, int *big);   /* this frame's */
int rocks_live(void);                   /* rocks kept round the player now */

/* per band: the small (far) ones, before the ships; the big ones in depth order with them (rocks_draw_to: those
 * deeper than `depth`, in positions, not drawn yet; then rocks_draw_rest) */
void rocks_draw_small(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);
void rocks_draw_begin(void);
void rocks_draw_to(long depth, int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);
void rocks_draw_rest(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

void rocks_report(void);                /* the flight's tally (logged) */
extern int g_rock_xms;                  /* RV.EXE /rockxms 0: the big rocks' rows copied from conventional memory
                                           (one XMS move a rock, then the copy) rather than moved a row at a time */
#ifdef RV_TESTS
long rocks_selftest(void);              /* the asm's placing against the C's: -1 on a mismatch, else rocks compared */
#endif

#endif
