/* dat.c: a base's archive, read whole into XMS (the base level); its
 * directory, rooms and people kept in conventional memory.  An asset's bytes
 * stay in XMS at their file offset and are moved out as they are drawn.
 *
 * A base is a ship the player lives on (docs/engine.md): base 0, the
 * carrier, is RV.DAT; base N is RVBASEN.DAT, the same rooms with its own
 * pictures.  dat_close gives back everything the archive took. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "rv.h"

#define CHUNK 32768U

static XmsBlock g_blk;
static RvDatHeader g_hdr;
static RvAsset __far *g_dir;
static RvRoom __far *g_rooms;
static RvPerson __far *g_people;

void dat_read(unsigned long at, void __far *dst, unsigned long n)
{
    xms_get(g_blk, at, dst, n);
}

int dat_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char __far *buf;
    unsigned long size, at = 0;
    if (!f) {
        rv_log("dat: no %s", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_blk = xms_alloc(size + 2, XMS_BASE);
    buf = (unsigned char __far *)_fmalloc(CHUNK);
    if (!g_blk || !buf) {
        rv_log("dat: no room for %lu bytes", size);
        return 0;
    }
    while (at < size) {
        unsigned n = (unsigned)(size - at < CHUNK ? size - at : CHUNK);
        unsigned k;
        for (k = 0; k < n; k += 1024) {
            unsigned part = n - k < 1024 ? n - k : 1024;
            static unsigned char tmp[1024];
            fread(tmp, 1, part, f);
            _fmemcpy(buf + k, tmp, part);
        }
        xms_put(g_blk, at, buf, (n + 1) & ~1U);
        at += n;
    }
    fclose(f);
    _ffree(buf);
    dat_read(0, &g_hdr, sizeof g_hdr);
    if (memcmp(g_hdr.magic, "RVDAT", 5) != 0 || g_hdr.version != RVDAT_VERSION
        || g_hdr.room_size != sizeof(RvRoom) || g_hdr.person_size != sizeof(RvPerson)) {
        rv_log("dat: %s is not this build's RV.DAT (version %u, room %u/%u, person %u/%u)", path, g_hdr.version,
               g_hdr.room_size, (unsigned)sizeof(RvRoom), g_hdr.person_size, (unsigned)sizeof(RvPerson));
        return 0;
    }
    g_dir = (RvAsset __far *)_fmalloc(sizeof(RvAsset) * g_hdr.nassets);
    g_rooms = (RvRoom __far *)_fmalloc(sizeof(RvRoom) * g_hdr.nrooms);
    g_people = (RvPerson __far *)_fmalloc(sizeof(RvPerson) * g_hdr.npeople);
    if (!g_dir || !g_rooms || !g_people)
        return 0;
    dat_read(g_hdr.dir_at, g_dir, (sizeof(RvAsset) * (unsigned long)g_hdr.nassets + 1) & ~1UL);
    dat_read(g_hdr.rooms_at, g_rooms, (sizeof(RvRoom) * (unsigned long)g_hdr.nrooms + 1) & ~1UL);
    dat_read(g_hdr.people_at, g_people, (sizeof(RvPerson) * (unsigned long)g_hdr.npeople + 1) & ~1UL);
    rv_log("dat: %s, %lu bytes in XMS, %u assets, %u rooms, %u people", path, size, g_hdr.nassets,
           g_hdr.nrooms, g_hdr.npeople);
    return 1;
}

void dat_close(void)
{
    if (g_dir) _ffree(g_dir);
    if (g_rooms) _ffree(g_rooms);
    if (g_people) _ffree(g_people);
    g_dir = 0;
    g_rooms = 0;
    g_people = 0;
    g_hdr.nassets = g_hdr.nrooms = g_hdr.npeople = 0;
    xms_free_level(XMS_BASE);
    g_blk = 0;
}

const char *dat_base_file(int base)
{
    static char name[16];
    if (base == 0)
        return "RV.DAT";
    sprintf(name, "RVBASE%d.DAT", base);
    return name;
}

const RvDatHeader __far *dat_header(void) { return &g_hdr; }

const RvAsset __far *dat_asset(unsigned i)
{
    RV_CHECK(i, g_hdr.nassets);
    return &g_dir[i];
}

const RvRoom __far *dat_room(unsigned i)
{
    RV_CHECK(i, g_hdr.nrooms);
    return &g_rooms[i];
}

const RvPerson __far *dat_person(unsigned i)
{
    RV_CHECK(i, g_hdr.npeople);
    return &g_people[i];
}
