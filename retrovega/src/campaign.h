/* campaign.h: the campaign, its missions and the bunks' saves (campaign.c) */
#ifndef CAMPAIGN_H
#define CAMPAIGN_H

#include "rvdata.h"

#define BUNKS 8
#define SAVE_MAGIC "RVSAVE"
#define SAVE_VERSION 1
enum { ENDING_NONE, ENDING_GOOD, ENDING_BAD };

#pragma pack(1)
typedef struct {
    uint16_t missions, kills;           /* kills: the splash count */
    uint32_t medals;                    /* a bit each */
    uint8_t alive, pad;
} RvPilotStats;

typedef struct {
    char magic[6];
    uint16_t version, size;
    uint8_t bunk;
    char name[24];
    uint8_t base;                       /* the ship the player lives on: 0 the carrier */
    uint8_t node, mission;              /* the campaign node, and the mission within it (0-3) */
    uint8_t wins;                       /* in this node */
    int16_t trend;                      /* the campaign's running total: wins minus losses */
    uint8_t ending;                     /* ENDING_* */
    uint8_t player_ship, livery;        /* ships.csv type, liveries.csv row */
    uint8_t pad;
    uint32_t seed;
    uint16_t flown;                     /* missions flown in all */
    RvPilotStats pilot[RV_PILOTS];
} RvSave;
#pragma pack()

extern RvSave g_camp;
extern RvPilot g_pilots[RV_PILOTS];
extern RvNode g_nodes[RV_NODES];
extern uint16_t g_npilots, g_nnodes, g_nmissions;
extern RvShipType __far g_shiptypes[RV_SHIPTYPES];
extern uint16_t g_nshiptypes;
const RvShipType __far *ship_type(uint16_t type);   /* data/ships.csv's row for a type, or NULL */
extern RvWeapon __far g_weapons[RV_WEAPONS];
extern uint16_t g_nweapons;
const RvWeapon __far *weapon(uint16_t type);    /* data/weapons.csv's row for a weapon type (a gun record's), or NULL */

int16_t camp_load_data(const char *path);
void camp_new(void);
uint16_t camp_mission_index(void);              /* the mission to fly next */
void camp_mission(uint16_t i, RvMission __far *out);
void camp_load_units(const RvMission __far *m);   /* missionShipTable and navPointTable, then missionLoaded */
int16_t camp_resolve(int16_t won);                      /* scores the mission; returns 1 when the node is done */
const char *camp_ending_text(void);

int16_t save_write(int16_t bunk, const char *name);
int16_t save_read(int16_t bunk, RvSave __far *out);     /* 0: empty or not a save */
int16_t save_load(int16_t bunk);

#endif
