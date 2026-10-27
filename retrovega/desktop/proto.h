/* proto.h: what rv-core (core.c, the game) and rv (front.c, its window) say to each other over their two pipes
 * (desktop/README.md).  Each message is an RvpHead and len bytes.  The layouts are the same in the 32-bit core and
 * the 64-bit front end (checked below). */
#ifndef RV_PROTO_H
#define RV_PROTO_H

#include <stdint.h>

enum {
    /* the core's, to the front end */
    RVP_PALETTE = 'P',                  /* 768 bytes: the DAC's 256 colours, 6 bits a component */
    RVP_FRAME = 'F',                    /* 256000 bytes: the screen, 640 x 400, a palette index a pixel */
    RVP_OPL = 'O',                      /* RvpOpl records: the OPL3's register writes, each with its time */
    RVP_WROTE = 'W',                    /* a file's name: the game opened it to write (the first time) */
    RVP_END = 'E',                      /* 4 bytes: the game's exit status; nothing follows */
    /* the front end's, to the core */
    RVP_KEY = 'K',                      /* an RvpKey */
    RVP_MOUSE = 'M'                     /* an RvpMouse */
};

typedef struct { uint8_t type, pad[3]; uint32_t len; } RvpHead;
typedef struct { uint64_t at_us; uint16_t reg; uint8_t val, pad[5]; } RvpOpl;   /* at_us: CLOCK_MONOTONIC */
typedef struct { uint16_t scan; uint8_t down, pad; } RvpKey;   /* scan: the PC's set 1 code (0x100 | it: E0) */
typedef struct { int16_t x, y; uint8_t kind, button, down, pad; } RvpMouse;   /* kind: RVPM_* */
enum { RVPM_MOVE, RVPM_BUTTON, RVPM_TAP };   /* a tap: a finger's press, counted as a click, never held */

#define RVP_W 640
#define RVP_H 400
#define RVP_MAX (RVP_W * RVP_H)         /* the largest message (a frame) */

_Static_assert(sizeof(RvpHead) == 8, "RvpHead");
_Static_assert(sizeof(RvpOpl) == 16, "RvpOpl");
_Static_assert(sizeof(RvpKey) == 4, "RvpKey");
_Static_assert(sizeof(RvpMouse) == 8, "RvpMouse");

#endif
