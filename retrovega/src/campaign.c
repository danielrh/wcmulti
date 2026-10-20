/* campaign.c: RVGAME.DAT, the campaign's moves, loading a mission's ships
 * into the slot tables the layer reads, and the bunks' saves.
 *
 * The campaign (docs/engine.md): every node has two ways out, good (towards
 * victory) and bad (towards loss).  After its four missions a node trends
 * good with 3-4 wins, bad with 0-1, and with 2 as the campaign's running
 * total says. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "rvhooks.h"
#include "campaign.h"

RvSave g_camp;
RvPilot g_pilots[RV_PILOTS];
RvNode g_nodes[RV_NODES];
unsigned g_npilots, g_nnodes, g_nmissions;
RvShipType __far g_shiptypes[RV_SHIPTYPES];
unsigned g_nshiptypes;
static RvGameHeader g_head;
static XmsBlock g_missions;

int camp_load_data(const char *path)
{
    FILE *f = fopen(path, "rb");
    static unsigned char buf[512];
    static unsigned char __far mission[sizeof(RvMission) + 2];
    unsigned i;
    if (!f)
        return 0;
    fread(&g_head, sizeof g_head, 1, f);
    if (memcmp(g_head.magic, RVGAME_MAGIC, 6) != 0 || g_head.version != RVGAME_VERSION
        || g_head.pilot_size != sizeof(RvPilot) || g_head.node_size != sizeof(RvNode)
        || g_head.mission_size != sizeof(RvMission)) {
        rv_log("game data: %s is not this build's (sizes %u %u %u)", path, g_head.pilot_size, g_head.node_size,
               g_head.mission_size);
        fclose(f);
        return 0;
    }
    RV_ASSERT(g_head.npilots <= RV_PILOTS && g_head.nnodes <= RV_NODES && g_head.nmissions <= RV_MISSIONS);
    g_npilots = g_head.npilots;
    g_nnodes = g_head.nnodes;
    g_nmissions = g_head.nmissions;
    fseek(f, g_head.pilots_at, SEEK_SET);
    fread(g_pilots, sizeof(RvPilot), g_npilots, f);
    fseek(f, g_head.nodes_at, SEEK_SET);
    fread(g_nodes, sizeof(RvNode), g_nnodes, f);
    g_missions = xms_alloc((unsigned long)sizeof(RvMission) * g_nmissions, XMS_RESIDENT);
    RV_ASSERT(g_missions != 0);
    fseek(f, g_head.missions_at, SEEK_SET);
    for (i = 0; i < g_nmissions; i++) {
        unsigned k;
        for (k = 0; k < sizeof(RvMission); k += sizeof buf) {
            unsigned part = sizeof(RvMission) - k < sizeof buf ? sizeof(RvMission) - k : sizeof buf;
            fread(buf, 1, part, f);
            _fmemcpy(mission + k, buf, part);
        }
        xms_put(g_missions, (unsigned long)i * sizeof(RvMission), mission, sizeof(RvMission));
    }
    RV_ASSERT(g_head.nshiptypes <= RV_SHIPTYPES && g_head.shiptype_size == sizeof(RvShipType));
    g_nshiptypes = g_head.nshiptypes;
    fseek(f, g_head.shiptypes_at, SEEK_SET);
    for (i = 0; i < g_nshiptypes; i++) {
        fread(buf, 1, sizeof(RvShipType), f);
        _fmemcpy(&g_shiptypes[i], buf, sizeof(RvShipType));
    }
    fclose(f);
    rv_log("game data: %u pilots, %u nodes, %u missions, %u ship types", g_npilots, g_nnodes, g_nmissions,
           g_nshiptypes);
    return 1;
}

const RvShipType __far *ship_type(unsigned type)
{
    unsigned i;
    for (i = 0; i < g_nshiptypes; i++)
        if (g_shiptypes[i].type == type)
            return &g_shiptypes[i];
    return 0;
}

void camp_new(void)
{
    unsigned i;
    memset(&g_camp, 0, sizeof g_camp);
    memcpy(g_camp.magic, SAVE_MAGIC, 6);
    g_camp.version = SAVE_VERSION;
    g_camp.size = sizeof g_camp;
    g_camp.node = (unsigned char)g_head.start_node;
    g_camp.player_ship = 0x03;           /* the Redeemer, until Kit has a word */
    g_camp.seed = ticks();
    for (i = 0; i < RV_PILOTS; i++)
        g_camp.pilot[i].alive = 1;
    campaignNode(g_camp.node, g_camp.mission);
}

unsigned camp_mission_index(void)
{
    RV_CHECK(g_camp.node, g_nnodes);
    RV_CHECK(g_camp.mission, RV_MISSIONS_PER_NODE);
    return g_nodes[g_camp.node].missions[g_camp.mission];
}

void camp_mission(unsigned i, RvMission __far *out)
{
    RV_CHECK(i, g_nmissions);
    xms_get(g_missions, (unsigned long)i * sizeof(RvMission), out, sizeof(RvMission));
}

/* the mission's ships into the tables the layer already reads (spec 4.4):
 * missionShipTable records of 0x2a bytes (+0 type, +2 side, +4 leader,
 * +6 behaviour, +8 nav point, +10 position x y z in km, +16 pilot), and
 * the nav points' records of 0x4d bytes (+0 id, +2 position). */
void camp_load_units(const RvMission __far *m)
{
    unsigned char *ships = missionShipTable, *navs = navPointTable;
    unsigned i, n = 0, k;
    memset(ships, 0, missionShipTable_SIZE * 64);
    memset(navs, 0, navPointTable_SIZE * 16);
    RV_ASSERT(m->nnav <= RV_NAVS && m->nship <= RV_SHIPS);
    for (i = 0; i < m->nnav; i++) {
        unsigned char *rec = navs + navPointTable_SIZE * i;
        rec[0] = m->nav[i].id;
        *(short *)(rec + 2) = m->nav[i].x;
        *(short *)(rec + 4) = m->nav[i].y;
        *(short *)(rec + 6) = m->nav[i].z;
    }
    for (i = 0; i < m->nship; i++) {
        const RvShip __far *s = &m->ship[i];
        for (k = 0; k < s->count && n < 64; k++, n++) {
            unsigned char *rec = ships + missionShipTable_SIZE * n;
            unsigned type = s->type == RV_SHIP_PLAYER || s->type == RV_SHIP_WINGMAN ? g_camp.player_ship : s->type;
            *(unsigned short *)(rec + 0) = type;
            *(unsigned short *)(rec + 2) = s->side;
            *(unsigned short *)(rec + 4) = s->type == RV_SHIP_PLAYER ? 0xFFFF : 0;
            *(unsigned short *)(rec + 6) = s->type == RV_SHIP_PLAYER ? 6 : s->type == RV_SHIP_WINGMAN ? 4 : 0;
            *(unsigned short *)(rec + 8) = s->nav;
            *(short *)(rec + 10) = (short)(k * 2);
            rec[16] = s->pilot;
        }
    }
    playerMissionShip = 0;
    missionStatus = 3;                   /* on the carrier */
    currentSeries = g_camp.node;
    currentMission = g_camp.mission;
    rv_log("mission %.16Fs: %u nav points, %u mission ships", m->id, (unsigned)m->nnav, n);
    missionLoaded(camp_mission_index());
}

int camp_resolve(int won)
{
    RvMission m;
    unsigned i, next;
    camp_mission(camp_mission_index(), &m);
    g_camp.flown++;
    g_camp.wins += won ? 1 : 0;
    g_camp.trend += won ? 1 : -1;
    for (i = 0; i < m.nship; i++)
        if (m.ship[i].pilot != RV_PILOT_NONE && m.ship[i].pilot < RV_PILOTS) {
            g_camp.pilot[m.ship[i].pilot].missions++;
            if (won)
                g_camp.pilot[m.ship[i].pilot].kills += 1 + (unsigned)(ticks() % 3);
        }
    missionResolved(won ? 1 : 0);
    if (++g_camp.mission < RV_MISSIONS_PER_NODE) {
        campaignNode(g_camp.node, g_camp.mission);
        return 0;
    }
    {
        const RvNode *n = &g_nodes[g_camp.node];
        int good = g_camp.wins >= 3 || (g_camp.wins == 2 && g_camp.trend >= 0);
        next = good ? n->good : n->bad;
        rv_log("node %.8s: %u wins, trend %d -> %s", n->id, g_camp.wins, g_camp.trend, good ? "good" : "bad");
    }
    g_camp.mission = 0;
    g_camp.wins = 0;
    if (next == RV_NODE_GOOD || next == RV_NODE_BAD) {
        g_camp.ending = next == RV_NODE_GOOD ? ENDING_GOOD : ENDING_BAD;
    } else {
        RV_CHECK(next, g_nnodes);
        g_camp.node = (unsigned char)next;
        campaignNode(g_camp.node, g_camp.mission);
    }
    return 1;
}

/* --- the bunks: BUNK01.SAV .. BUNK08.SAV --- */

static void bunk_path(int bunk, char *path)
{
    sprintf(path, "BUNK%02d.SAV", bunk + 1);
}

int save_write(int bunk, const char *name)
{
    char path[16];
    FILE *f;
    RV_CHECK(bunk, BUNKS);
    g_camp.bunk = (unsigned char)bunk;
    strncpy(g_camp.name, name, sizeof g_camp.name - 1);
    g_camp.name[sizeof g_camp.name - 1] = 0;
    bunk_path(bunk, path);
    f = fopen(path, "wb");
    if (!f)
        return 0;
    fwrite(&g_camp, sizeof g_camp, 1, f);
    fclose(f);
    rv_log("saved bunk %d '%s': node %u mission %u", bunk + 1, g_camp.name, g_camp.node, g_camp.mission);
    gameSaved(bunk);
    return 1;
}

int save_read(int bunk, RvSave __far *out)
{
    char path[16];
    FILE *f;
    static RvSave s;
    RV_CHECK(bunk, BUNKS);
    bunk_path(bunk, path);
    f = fopen(path, "rb");
    if (!f)
        return 0;
    if (fread(&s, sizeof s, 1, f) != 1)
        s.magic[0] = 0;
    _fmemcpy(out, &s, sizeof s);
    if (memcmp(s.magic, SAVE_MAGIC, 6) != 0
        || s.version != SAVE_VERSION || s.size != sizeof s || s.node >= g_nnodes
        || s.mission >= RV_MISSIONS_PER_NODE) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

int save_load(int bunk)
{
    static RvSave __far s;
    if (!save_read(bunk, &s))
        return 0;
    _fmemcpy(&g_camp, &s, sizeof s);
    rv_log("loaded bunk %d '%s': node %u mission %u", bunk + 1, g_camp.name, g_camp.node, g_camp.mission);
    gameLoaded(bunk);
    return 1;
}
