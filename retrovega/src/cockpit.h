/* cockpit.h: the player's cockpit on screen (cockpit.c) */
#ifndef COCKPIT_H
#define COCKPIT_H

#include "rv.h"

#define CP_CONTACTS 16

typedef struct {
    int shield_f, shield_r;             /* percent */
    int armor[4];                       /* front, rear, left, right: percent */
    int speed, set_speed, max_speed;    /* kps */
    int fuel;                           /* percent */
    int stick_x, stick_y;               /* -1, 0, 1: the joystick's eight ways and the middle */
    unsigned long damage;               /* rvCockpitDamage: a bit per overlay */
    int lock, warn;                     /* the lamps */
    int ncontacts;
    struct { short x, y; unsigned char colour; } contact[CP_CONTACTS];   /* radar: 256 = the rim */
    const char *mfd_text[2][4];         /* lines on each MFD (0 left, 1 right), NULL ends */
    void (*mfd_draw)(int which, const RvRect __far *r, int y0, int y1);   /* drawn before the text, or NULL */
} CockpitState;

int cockpit_load(int ship_type);        /* the cockpit for a ship type; sets its palette. 0: none */
void cockpit_unload(void);
const RvCockpit __far *cockpit_record(void);
const short __far *cockpit_extents(int *y0, int *rows);   /* (first, end) x per row of the opening */
void cockpit_dirty(const CockpitState *st);               /* marks what must be redrawn for this state */
void cockpit_draw(int y0, int y1, const CockpitState *st, void (*world)(int y0, int y1));

#endif
