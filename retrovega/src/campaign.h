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
    unsigned short missions, kills;     /* kills: the splash count */
    unsigned long medals;               /* a bit each */
    unsigned char alive, pad;
} RvPilotStats;

typedef struct {
    char magic[6];
    unsigned short version, size;
    unsigned char bunk;
    char name[24];
    unsigned char base;                 /* the ship the player lives on: 0 the carrier */
    unsigned char node, mission;        /* the campaign node, and the mission within it (0-3) */
    unsigned char wins;                 /* in this node */
    short trend;                        /* the campaign's running total: wins minus losses */
    unsigned char ending;               /* ENDING_* */
    unsigned char player_ship, livery;  /* ships.csv type, liveries.csv row */
    unsigned char pad;
    unsigned long seed;
    unsigned short flown;               /* missions flown in all */
    RvPilotStats pilot[RV_PILOTS];
} RvSave;
#pragma pack()

extern RvSave g_camp;
extern RvPilot g_pilots[RV_PILOTS];
extern RvNode g_nodes[RV_NODES];
extern unsigned g_npilots, g_nnodes, g_nmissions;

int camp_load_data(const char *path);
void camp_new(void);
unsigned camp_mission_index(void);              /* the mission to fly next */
void camp_mission(unsigned i, RvMission __far *out);
void camp_load_units(const RvMission __far *m);   /* missionShipTable and navPointTable, then missionLoaded */
int camp_resolve(int won);                      /* scores the mission; returns 1 when the node is done */
const char *camp_ending_text(void);

int save_write(int bunk, const char *name);
int save_read(int bunk, RvSave __far *out);     /* 0: empty or not a save */
int save_load(int bunk);

#endif
