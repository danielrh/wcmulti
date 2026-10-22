/* dat.c: a base's archive: its directory, rooms, people and cockpits read
 * into conventional memory when it opens; an asset's bytes read from the
 * file the first time they are wanted, into XMS at the level of the moment
 * (dat_enter: a room's while in the rooms, the mission's in flight), and
 * kept there until that level is given back.  So the briefing room's
 * pictures are not held through a flight, and a flight holds its one
 * cockpit, not all eight (the user, 2026-10-22: the archive whole in XMS was
 * 11 MB of a 32 MB arena).  The palettes, fonts and small tables everything
 * uses are read when it opens, at the base level (a level may not be taken
 * below one in use: they could not come later).  DOSBox serves a read at
 * host speed: a 250 KB picture is a few ms.
 *
 * A base is a ship the player lives on (docs/engine.md): base 0, the
 * carrier, is RV.DAT; base N is RVBASEN.DAT, the same rooms with its own
 * pictures.  dat_close gives back everything the archive took. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <io.h>
#include <fcntl.h>
#include "rv.h"

#define SLAB 65536UL                    /* assets are carved from blocks of this; one over half of it gets its own */

static int g_fd = -1;
static RvDatHeader g_hdr;
static RvAsset __far *g_dir;
static RvRoom __far *g_rooms;
static RvPerson __far *g_people;
static RvCockpit __far *g_cockpits;
static struct { XmsBlock blk; unsigned short off; } __far *g_in;   /* each asset's place in XMS (blk 0: not in) */
static struct { XmsBlock blk; unsigned long used; } __far g_slab[XMS_LEVELS];
static int g_level = XMS_ROOM;          /* where an asset wanted now goes */
static unsigned long g_read_bytes, g_reads;   /* (the tally) */

/* n bytes of the file from `at` into dst, through the near file buffer */
static int file_read(unsigned long at, void __far *dst, unsigned long n)
{
    unsigned char __far *d = (unsigned char __far *)dst;
    if (g_fd < 0 || lseek(g_fd, at, SEEK_SET) != (long)at)
        return 0;
    while (n) {
        unsigned part = n < sizeof rv_filebuf ? (unsigned)n : sizeof rv_filebuf;
        if (read(g_fd, rv_filebuf, part) != (int)part)
            return 0;
        _fmemcpy(d, rv_filebuf, part);
        d += part;
        n -= part;
    }
    return 1;
}

/* the same into XMS (blk, off): an odd tail rounds up into the padding the caller left */
static int file_to_xms(unsigned long at, XmsBlock blk, unsigned long off, unsigned long n)
{
    if (g_fd < 0 || lseek(g_fd, at, SEEK_SET) != (long)at)
        return 0;
    while (n) {
        unsigned part = n < sizeof rv_filebuf ? (unsigned)n : sizeof rv_filebuf;
        if (read(g_fd, rv_filebuf, part) != (int)part)
            return 0;
        xms_put(blk, off, rv_filebuf, (part + 1) & ~1U);
        off += part;
        n -= part;
    }
    return 1;
}

/* the blocks past the first nblocks were given back (xms_free_level): every asset in them is out again */
static void freed(unsigned nblocks)
{
    unsigned i;
    if (!g_in)
        return;
    for (i = 0; i < g_hdr.nassets; i++)
        if (g_in[i].blk > nblocks)
            g_in[i].blk = 0;
    for (i = 0; i < XMS_LEVELS; i++)
        if (g_slab[i].blk > nblocks)
            g_slab[i].blk = 0;
}

/* asset i in XMS at `level`: read now if it is not */
static int bring_in(unsigned i, int level)
{
    const RvAsset __far *a = &g_dir[i];
    unsigned long size = (a->size + 1) & ~1UL;
    XmsBlock blk;
    unsigned long off;
    if (g_in[i].blk)
        return 1;
    if (size > SLAB / 2) {
        blk = xms_alloc(size, level);
        off = 0;
    } else {
        if (!g_slab[level].blk || g_slab[level].used + size > SLAB) {
            g_slab[level].blk = xms_alloc(SLAB, level);
            g_slab[level].used = 0;
        }
        blk = g_slab[level].blk;
        off = g_slab[level].used;
        g_slab[level].used += size;
    }
    if (!blk || !file_to_xms(a->at, blk, off, a->size)) {
        rv_log("dat: asset %u (%.22Fs): %s", i, (const char __far *)a->name, blk ? "the read failed" : "no XMS");
        return 0;
    }
    g_in[i].blk = blk;
    g_in[i].off = (unsigned short)off;
    g_reads++;
    g_read_bytes += a->size;
    return 1;
}

/* the asset whose bytes hold file offset `at`: the last one starting at or before it (they lie in directory order) */
static int asset_at(unsigned long at)
{
    int lo = 0, hi = (int)g_hdr.nassets - 1, mid;
    if (!g_hdr.nassets || at < g_dir[0].at)
        return -1;
    while (lo < hi) {
        mid = (lo + hi + 1) / 2;
        if (g_dir[mid].at <= at)
            lo = mid;
        else
            hi = mid - 1;
    }
    return at < g_dir[lo].at + g_dir[lo].size ? lo : -1;
}

void dat_read(unsigned long at, void __far *dst, unsigned long n)
{
    int i = asset_at(at);
    if (i >= 0 && at + n <= g_dir[i].at + g_dir[i].size && bring_in(i, g_level))
        xms_get(g_in[i].blk, g_in[i].off + (at - g_dir[i].at), dst, n);
    else
        file_read(at, dst, n);          /* (outside an asset, or no room: straight from the file) */
}

void dat_enter(int level)
{
    xms_free_level(level);
    g_level = level;
}

void dat_log(void)
{
    static const char __far s[] = "dat: %lu assets read in, %lu KB (the level now %d)";
    rv_logf(s, g_reads, g_read_bytes / 1024, g_level);
}

int dat_open(const char *path)
{
    unsigned long pinned = 0;
    unsigned i;
    g_fd = open(path, O_RDONLY | O_BINARY);
    if (g_fd < 0) {
        rv_log("dat: no %s", path);
        return 0;
    }
    g_hdr.nassets = 0;
    if (!file_read(0, &g_hdr, sizeof g_hdr) || memcmp(g_hdr.magic, "RVDAT", 5) != 0
        || g_hdr.version != RVDAT_VERSION || g_hdr.room_size != sizeof(RvRoom) || g_hdr.person_size != sizeof(RvPerson)
        || (g_hdr.ncockpits && g_hdr.cockpit_size != sizeof(RvCockpit))) {
        rv_log("dat: %s is not this build's RV.DAT (version %u, room %u/%u, person %u/%u, cockpit %u/%u)", path,
               g_hdr.version, g_hdr.room_size, (unsigned)sizeof(RvRoom), g_hdr.person_size, (unsigned)sizeof(RvPerson),
               g_hdr.cockpit_size, (unsigned)sizeof(RvCockpit));
        g_hdr.nassets = 0;
        return 0;
    }
    g_dir = (RvAsset __far *)_fmalloc(sizeof(RvAsset) * g_hdr.nassets);
    g_rooms = (RvRoom __far *)_fmalloc(sizeof(RvRoom) * g_hdr.nrooms);
    g_people = (RvPerson __far *)_fmalloc(sizeof(RvPerson) * g_hdr.npeople);
    g_cockpits = (RvCockpit __far *)_fmalloc(sizeof(RvCockpit) * (g_hdr.ncockpits ? g_hdr.ncockpits : 1));
    g_in = _fmalloc(sizeof *g_in * (unsigned long)g_hdr.nassets);
    if (!g_dir || !g_rooms || !g_people || !g_cockpits || !g_in)
        return 0;
    _fmemset(g_in, 0, sizeof *g_in * g_hdr.nassets);
    _fmemset(g_slab, 0, sizeof g_slab);
    file_read(g_hdr.dir_at, g_dir, sizeof(RvAsset) * (unsigned long)g_hdr.nassets);
    file_read(g_hdr.rooms_at, g_rooms, sizeof(RvRoom) * (unsigned long)g_hdr.nrooms);
    file_read(g_hdr.people_at, g_people, sizeof(RvPerson) * (unsigned long)g_hdr.npeople);
    if (g_hdr.ncockpits)
        file_read(g_hdr.cockpits_at, g_cockpits, sizeof(RvCockpit) * (unsigned long)g_hdr.ncockpits);
    for (i = 1; i < g_hdr.nassets; i++)
        RV_ASSERT(g_dir[i].at >= g_dir[i - 1].at);   /* (in file order: asset_at searches them) */
    xms_freed = freed;
    for (i = 0; i < g_hdr.nassets; i++)  /* the palettes, fonts and tables: in now, at the base level */
        if (g_dir[i].kind != RV_KIND_IMG && g_dir[i].kind != RV_KIND_SPR && bring_in(i, XMS_BASE))
            pinned += g_dir[i].size;
    rv_log("dat: %s, %u assets, %u rooms, %u people, %u cockpits; %lu KB of palettes, fonts and tables in XMS, the "
           "rest read as wanted", path, g_hdr.nassets, g_hdr.nrooms, g_hdr.npeople, g_hdr.ncockpits, pinned / 1024);
    return 1;
}

void dat_close(void)
{
    xms_free_level(XMS_BASE);           /* (first: freed() clears what was in) */
    if (g_dir) _ffree(g_dir);
    if (g_rooms) _ffree(g_rooms);
    if (g_people) _ffree(g_people);
    if (g_cockpits) _ffree(g_cockpits);
    if (g_in) _ffree(g_in);
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
    g_dir = 0;
    g_rooms = 0;
    g_people = 0;
    g_cockpits = 0;
    g_in = 0;
    g_hdr.nassets = g_hdr.nrooms = g_hdr.npeople = g_hdr.ncockpits = 0;
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

const RvCockpit __far *dat_cockpit(unsigned i)
{
    RV_CHECK(i, g_hdr.ncockpits);
    return &g_cockpits[i];
}
