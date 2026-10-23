/* panel.h: the cockpit's panel (panel.c): every dial, lamp and readout showing the ship's systems.
 *
 * The user's ruling (2026-10-22): every dial, display and light of each cockpit does something, or is blanked;
 * the power-distribution knobs, which the game has no use for, repainted as a radio's frequency dials.  The table
 * is data/panels.json (docs/engine.md "The panel"); tools/panels.py makes each control's states from the kit's
 * art, RV.DAT keeps them (RvPanelControl), and here each frame's state of every control comes from the systems
 * (ShipSystems, kept by flight.c), a changed one marks its rows and is drawn over the base. */
#ifndef RV_PANEL_H
#define RV_PANEL_H

#include "cockpit.h"

enum { EMCON_OFF, EMCON_PASSIVE, EMCON_ACTIVE };
enum { SENSOR_RADAR, SENSOR_IR };
enum { CHAN_WING, CHAN_CARRIER, CHAN_GUARD };

/* the ship's systems as the panel shows and sets them (flight.c keeps them; flags combat will read are marked) */
typedef struct {
    unsigned char emcon;                /* EMCON_*: OFF dark (no scope, no lock, no running lights), PASSIVE (the
                                           scope unidentified, no friend-or-foe lock), ACTIVE (the radar on) */
    unsigned char sensor;               /* SENSOR_*: the lock's seeker (combat: radar-guided or IR missiles) */
    unsigned char iff;                  /* the IFF on: contacts identified (combat: friend-or-foe locks) */
    unsigned char armed;                /* master arm (combat: the guns fire only armed) */
    unsigned char muted;                /* comms muted: no comm faces, no hails */
    unsigned char hold;                 /* the lock held */
    unsigned char match, autotarget, autodecoy;   /* AUTO: match the target's speed, keep the nearest enemy
                                           targeted, decoys on a lock (combat) */
    unsigned char decoys;               /* left (combat: X drops one) */
    unsigned char radar_range;          /* 0 4,000 km, 1 8,000, 2 16,000 */
    unsigned char surv_page;            /* the survival display's buttons' page */
    unsigned char nav;                  /* the nav point chosen */
    unsigned char chan;                 /* the radio's channel (CHAN_*): the last that talked */
    unsigned char digit[3];             /* the frequency knobs as they stand (they step to the channel's) */
    unsigned char talking;              /* a transmission coming in */
    unsigned char lock, warn, burning, lights, autopilot_ready, target_hot;
    int target;                         /* its slot, -1 none */
    int heat, signature;                /* 0 .. 100 */
    int fuel;                           /* afterburner fuel, 0 .. 1000 */
    int hull;                           /* percent */
    long target_km, nav_km;             /* -1: none */
    char target_name[12];
} ShipSystems;
extern ShipSystems __far g_sys;

void panel_load(const RvCockpit __far *c);   /* its panel's controls (cockpit_load) */
void panel_unload(void);
void panel_dirty(const CockpitState *st);    /* each control's state: the rows of those that changed marked */
void panel_draw(const CockpitState *st, int y0, int y1);
int panel_at(int x, int y, int *fn, int *arg);   /* the control under a point: 1, its RV_PF_* and argument */

#endif
