/* room.h: a room on screen (room.c) */
#ifndef ROOM_H
#define ROOM_H

#include "rvhooks.h"

#define LABEL_H 20                      /* the hover label's strip at the bottom */
enum { HOVER_NONE, HOVER_SEAT, HOVER_ACTOR, HOVER_HOT };
enum { ACTION_NONE, ACTION_HOT, ACTION_SEAT, ACTION_ACTOR };

typedef struct {
    int kind, index;                    /* ACTION_*, and which seat / hotspot */
    int action, arg;                    /* a hotspot's RV_ACT_* and its room; a person's row */
} SceneAction;

typedef struct {
    int room;
    unsigned char seat[3];              /* people.csv row per seat, 0xFF empty */
    unsigned char turn[3];              /* 0 talk pose, 1 between, 2 facing the player */
    int facing;                         /* 1-3: that seat faces the player anyway; 9: the actor */
    unsigned long turn_next;
    int actor_frame, actor_step;
    unsigned long actor_next, glance_at;
    int hover_kind, hover_index;
    int px, py, pointer, hide_pointer;
    char label[40];
    void (*underlay)(int y0, int y1);   /* room-specific pictures, under the people */
    void (*overlay)(int y0, int y1);    /* the game's text and panels, over everything but the pointer */
    void (*full)(int y0, int y1);       /* set: the game draws the whole screen (close-ups, endings) */
    int no_room;                        /* no room's hotspots or people: only the game's screen */
} Scene;

extern Scene g_scene;
extern char __far g_people_names[16][28];     /* "Name (Callsign)" per people.csv row, for labels */

void scene_enter(int room, const unsigned char *seats);
int scene_update(const Input *in, SceneAction *act);
void scene_draw(void);

#endif
