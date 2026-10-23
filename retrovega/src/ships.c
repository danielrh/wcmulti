/* ships.c: a ship type's views in XMS, and the draw of one (ships.h).
 *
 * The flight draws from pre-rendered sets, SHIPnn.RVS (tools/rvsprites.c,
 * made on the host from SHIPnn.RV, the "compressed" form: 37 or 62 views at
 * full size): every view at 13 size steps, and at roll steps of 22.5
 * degrees for the largest sizes, already in gfx.c's run-length row format,
 * so a block of the file goes into XMS by a straight copy and every draw is
 * a copy of rows.  A symmetric ship stores only the arc 0..90 degrees (five
 * of the sixteen steps): an upside-down copy (the rows in the other order)
 * turns a sprite by 180 - theta and mirrors its heading, a mirrored copy
 * (each row backwards, rv_blit_row_rev) turns it by -theta and mirrors the
 * heading, and both together turn it by 180; so the other eleven steps are
 * those copies of the stored five, of this view or of the opposite
 * heading's.  The set's index (a view table per block) sits in XMS too and
 * is fetched twelve bytes at a time.  A mission opens its types' sets,
 * then ships_load_blocks fills the ladder for every type and the rolls by
 * size while the budget lasts; a draw that finds its block missing takes
 * the unturned one at that size.
 *
 * The full-size block (the exact draw's views, the densest lattice: every
 * 15 degrees) is loaded too, after the ladder and before the rolls: a
 * fighter type's is 0.2 to 1.5 MB.  Nothing is read from the disk in
 * flight (the user, 2026-10-21: the stream that read these views a few a
 * frame into six slots was complicated, it thrashed, and the disk is for
 * comm images and the like); a type whose full-size block did not fit is
 * drawn from its sets.
 *
 * Without a .RVS beside RV.EXE a type falls back to SHIPnn.RV decoded here
 * (full size only, scaled or turned at draw time through the lightfield
 * kernels, which costs nine times a copy): the page runs that way until its
 * JavaScript makes the sets. */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <io.h>
#include <fcntl.h>
#include <dos.h>
#include "ships.h"
#include "lightfield.h"
#include "music.h"

#define SCRATCH 32768U                  /* gfx.c's scratch: a view's raw bytes, then the rows going out */
#define LINE 1024
#define BAND_ROWS 12                    /* rows decoded at a time */
#define OUT_MIN 4096U                   /* the least room kept for the rows going out */

static ShipSet __far *g_set[SHIP_SETS];
static int g_nsets;
static unsigned char __far *g_band;     /* BAND_ROWS x LINE pixels, 0 = clear (no view uses colour 0) */

/* the camera */
static int g_cx = SCR_W / 2, g_cy = SCR_H / 2, g_focal = 400;

/* two views expanded dense (w x h, pitch w) for the exact draw: the nearest
 * ships keep theirs from frame to frame */
#define DENSE_MAX 65024U
#define DENSE_BUFS SHIPS_DENSE_BUFS
static unsigned char __far *g_dense_buf[DENSE_BUFS];
static int g_dense_set[DENSE_BUFS] = { -1, -1 }, g_dense_view[DENSE_BUFS] = { -1, -1 };
static unsigned g_dense_used[DENSE_BUFS], g_dense_tick;
static unsigned char __far *g_dense;    /* the one a draw is using */
#define DCACHE 8                        /* the dense copies kept in XMS (dense_from) */
static struct { short set, key; unsigned used; XmsBlock blk; } __far g_dc[DCACHE];
static int g_ndc;
static unsigned long g_dc_hits, g_dc_expands;   /* (the flight's tally) */
static unsigned short __far *g_cmap;    /* the scaled draw's column map, LINE entries */
static unsigned char __far *g_line;     /* one source row expanded, LINE pixels */


void __far __cdecl rv_blit_row(unsigned char __far *row, const unsigned char __far *spans, int left, int xa, int xb);
void __far __cdecl rv_blit_row_rev(unsigned char __far *row, const unsigned char __far *spans, int right, int xa, int xb);
void __far __cdecl rv_scale_row(const unsigned char __far *line, const unsigned short __far *map, int xa, int xb,
                                unsigned char __far *out);
void __far __cdecl rv_rotate_row(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                 unsigned char __far *out);
void __far __cdecl rv_rotate_row2(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                  unsigned char __far *out);
long __far __cdecl rv_rotate_edges(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs,
                                   int xe);           /* (the silhouette's first column, low word; one past its last) */
long __far __cdecl rv_turn_drawn(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                 unsigned char __far *out);   /* (a row: its edges, the kernel between them) */
long __far __cdecl rv_turn_dropped(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs,
                                   int xe, int pl, int pr, unsigned char __far *out, const unsigned char __far *above);

/* --- the .RV fallback: decoding WC's sprite code --- */

static unsigned long rd32(FILE *f)
{
    unsigned char b[4];
    fread(b, 1, 4, f);
    return b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

/* the records of a view (its raw bytes at buf) for rows r0 .. r0+n-1 into
 * the band buffer, w wide; the origin's column is x1 + 1, its row y1 + 1 */
static void decode_band(const unsigned char __far *buf, unsigned long len, int w, int x1, int y1, int r0, int n)
{
    unsigned long off = 8;
    _fmemset(g_band, 0, (unsigned)n * w);
    while (off + 6 <= len) {
        short key = *(const short __far *)(buf + off), vx = *(const short __far *)(buf + off + 2);
        short vy = *(const short __far *)(buf + off + 4);
        int row = vy + y1 + 1 - r0, x = vx + x1 + 1, cnt = key / 2, k;
        unsigned char __far *line = row >= 0 && row < n ? g_band + (unsigned)row * w : 0;
        off += 6;
        if (key == 0)
            break;
        if (key % 2 == 0) {
            if (line)
                _fmemcpy(line + x, buf + off, cnt);
            off += cnt;
        } else {
            int got = 0;
            while (got < cnt && off < len) {
                k = buf[off++];
                if (k % 2 == 0) {
                    if (line)
                        _fmemcpy(line + x + got, buf + off, k / 2);
                    off += k / 2;
                } else {
                    unsigned char c = buf[off++];
                    if (line)
                        _fmemset(line + x + got, c, k / 2);
                }
                got += k / 2;
            }
        }
    }
}

/* one copy of a view (mirrored or not) out as run-length rows into XMS at
 * `at`, through the scratch's free end: fills the row table, returns the
 * bytes written */
static unsigned long encode_view(XmsBlock blk, unsigned long at, const unsigned char __far *raw, unsigned long len,
                                 int w, int h, int x1, int y1, int mirror, unsigned long __far *tab)
{
    unsigned char __far *out = gfx_scratch() + (unsigned)((len + 15) & ~15UL);
    unsigned room = SCRATCH - (unsigned)((len + 15) & ~15UL), n = 0;
    unsigned long start = at;
    int r0, y;
    for (r0 = 0; r0 < h; r0 += BAND_ROWS) {
        int rows = h - r0 < BAND_ROWS ? h - r0 : BAND_ROWS;
        decode_band(raw, len, w, x1, y1, r0, rows);
        for (y = 0; y < rows; y++) {
            const unsigned char __far *line = g_band + (unsigned)y * w;
            int x = 0;
            tab[r0 + y] = at + n - start;
            while (x < w) {
                int skip = 0, run = 0, i;
                while (x < w && !line[mirror ? w - 1 - x : x]) { x++; skip++; }
                if (x >= w)
                    break;
                while (x + run < w && line[mirror ? w - 1 - x - run : x + run] && run < 255)
                    run++;
                if (n + skip / 255 * 2 + 4 + run + 2 > room) {
                    xms_put(blk, at, out, n);
                    at += n;
                    n = 0;
                }
                while (skip > 255) {
                    out[n++] = 255; out[n++] = 0;
                    skip -= 255;
                }
                out[n++] = (unsigned char)skip;
                out[n++] = (unsigned char)run;
                for (i = 0; i < run; i++)
                    out[n + i] = line[mirror ? w - 1 - x - i : x + i];
                n += run;
                x += run;
            }
            out[n++] = 0;
            out[n++] = 0;
        }
    }
    tab[h] = at + n - start;
    if (n)
        xms_put(blk, at, out, n);
    return at + n - start;
}

static int load_rv(ShipSet __far *s, const char *name)
{
    FILE *f = fopen(name, "rb");
    unsigned long total, b0, b1, n, i, at, size, tab0[64];
    unsigned long __far *rowtab;
    unsigned char __far *raw = gfx_scratch();
    unsigned char rec[13];
    int v;
    if (!f)
        return 0;
    if (!g_band) g_band = (unsigned char __far *)_fmalloc((size_t)BAND_ROWS * LINE);
    RV_ASSERT(g_band != 0);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    total = rd32(f);
    b0 = rd32(f) & 0xFFFFFFUL;
    b1 = rd32(f) & 0xFFFFFFUL;
    (void)total;
    fseek(f, b1, SEEK_SET);
    fread(rec, 1, 13, f);
    if (memcmp(rec, "RVSH", 4) != 0) {
        rv_log("ships: %s is not a ship file", name);
        fclose(f);
        return 0;
    }
    s->nviews = rec[5] | (rec[6] << 8);
    s->asym = rec[7] & 1;
    s->lod0 = rec[9] | (rec[10] << 8);
    s->nb0 = rec[11] ? rec[11] : 5;    /* the lattice (0, 0 in older files: WC1's 5 x 7) */
    s->nh0 = rec[12] ? rec[12] : 7;
    s->nb1 = s->nb0;
    s->nh1 = s->nh0;
    fseek(f, b0, SEEK_SET);
    total = rd32(f);
    n = (rd32(f) & 0xFFFFFFUL);
    n = (n - 4) / 4;                    /* the first offset says how long the table is */
    if (n > 64) n = 64;
    fseek(f, b0 + 4, SEEK_SET);
    for (i = 0; i < n; i++)
        tab0[i] = b0 + (rd32(f) & 0xFFFFFFUL);
    if (s->nviews > n) s->nviews = (unsigned char)n;
    s->blk = xms_alloc(size * 3 + 65536UL, XMS_MISSION);
    if (!s->blk) {
        rv_log("ships: no XMS for %s", name);
        fclose(f);
        return 0;
    }
    rowtab = (unsigned long __far *)_fmalloc(4 * 1025);
    RV_ASSERT(rowtab != 0);
    at = 0;
    for (v = 0; v < s->nviews; v++) {
        short hdr[4];
        int w, h, copies = 1, c;
        unsigned long len = (i = v + 1) < n ? tab0[i] - tab0[v] : b0 + total - tab0[v], k;
        if (len > SCRATCH - OUT_MIN) {
            rv_log("ships: %s view %d is %lu bytes, too big to decode", name, v, len);
            continue;
        }
        fseek(f, tab0[v], SEEK_SET);
        for (k = 0; k < len; k += 1024) {
            unsigned part = len - k < 1024 ? (unsigned)(len - k) : 1024;
            fread(rv_filebuf, 1, part, f);
            _fmemcpy(raw + k, rv_filebuf, part);
        }
        _fmemcpy(hdr, raw, 8);          /* x2, x1, y1, y2 */
        w = hdr[1] + hdr[0] + 2;
        h = hdr[2] + hdr[3] + 2;
        if (w > LINE || h > 1024) {
            rv_log("ships: %s view %d is %dx%d, too big to decode", name, v, w, h);
            continue;
        }
        /* a symmetric ship's band views between the nose-on and tail-on headings
         * also make the mirrored heading (their pair) */
        if (!s->asym && lf_pair_of(v, s->nb0, s->nh0) != v)
            copies = 2;
        for (c = 0; c < copies; c++) {
            int idx = c ? lf_pair_of(v, s->nb0, s->nh0) : v;
            ShipView __far *view;
            view = &s->view[idx];
            view->w = (unsigned short)w;
            view->h = (unsigned short)h;
            view->ox = (short)(c ? w - 1 - (hdr[1] + 1) : hdr[1] + 1);
            view->oy = (short)(hdr[2] + 1);
            view->blk = s->blk;
            view->at = at;
            at += 4UL * (h + 1);
            at += encode_view(s->blk, at, raw, len, w, h, hdr[1], hdr[2], c, rowtab);
            xms_put(s->blk, view->at, rowtab, 4UL * (h + 1));
            at = (at + 1) & ~1UL;
        }
    }
    if (!s->asym)
        s->nviews = (unsigned short)LF_LATTICE_VIEWS(s->nb0, s->nh0);
        RV_ASSERT(s->nviews <= SHIP_VIEWS);   /* (the .RV fallback makes the mirrored headings here: lattices to 15 degrees) */
    _ffree(rowtab);
    fclose(f);
    s->nsizes = 1;
    s->nrolls = 1;
    s->scale[0] = 256;
    rv_log("ships: %s decoded, %d views, %lu KB in XMS (no .RVS: scaled and turned at draw time)", name, s->nviews, at / 1024);
    return 1;
}

/* --- the pre-rendered sets --- */

/* a file's bytes [at, at+n) into an XMS block at `to` */
/* n bytes of the file from `at` into XMS, through gfx's scratch (32 KB a read: a mission's blocks are
 * megabytes, and in the browser every DOS read is a trip through the page's file system) */
static void file_to_xms(FILE *f, unsigned long at, unsigned long n, XmsBlock blk, unsigned long to)
{
    unsigned long k;
    int fd = fileno(f);
    lseek(fd, at, SEEK_SET);
    for (k = 0; k < n; k += SCRATCH) {
        unsigned part = n - k < SCRATCH ? (unsigned)(n - k) : SCRATCH, got;
        if (_dos_read(fd, gfx_scratch(), part, &got) != 0 || got != part)
            break;
        xms_put(blk, to + k, gfx_scratch(), part);
    }
}

/* the view record (w, h, ox, oy, at) of view v in a block of a set: 1 when it is stored and loaded */
static int set_view(const ShipSet __far *s, const SetBlock __far *b, int v, ShipView *out)
{
    unsigned char e[12];
    if (!b || !b->loaded || !b->blk || v >= (int)b->nv)   /* (2: the full block, resident for the exact draw) */           /* (2: the full-size block, the exact draw's: its views are the dense lattice's) */
        return 0;
    xms_get(s->blk, b->index_at + 12UL * v, e, 12);
    out->w = e[0] | (e[1] << 8);
    out->h = e[2] | (e[3] << 8);
    out->ox = (short)(e[4] | (e[5] << 8));
    out->oy = (short)(e[6] | (e[7] << 8));
    out->blk = b->blk;
    out->at = b->xoff + (e[8] | ((unsigned long)e[9] << 8) | ((unsigned long)e[10] << 16) | ((unsigned long)e[11] << 24));
    return out->w != 0;
}

static int load_block(ShipSet __far *s, SetBlock __far *b)
{
    FILE *f;
    char name[16];                      /* (near: the file calls take near pointers) */
    if (b->loaded)
        return 1;
    b->blk = xms_alloc(b->length + 2, XMS_MISSION);
    b->xoff = 0;
    if (!b->blk)
        return 0;
    sprintf(name, "SHIP%02X.RV%c", s->type, s->rvh ? 'H' : 'S');
    f = fopen(name, "rb");
    if (!f)
        return 0;
    file_to_xms(f, b->at, b->length, b->blk, 0);
    fclose(f);
    b->loaded = 1;
    return 1;
}

static int load_rvs(ShipSet __far *s, const char *name)
{
    FILE *f = fopen(name, "rb");
    unsigned char hd[48];
    unsigned long nblocks, i, index_len;
    int k, v;
    if (!f)
        return 0;
    fread(hd, 1, 48, f);
    if (memcmp(hd, "RVSP", 4) != 0) {
        rv_log("ships: %s is not a sprite set", name);
        fclose(f);
        return 0;
    }
    s->rvs = 1;
    s->nviews = hd[5];
    s->nsizes = hd[6];
    s->nrolls = hd[7];
    s->roll_sizes = hd[8];
    s->asym = hd[9] & 1;
    s->factor = (hd[9] & 2) ? 2 : (hd[9] & 4) ? 4 : 1;
    s->nb0 = hd[10] ? hd[10] : 5;
    s->nh0 = hd[11] ? hd[11] : 7;
    for (k = 0; k < 16; k++)
        s->scale[k] = hd[12 + 2 * k] | (hd[13 + 2 * k] << 8);
    s->nb1 = hd[44] ? hd[44] : 5;
    s->nh1 = hd[45] ? hd[45] : 7;
    if (hd[46] | hd[47])
        s->nviews = hd[46] | (hd[47] << 8);   /* the whole count (byte 5 is its low byte) */
    nblocks = rd32(f);
    RV_ASSERT(nblocks <= SET_BLOCKS && s->nviews <= SHIP_VIEWS && s->nrolls <= 32);
    s->nblocks = (int)nblocks;
    /* the index: every block's header and view table, walked by their own view counts */
    index_len = 0;
    for (i = 0; i < nblocks; i++) {
        unsigned char bh[12];
        SetBlock __far *b = &s->block[i];
        fseek(f, 52 + index_len, SEEK_SET);
        fread(bh, 1, 12, f);
        b->size = bh[0];
        b->roll = bh[1];
        b->at = bh[4] | ((unsigned long)bh[5] << 8) | ((unsigned long)bh[6] << 16) | ((unsigned long)bh[7] << 24);
        b->length = bh[8] | ((unsigned long)bh[9] << 8) | ((unsigned long)bh[10] << 16) | ((unsigned long)bh[11] << 24);
        b->index_at = index_len + 12;
        b->nv = (unsigned short)(bh[2] | (bh[3] << 8));
        b->loaded = 0;
        index_len += 12 + 12UL * (bh[2] | (bh[3] << 8));
    }
    s->blk = xms_alloc(index_len + 2, XMS_MISSION);
    if (!s->blk) {
        fclose(f);
        return 0;
    }
    file_to_xms(f, 52, index_len, s->blk, 0);
    fclose(f);
    /* the full-size unturned block (the exact draw's: loaded = 2, set_view never takes its views, which are in the
     * dense lattice, not the copies'): its views' records, where each is in it */
    {
        SetBlock __far *b0 = &s->block[0];
        RV_ASSERT(b0->size == 0 && b0->roll == 0);
        b0->loaded = 2;
        for (v = 0; v < s->nviews; v++) {
            unsigned char e[12];
            ShipView one;
            xms_get(s->blk, b0->index_at + 12UL * v, e, 12);
            one.w = e[0] | (e[1] << 8);
            one.h = e[2] | (e[3] << 8);
            one.ox = (short)(e[4] | (e[5] << 8));
            one.oy = (short)(e[6] | (e[7] << 8));
            one.blk = 0;
            one.at = e[8] | ((unsigned long)e[9] << 8) | ((unsigned long)e[10] << 16) | ((unsigned long)e[11] << 24);
            s->view[v] = one;           /* (in the block: blk set when it is loaded, load_full) */
        }
    }
    rv_log("ships: %s, %d views (%d x %d; the copies %d x %d), %d sizes, %d roll steps on %d sizes, %lu blocks", name,
           s->nviews, s->nb0, s->nh0, s->nb1, s->nh1, s->nsizes, s->nrolls, s->roll_sizes, nblocks);
    return 1;
}

int ships_find(unsigned type)
{
    int i;
    for (i = 0; i < g_nsets; i++)
        if (g_set[i]->type == type)
            return i;
    return -1;
}

int ships_load(unsigned type)
{
    char name[16];
    ShipSet __far *s;
    int found = ships_find(type), ok;
    if (found >= 0)
        return found;
    if (g_nsets >= SHIP_SETS)
        return -1;
    s = (ShipSet __far *)_fmalloc(sizeof(ShipSet));
    RV_ASSERT(s != 0);
    _fmemset(s, 0, sizeof(ShipSet));
    s->type = (unsigned char)type;
    ok = 0;
    if (g_res == 2) {                   /* the block mode's set, when it is there */
        sprintf(name, "SHIP%02X.RVH", type);
        s->rvh = 1;
        ok = load_rvs(s, name);
        if (!ok)
            s->rvh = 0;
    }
    if (!ok) {
        sprintf(name, "SHIP%02X.RVS", type);
        ok = load_rvs(s, name);
    }
    if (!ok) {
        sprintf(name, "SHIP%02X.RV", type);
        ok = load_rv(s, name);
    }
    if (!ok) {
        rv_log("ships: no set for type %02X", type);
        _ffree(s);
        return -1;
    }
    g_set[g_nsets] = s;
    return g_nsets++;
}

/* how many of a set's roll blocks at size k are not loaded */
static int rolls_out(const ShipSet __far *s, int k)
{
    int b, n = 0;
    for (b = 0; b < s->nblocks; b++)
        n += s->block[b].size == k && s->block[b].roll && !s->block[b].loaded;
    return n;
}

/* a set's roll blocks at size k (they lie together in the file, rolls 1 .. in order) in one XMS block, each at
 * its offset in it: a fighter type takes 22 XMS blocks rather than 85 (the arena's table has 512): the bytes
 * taken, 0 when there were none to load or they did not fit */
static unsigned long load_rolls(ShipSet __far *s, int k, unsigned long room)
{
    int b, first = -1, last = -1;
    unsigned long span;
    XmsBlock blk;
    FILE *f;
    char name[16];
    for (b = 0; b < s->nblocks; b++)
        if (s->block[b].size == k && s->block[b].roll && !s->block[b].loaded) {
            if (first < 0)
                first = b;
            last = b;
        }
    if (first < 0)
        return 0;
    span = s->block[last].at + s->block[last].length - s->block[first].at;
    if (span + 2 > room || !(blk = xms_alloc(span + 2, XMS_MISSION)))
        return 0;
    sprintf(name, "SHIP%02X.RV%c", s->type, s->rvh ? 'H' : 'S');
    if (!(f = fopen(name, "rb")))
        return 0;
    file_to_xms(f, s->block[first].at, span, blk, 0);
    fclose(f);
    for (b = first; b <= last; b++)
        if (s->block[b].size == k && s->block[b].roll && !s->block[b].loaded) {
            s->block[b].blk = blk;
            s->block[b].xoff = s->block[b].at - s->block[first].at;
            s->block[b].loaded = 1;
        }
    return span + 2;
}

/* a set's full-size block in, its views pointed at it */
static int load_full(ShipSet __far *s, SetBlock __far *b)
{
    int v, ok;
    b->loaded = 0;
    ok = load_block(s, b);
    b->loaded = 2;                      /* (the exact draw's still: not the sets') */
    if (!ok)
        return 0;
    for (v = 0; v < s->nviews; v++)
        if (s->view[v].w)
            s->view[v].blk = b->blk;
    return 1;
}

/* the dense copies' cache, then the sets' blocks: the ladder (every size, unturned) for every set, then the
 * full-size views (the exact draw's), then the rolls, largest size first, while the budget lasts */
unsigned long ships_load_blocks(unsigned long budget)
{
    unsigned long taken = 0;
    int pass, i, k;
    for (i = 0, g_ndc = 0; i < DCACHE; i++) {
        g_dc[i].set = g_dc[i].key = -1;
        g_dc[i].used = 0;
        g_dc[i].blk = taken + DENSE_MAX <= budget ? xms_alloc(DENSE_MAX, XMS_MISSION) : 0;
        if (g_dc[i].blk) {
            taken += DENSE_MAX;
            g_ndc = i + 1;
        }
    }
    g_dc_hits = g_dc_expands = 0;
    for (pass = 0; pass < 3; pass++)
        for (k = 0; k < 16; k++)
            for (i = 0; i < g_nsets; i++) {
                ShipSet __far *s = g_set[i];
                int b;
                if (!s->rvs)
                    continue;
                if (pass == 2) {        /* the rolls: a size's at once */
                    unsigned long got = load_rolls(s, k, budget - taken);
                    if (!got && rolls_out(s, k)) {
                        rv_log("ships: the budget is spent at type %02X size %d's rolls", s->type, k);
                        goto spent;
                    }
                    taken += got;
                    continue;
                }
                for (b = 0; b < s->nblocks; b++) {
                    SetBlock __far *blk = &s->block[b];
                    int full = blk->loaded == 2;
                    if (blk->size != k || (pass == 1) != full || (full ? blk->blk != 0 : blk->loaded)
                        || (!full && (pass == 0) != (blk->roll == 0)))
                        continue;
                    if (taken + blk->length > budget) {
                        rv_log("ships: the budget is spent at type %02X size %d roll %d", s->type, k, blk->roll);
                        goto spent;
                    }
                    if (full ? !load_full(s, blk) : !load_block(s, blk))
                        goto spent;
                    taken += blk->length;
                }
            }
spent:
    for (i = 0; i < g_nsets; i++) {     /* which sizes have all their rolls in (turn_wanted asks every frame) */
        ShipSet __far *s = g_set[i];
        s->rolls_in = 0;
        for (k = 0; k < s->nsizes && k < 16; k++)
            if (s->rvs && !rolls_out(s, k) && s->nrolls >= 4) {
                int b, any = 0;
                for (b = 0; b < s->nblocks; b++)
                    any |= s->block[b].size == k && s->block[b].roll;
                if (any)
                    s->rolls_in |= 1U << k;
            }
    }
    return taken;
}

const ShipSet __far *ships_set(int i)
{
    RV_CHECK(i, g_nsets);
    return g_set[i];
}

void ships_unload(void)
{
    int i;
    for (i = 0; i < g_nsets; i++)
        _ffree(g_set[i]);
    g_nsets = 0;
    for (i = 0; i < DENSE_BUFS; i++)
        g_dense_set[i] = g_dense_view[i] = -1;
    for (i = 0; i < DCACHE; i++) {
        g_dc[i].set = g_dc[i].key = -1;
        g_dc[i].blk = 0;                /* (the mission level frees it) */
    }
    g_ndc = 0;
}

/* a full-size view's record with its data: 0 when it is not in (its set's full-size block did not fit) */
static const ShipView *full_view(const ShipSet __far *s, int v, ShipView *out)
{
    *out = s->view[v];
    return out->w && out->blk ? out : 0;
}

/* a symmetric set's mirrored headings, the indices past its stored views, are drawn as the pair's mirror image */
static int mirrored_index(const ShipSet __far *s, int v)
{
    return !s->asym && v >= 2 + s->nb0 * s->nh0;
}

/* the stored view behind v: itself, or a symmetric set's pair of a mirrored heading (then *flags gets BLIT_H) */
static int stored_view(const ShipSet __far *s, int v, int *flags)
{
    *flags = 0;
    if (mirrored_index(s, v)) {
        *flags = BLIT_H;
        v = lf_pair_of(v, s->nb0, s->nh0);
    }
    RV_CHECK(v, SHIP_VIEWS);
    return v;
}

void ships_draw_log(void)
{
    static const char __far s_dc[] = "ships: the turned draw's dense copies: %lu expanded, %lu moved back from XMS "
        "(%d kept)";
    rv_logf(s_dc, g_dc_expands, g_dc_hits, g_ndc);
}

/* --- drawing --- */

void ships_set_camera(int cx, int cy, int focal)
{
    g_cx = cx;
    g_cy = cy;
    g_focal = focal;
}

void ships_lattice(int set_i, int exact, int *nb, int *nh)
{
    const ShipSet __far *s = ships_set(set_i);
    *nb = exact ? s->nb0 : s->nb1;
    *nh = exact ? s->nh0 : s->nh1;
}

/* rows r0 .. *r1-1 of a view into the scratch (one XMS move for the table,
 * one for the rows; *r1 comes down when they do not all fit): returns the
 * row table, offsets relative to row r0's data, which starts at *data */
static unsigned long __far *fetch_rows_in(const ShipView *view, int r0, int *r1, unsigned char __far **data,
                                         unsigned char __far *scratch, unsigned size)
{
    unsigned long __far *tab = (unsigned long __far *)scratch;
    unsigned n = *r1 - r0 + 1, room;
    xms_get(view->blk, view->at + 4UL * r0, tab, 4UL * n);
    room = size - 4 * n - 16;
    while (tab[*r1 - r0] - tab[0] > room && *r1 > r0 + 1)
        (*r1)--;
    *data = scratch + 4 * n;
    xms_get(view->blk, view->at + 4UL * (view->h + 1) + tab[0], *data, tab[*r1 - r0] - tab[0]);
    return tab;
}

static unsigned long __far *fetch_rows(const ShipView *view, int r0, int *r1, unsigned char __far **data)
{
    return fetch_rows_in(view, r0, r1, data, gfx_scratch(), SCRATCH);
}

/* a row's clip: [xa, xb) and the opening's extent on row ry; 0 when nothing is left */
static int row_clip(int ry, int *ca, int *cb, const short __far *ext, int ext_y0, int ext_rows)
{
    if (ext) {
        const short __far *e = ext + 2 * (ry - ext_y0);
        if (ry < ext_y0 || ry >= ext_y0 + ext_rows)
            return 0;
        if (e[0] > *ca) *ca = e[0];
        if (e[1] < *cb) *cb = e[1];
    }
    return *ca < *cb;
}

/* the copy: the view's rows, span by span; upside down (BLIT_V: the rows in
 * the other order) and mirrored (BLIT_H: each row backwards) as asked */
void ships_blit_view(const ShipView __far *fv, int flags, int x, int y, int y0, int y1, int xa, int xb,
                     const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view = *fv;
    int top, left, right, r0, r1, h = view.h, w = view.w, flip = flags & BLIT_V, mir = flags & BLIT_H;
    if (!w)
        return;
    top = y - (flip ? h - 1 - view.oy : view.oy);
    left = x - (mir ? w - 1 - view.ox : view.ox);
    right = left + w;
    if (y0 < top) y0 = top;
    if (y1 > top + h) y1 = top + h;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || right <= xa)
        return;
    /* the sprite rows wanted: screen rows y0..y1-1 are sprite rows y0-top.. (or h-1-.. when flipped) */
    r0 = flip ? h - (y1 - top) : y0 - top;
    while (r0 < (flip ? h - (y0 - top) : y1 - top)) {
        unsigned long __far *tab;
        unsigned char __far *data, __far *dst;
        int r;
        r1 = flip ? h - (y0 - top) : y1 - top;
        tab = fetch_rows(&view, r0, &r1, &data);
        /* the run's rows are contiguous: step the row pointer instead of asking for each */
        dst = vid_row(flip ? top + (h - 1 - r0) : top + r0);
        for (r = r0; r < r1; r++, dst += flip ? -SCR_W : SCR_W) {
            int ry = flip ? top + (h - 1 - r) : top + r, ca = xa, cb = xb;
            if (!row_clip(ry, &ca, &cb, ext, ext_y0, ext_rows))
                continue;
            if (mir)
                rv_blit_row_rev(dst, data + (unsigned)(tab[r - r0] - tab[0]), right, ca, cb);
            else
                rv_blit_row(dst, data + (unsigned)(tab[r - r0] - tab[0]), left, ca, cb);
        }
        r0 = r1;
    }
}

/* a view whose rows may stand for two screen rows each (step 2: kept at half its height) and may be drawn
 * doubled or interlaced (the sky's far things: rows 1 every row, 2 each kept row on two screen rows or, of a
 * full view, its even rows twice, 3 only the even screen rows from the thing's top, what is behind showing
 * between them) */
void ships_blit_rows(const ShipView __far *fv, int flags, int step, int rows, int x, int y, int y0, int y1, int xa,
                     int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view = *fv;
    int top, left, right, r0, r1, rlo, rhi, ry, H = view.h * step, w = view.w, flip = flags & BLIT_V,
        mir = flags & BLIT_H, oy = view.oy * step;
    if (!w)
        return;
    top = y - (flip ? H - 1 - oy : oy);
    left = x - (mir ? w - 1 - view.ox : view.ox);
    right = left + w;
    if (y0 < top) y0 = top;
    if (y1 > top + H) y1 = top + H;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || right <= xa)
        return;
    /* the kept rows screen rows y0 .. y1-1 show */
    rlo = flip ? H - (y1 - top) : y0 - top;
    rhi = flip ? H - (y0 - top) : y1 - top;
    if (step == 2) {
        rlo >>= 1;
        rhi = (rhi + 1) >> 1;
    } else if (rows == 2) {
        rlo &= ~1;
    }
    for (r0 = rlo; r0 < rhi; r0 = r1) {
        unsigned long __far *tab;
        unsigned char __far *data;
        r1 = rhi;
        tab = fetch_rows(&view, r0, &r1, &data);
        for (ry = y0; ry < y1; ry++) {
            int i = ry - top, r = flip ? H - 1 - i : i, ca = xa, cb = xb;
            if (rows == 3 && (i & 1))
                continue;
            if (step == 2)
                r >>= 1;
            else if (rows == 2)
                r &= ~1;
            if (r < r0 || r >= r1 || !row_clip(ry, &ca, &cb, ext, ext_y0, ext_rows))
                continue;
            if (mir)
                rv_blit_row_rev(vid_row(ry), data + (unsigned)(tab[r - r0] - tab[0]), right, ca, cb);
            else
                rv_blit_row(vid_row(ry), data + (unsigned)(tab[r - r0] - tab[0]), left, ca, cb);
        }
    }
}

/* two views of one size and origin, each row from one or the other as bit (row + seed) of a scramble says: the
 * engines' flames, two frames of noise mixed row by row so a flame changes every frame */
void ships_blit_mix(const ShipView __far *fa, unsigned long at_b, unsigned seed, int flags, int x, int y, int y0, int y1,
                    int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView va = *fa, vb = *fa;
    int top, left, right, r0, r1, rb, h = va.h, w = va.w, flip = flags & BLIT_V, mir = flags & BLIT_H;
    if (!w)
        return;
    vb.at = at_b;
    top = y - (flip ? h - 1 - va.oy : va.oy);
    left = x - (mir ? w - 1 - va.ox : va.ox);
    right = left + w;
    if (y0 < top) y0 = top;
    if (y1 > top + h) y1 = top + h;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || right <= xa)
        return;
    r0 = flip ? h - (y1 - top) : y0 - top;
    while (r0 < (flip ? h - (y0 - top) : y1 - top)) {
        unsigned long __far *ta, __far *tb;
        unsigned char __far *da, __far *db, __far *dst;
        int r;
        r1 = flip ? h - (y0 - top) : y1 - top;
        ta = fetch_rows_in(&va, r0, &r1, &da, gfx_scratch(), SCRATCH / 2);
        rb = r1;
        tb = fetch_rows_in(&vb, r0, &rb, &db, gfx_scratch() + SCRATCH / 2, SCRATCH / 2);
        if (rb < r1)
            r1 = rb;
        dst = vid_row(flip ? top + (h - 1 - r0) : top + r0);
        for (r = r0; r < r1; r++, dst += flip ? -SCR_W : SCR_W) {
            int ry = flip ? top + (h - 1 - r) : top + r, ca = xa, cb = xb, b = ((unsigned)(r + seed) * 40503U >> 9) & 1;
            const unsigned char __far *src = b ? db + (unsigned)(tb[r - r0] - tb[0]) : da + (unsigned)(ta[r - r0] - ta[0]);
            if (!row_clip(ry, &ca, &cb, ext, ext_y0, ext_rows))
                continue;
            if (mir)
                rv_blit_row_rev(dst, src, right, ca, cb);
            else
                rv_blit_row(dst, src, left, ca, cb);
        }
        r0 = r1;
    }
}

int ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
               const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    int flags;
    v = stored_view(s, v, &flags);      /* a mirrored heading not stored: its pair, mirrored */
    if (!full_view(s, v, &view))
        return 0;
    ships_blit_view(&view, flags, x, y, y0, y1, xa, xb, ext, ext_y0, ext_rows);
    return 1;
}

static int find_sprite(const ShipSet __far *s, int k, int v, int r, ShipView *out);

int ships_blit_size(const ShipSet __far *s, int k, int v, int x, int y, int y0, int y1, int xa, int xb,
                    const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    int flags;
    RV_CHECK(v, SHIP_VIEWS);
    if (!s->rvs || (flags = find_sprite(s, k, v, 0, &view)) < 0)
        return 0;
    ships_blit_view(&view, flags, x, y, y0, y1, xa, xb, ext, ext_y0, ext_rows);
    return 1;
}

int ships_view_size(const ShipSet __far *s, int k, int v, int *w, int *h)
{
    ShipView view;
    RV_CHECK(v, SHIP_VIEWS);
    if (!s->rvs || find_sprite(s, k, v, 0, &view) < 0)
        return 0;
    *w = view.w;
    *h = view.h;
    return 1;
}

/* a run-length row expanded into a line of w pixels (0 where clear) */
static void expand_row(const unsigned char __far *p, unsigned char __far *line, int w)
{
    int x = 0;
    _fmemset(line, 0, w);
    for (;;) {
        unsigned skip = p[0], n = p[1];
        p += 2;
        if (!skip && !n)
            break;
        x += skip;
        if (n) {
            _fmemcpy(line + x, p, n);
            p += n;
            x += n;
        }
    }
}

static void need_buffers(void)
{
    if (!g_cmap) g_cmap = (unsigned short __far *)_fmalloc(2 * LINE);
    if (!g_line) g_line = (unsigned char __far *)_fmalloc(LINE);
    RV_ASSERT(g_cmap && g_line);
}

/* the fallback's scaled copy: each output row expands its source row once,
 * then maps columns straight into the back buffer */
static void draw_scaled(const ShipSet __far *s, int v, int x, int y, int scale256, int y0, int y1, int xa, int xb,
                        const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    int w, h, ow, oh, top, left, oy, r0 = 0, r1 = 0;
    unsigned long __far *tab = 0;
    unsigned char __far *data = 0;
    if (!full_view(s, v, &view))
        return;
    w = view.w;
    h = view.h;
    if (!w)
        return;
    need_buffers();
    ow = (int)(((long)w * scale256 + 255) / 256);
    oh = (int)(((long)h * scale256 + 255) / 256);
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    if (ow > LINE) ow = LINE;
    top = y - (int)((long)view.oy * scale256 / 256);
    left = x - (int)((long)view.ox * scale256 / 256);
    if (y0 < top) y0 = top;
    if (y1 > top + oh) y1 = top + oh;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || left + ow <= xa)
        return;
    if (xa < left) xa = left;
    if (xb > left + ow) xb = left + ow;
    lf_colmap(w, ow, g_cmap);
    for (oy = y0; oy < y1; oy++) {
        int sy = (int)((long)(oy - top) * 256 / scale256), ca = xa, cb = xb;
        if (sy >= h) sy = h - 1;
        if (!row_clip(oy, &ca, &cb, ext, ext_y0, ext_rows))
            continue;
        if (sy >= r1) {
            r0 = sy;
            r1 = h;
            tab = fetch_rows(&view, r0, &r1, &data);
        }
        expand_row(data + (unsigned)(tab[sy - r0] - tab[0]), g_line, w);
        rv_scale_row(g_line, g_cmap, ca - left, cb - left, vid_row(oy) + left);
    }
}

/* a dense buffer: upper memory first (both fit DOSBox's 128 KB of UMBs; each
 * there is 64 KB more of the 640 for the rest), else conventional */
static unsigned char __far *dense_alloc(void)
{
    unsigned char __far *p = (unsigned char __far *)rv_alloc_high(DENSE_MAX);
    rv_log("ships: a dense buffer in %s memory", p ? "upper" : "conventional");
    return p ? p : (unsigned char __far *)_fmalloc(DENSE_MAX);
}

int ships_dense_reserve(void)
{
    int i;
    for (i = 0; i < DENSE_BUFS; i++)
        if (!g_dense_buf[i])
            g_dense_buf[i] = dense_alloc();
    return g_dense_buf[DENSE_BUFS - 1] != 0;
}

/* The dense copies' cache in XMS (the mission level): a view once expanded
 * is kept there too, so a ship whose copy another ship's took from the two
 * buffers gets it back with one XMS move (DOSBox makes it at host speed)
 * rather than expanding it again (with three ships turned at draw time, each
 * had re-expanded its view every frame: 25.6 ms).  A copy's key: its
 * full-size view's number. */

/* the copy of key in a dense buffer, or in the XMS cache: where, 0 when in neither */
static int dense_in(int set_i, int key, int *buf, int *dc)
{
    int i;
    *buf = *dc = -1;
    for (i = 0; i < DENSE_BUFS; i++)
        if (g_dense_set[i] == set_i && g_dense_view[i] == key) {
            *buf = i;
            return 1;
        }
    for (i = 0; i < g_ndc; i++)
        if (g_dc[i].blk && g_dc[i].set == set_i && g_dc[i].key == key) {
            *dc = i;
            return 1;
        }
    return 0;
}

/* a dense copy of view `view` (its rows backwards when mir) in g_dense, under key: in a buffer already, else into
 * the least recently used one, from the XMS cache or expanded (and then kept in the cache): 0 when it does not
 * fit */
static int dense_from(int set_i, int key, const ShipView *view, int mir)
{
    int w = view->w, h = view->h, r0 = 0, i, pick = 0, dc;
    g_dense_tick++;
    if (dense_in(set_i, key, &i, &dc) && i >= 0) {
        g_dense = g_dense_buf[i];
        g_dense_used[i] = g_dense_tick;
        return 1;
    }
    if (!w || (unsigned long)w * h > DENSE_MAX)
        return 0;
    for (i = 1; i < DENSE_BUFS; i++)
        if (g_dense_used[i] < g_dense_used[pick])
            pick = i;
    if (!g_dense_buf[pick]) g_dense_buf[pick] = dense_alloc();
    RV_ASSERT(g_dense_buf[pick] != 0);
    g_dense = g_dense_buf[pick];
    g_dense_set[pick] = g_dense_view[pick] = -1;
    if (dc >= 0) {                      /* (kept: one move) */
        xms_get(g_dc[dc].blk, 0, g_dense, ((unsigned long)w * h + 1) & ~1UL);
        g_dc[dc].used = g_dense_tick;
        g_dc_hits++;
    } else {
        while (r0 < h) {
            unsigned long __far *tab;
            unsigned char __far *data;
            int r1 = h, r;
            tab = fetch_rows(view, r0, &r1, &data);
            for (r = r0; r < r1; r++) {
                unsigned char __far *line = g_dense + (unsigned)r * w;
                expand_row(data + (unsigned)(tab[r - r0] - tab[0]), line, w);
                if (mir) {
                    int a = 0, b = w - 1;
                    for (; a < b; a++, b--) {
                        unsigned char t = line[a];
                        line[a] = line[b];
                        line[b] = t;
                    }
                }
            }
            r0 = r1;
        }
        g_dc_expands++;
        for (i = 0, dc = -1; i < g_ndc; i++)   /* (kept for next time, over the least recently used) */
            if (g_dc[i].blk && (dc < 0 || g_dc[i].used < g_dc[dc].used))
                dc = i;
        if (dc >= 0) {
            xms_put(g_dc[dc].blk, 0, g_dense, ((unsigned long)w * h + 1) & ~1UL);
            g_dc[dc].set = (short)set_i;
            g_dc[dc].key = (short)key;
            g_dc[dc].used = g_dense_tick;
        }
    }
    g_dense_set[pick] = set_i;
    g_dense_view[pick] = key;
    g_dense_used[pick] = g_dense_tick;
    return 1;
}

/* the whole full-size view dense in g_dense: 0 when it is not in (its block did not fit) or does not fit */
static int make_dense(const ShipSet __far *s, int set_i, int v)
{
    ShipView view;
    int mir, src, i, dc;
    src = stored_view(s, v, &mir);      /* a mirrored heading not stored: its pair, backwards */
    if (dense_in(set_i, v, &i, &dc))
        view = s->view[src];            /* (its size only: the copy is made) */
    else if (!full_view(s, src, &view))
        return 0;
    return dense_from(set_i, v, &view, mir);
}

/* the rows of a picture turned at draw time, from the dense copy in g_dense (w x h, its origin at (ox, oy)): turned
 * by roll and scaled by scale256 / 256 about its origin, put at (x, y).  detail 1 samples every pixel; 2 samples
 * every other row in full and of the rows between only their edges, the inside copied down from the row above
 * (the user's ideas, 2026-10-21: half height for the ships beyond the plan's first two, its silhouette still
 * exact; the ship taken as convex, a left and a right edge a row, a jagged inside is fine): a drawn row finds its
 * edges walking in from its span and samples between them (rv_turn_drawn), a dropped row probes its edges from the
 * row above's (a step or two each), copies the part the row above covers too and samples the rest out to its own
 * edges (rv_turn_dropped): one call a row (lowlevel.asm).  4 also writes each drawn row's samples as 2 x 1 blocks
 * (rv_rotate_row2): a ship too big for the budget is drawn coarser rather than slower. */
static void turn_rows(int w, int h, int ox, int oy_, int x, int y, int scale256, int roll, int detail, int y0, int y1,
                      int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    LfRot r;
    int top, left, oy, pl = 0, pr = 0;  /* (pl, pr: the row above's edges; none when pl >= pr) */
    lf_rotate_setup(w, h, ox, oy_, roll, scale256, &r);
    top = y - r.ocy;
    left = x - r.ocx;
    if (y0 < top) y0 = top;
    if (y1 > top + r.oh) y1 = top + r.oh;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || left + r.ow <= xa)
        return;
    if (xa < left) xa = left;
    if (xb > left + r.ow) xb = left + r.ow;
    for (oy = y0; oy < y1; oy++) {
        int ca = xa, cb = xb, xs, xe;
        long ed;
        unsigned char __far *row;
        if (!row_clip(oy, &ca, &cb, ext, ext_y0, ext_rows)
            || !lf_rotate_span(&r, w, h, oy - top, ca - left, cb - left, &xs, &xe)) {
            pl = pr = 0;
            continue;
        }
        row = vid_row(oy) + left;
        if (detail < 2) {
            rv_rotate_row(g_dense, w, h, &r, oy - top, xs, xe, row);
            continue;
        }
        if (detail >= 8) {              /* 4 x 4 blocks (the quarter-size sets: capital ships) */
            int ry = oy - top, x4;
            if (ry & 3) {
                if (oy > y0 && oy - 1 >= 0)
                    _fmemcpy(row + (ca - left), vid_row(oy - 1) + ca, cb - ca);
                continue;
            }
            for (x4 = xs & ~3; x4 < xe; x4 += 4) {
                long u = r.u00 + (long)ry * r.dru + (long)x4 * r.du, vv = r.v00 + (long)ry * r.drv + (long)x4 * r.dv;
                long su = u >> 16, sv = vv >> 16;
                unsigned char pix;
                int n;
                if (su < 0 || sv < 0 || su >= w || sv >= h)
                    continue;
                pix = g_dense[(unsigned)sv * w + (unsigned)su];
                if (!pix)
                    continue;
                for (n = 0; n < 4; n++)
                    if (x4 + n >= ca - left && x4 + n < cb - left)
                        row[x4 + n] = pix;
            }
            continue;
        }
        if (((oy - top) & 1) && pl < pr)   /* (a dropped row) */
            ed = rv_turn_dropped(g_dense, w, h, &r, oy - top, xs, xe, pl, pr, row, vid_row(oy - 1) + left);
        else if (detail >= 4) {
            ed = rv_rotate_edges(g_dense, w, h, &r, oy - top, xs, xe);
            if ((int)ed < (int)(ed >> 16))
                rv_rotate_row2(g_dense, w, h, &r, oy - top, (int)ed, (int)(ed >> 16), row);
        } else
            ed = rv_turn_drawn(g_dense, w, h, &r, oy - top, xs, xe, row);
        pl = (int)ed;
        pr = (int)(ed >> 16);
    }
}

static const SetBlock __far *block_of(const ShipSet __far *s, int size, int roll);

/* the turned draw of ladder size k's view v (the block modes: every sprite turned from the ladder, which holds the
 * lattice's own views; a mirrored heading is its pair's copy backwards, as for the full-size views): the dense
 * copy keyed by size and view, kscale the kernel's scale for the view (256 = as stored) */
static int draw_turned_at(const ShipSet __far *s, int set_i, int k, int v, int x, int y, int kscale, int roll,
                          int detail, int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    const SetBlock __far *b = block_of(s, k, 0);
    int mir, src = stored_view(s, v, &mir);
    if (!b || !set_view(s, b, src, &view) || (unsigned long)view.w * view.h > DENSE_MAX)
        return 0;
    if (!dense_from(set_i, k * 1024 + v, &view, mir))
        return 0;
    if (mir)
        view.ox = view.w - 1 - view.ox;
    turn_rows(view.w, view.h, view.ox, view.oy, x, y, kscale, roll, detail, y0, y1, xa, xb, ext, ext_y0, ext_rows);
    return 1;
}

/* the turned draw of a full-size view: inverse mapping from its dense copy */
static int draw_turned(const ShipSet __far *s, int set_i, int v, int x, int y, int scale256, int roll, int detail,
                       int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    if (!make_dense(s, set_i, v))
        return 0;
    if (mirrored_index(s, v)) {         /* the pair's picture, mirrored: so is its origin */
        view = s->view[lf_pair_of(v, s->nb0, s->nh0)];
        view.ox = view.w - 1 - view.ox;
    } else
        view = s->view[v];
    turn_rows(view.w, view.h, view.ox, view.oy, x, y, scale256, roll, detail, y0, y1, xa, xb, ext, ext_y0, ext_rows);
    return 1;
}

/* the set's block for a size and roll step, or NULL */
static const SetBlock __far *block_of(const ShipSet __far *s, int size, int roll)
{
    int b;
    for (b = 0; b < s->nblocks; b++)
        if (s->block[b].size == size && s->block[b].roll == roll)
            return &s->block[b];
    return 0;
}

/* the size step nearest a wanted scale (0 for anything at or above full size) */
static int size_step(const ShipSet __far *s, int scale256)
{
    int k;
    for (k = 0; k + 1 < s->nsizes; k++)
        if (scale256 >= (s->scale[k] + s->scale[k + 1]) / 2)
            return k;
    return s->nsizes - 1;
}

/* The sprite for view v turned by step r (of s->nrolls) at size step k,
 * from what the set stores.  A symmetric set stores the arc 0..90 (steps
 * 0..nrolls/4): the rest are flips of a stored step, of this view or of the
 * opposite heading's: upside down turns by 180 - theta and mirrors the
 * heading, mirrored turns by -theta and mirrors the heading, both turn by
 * 180.  The top and bottom views are their own mirror, and their mirror is
 * their 180 turn, so their table differs.  Returns the flags, -1 when not
 * drawable from loaded blocks. */
static int find_sprite(const ShipSet __far *s, int k, int v, int r, ShipView *out)
{
    int q = s->nrolls / 4, half = s->nrolls / 2, n = s->nrolls, src_v = v, src_r = r, flags = 0;
    int bottom = 1 + s->nb1 * s->nh1;
    const SetBlock __far *b;
    if (s->factor > 1) {                /* a ladder-only set: the lattice's own views, no turns stored */
        int mir, src = stored_view(s, v, &mir);
        const SetBlock __far *b0 = r == 0 ? block_of(s, k, 0) : 0;
        return b0 && set_view(s, b0, src, out) ? mir : -1;
    }
    r %= n;
    if ((b = block_of(s, k, r)) != 0 && set_view(s, b, v, out))
        return 0;
    if (s->asym)
        return -1;
    if (r <= q) {
        return -1;                      /* stored but not loaded */
    } else if (r <= half) {             /* 90 < theta <= 180 */
        src_r = half - r;
        if (v == 0 || v == bottom) { flags = BLIT_H; src_v = v; }
        else { flags = BLIT_V; src_v = lf_pair_of(v, s->nb1, s->nh1); }
    } else if (r <= half + q) {         /* 180 < theta <= 270 */
        src_r = r - half;
        flags = BLIT_V | BLIT_H;
        src_v = v;
    } else {                            /* 270 < theta < 360 */
        src_r = n - r;
        if (v == 0 || v == bottom) { flags = BLIT_V; src_v = v; }
        else { flags = BLIT_H; src_v = lf_pair_of(v, s->nb1, s->nh1); }
    }
    if ((b = block_of(s, k, src_r)) != 0 && set_view(s, b, src_v, out))
        return flags;
    return -1;
}

/* what the last ships_draw or ships_draw_exact drew: its path (ships_draw's return, SHIP_PATH_*), the view and the
 * turn on screen its picture shows (the flames' self-test checks ships_drawn against it) */
static struct { short path, view, roll; } __far g_drawn;

static void drawn(int path, int view, int roll)
{
    g_drawn.path = (short)path;
    g_drawn.view = (short)view;
    g_drawn.roll = (short)roll;
}

int ships_last_drawn(int *view, int *roll)
{
    *view = g_drawn.view;
    *roll = g_drawn.roll;
    return g_drawn.path;
}

/* the sets' sprite for a place (in the copies' lattice): as wanted; else a coarser roll; else unturned (else
 * smaller): its flags (-1: none in), *roll the turn its picture shows (1024 a turn), *path ships_draw's return */
static int set_pick(const ShipSet __far *s, const LfPlace *p, int scale256, ShipView *view, int *roll, int *path)
{
    int flags, step = 1024 / s->nrolls, rq = ((p->roll + step / 2) & 1023) / step, k = size_step(s, scale256);
    if ((flags = find_sprite(s, k, p->view, rq, view)) >= 0) {
        *roll = rq * step;
        *path = 1 + flags;
        return flags;
    }
    if ((rq & 1) && (flags = find_sprite(s, k, p->view, rq & ~1, view)) >= 0) {
        *roll = (rq & ~1) * step;
        *path = 1 + flags;
        return flags;
    }
    for (; k < s->nsizes; k++)
        if ((flags = find_sprite(s, k, p->view, 0, view)) >= 0) {
            *roll = 0;
            *path = SHIP_PATH_UNTURNED;
            return flags;
        }
    return -1;
}

/* view v can be turned at draw time now (draw_turned's make_dense would find it: a dense copy made, or it in) */
static int can_turn(const ShipSet __far *s, int set_i, int v)
{
    ShipView view;
    int i, dc, mir;
    if (dense_in(set_i, v, &i, &dc))
        return 1;
    return full_view(s, stored_view(s, v, &mir), &view) && (unsigned long)view.w * view.h <= DENSE_MAX;
}

/* A ship near enough that the sets have no turned sprites at its size (their four biggest: scale 152 and up),
 * or whose size's roll steps did not fit the mission's XMS, would show unturned from them: the plan draws every
 * such ship exactly instead, turned by its true roll from its full-size view (the user, 2026-10-21: unrolled near
 * fighters look strange), at half height beyond its first two (the user's: each odd row the one above copied
 * down) */
static int turn_wanted(const ShipSet __far *s, int scale256)
{
    return s->rvs && s->nrolls >= 4 && !(s->rolls_in >> size_step(s, scale256) & 1);   /* (ships_load_blocks') */
}

int ships_turn_wanted(int set_i, int scale256)
{
    return turn_wanted(ships_set(set_i), scale256);
}

int g_res = 2;                          /* the block mode is the game's mode (the user, 2026-10-23: "here is where we need
                                           to be"); /res 1 keeps the sharp sets for comparison */

int ships_draw(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
               int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    int rq;
    drawn(0, -1, 0);
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->rvs ? s->nb1 : s->nb0, s->rvs ? s->nh1 : s->nh0, &p);
    if (!p.on_screen || scale256 < 8)
        return 0;
    if (s->rvs) {
        ShipView view;
        int flags, roll, path;
        if ((flags = set_pick(s, &p, scale256, &view, &roll, &path)) < 0)
            return 0;
        ships_blit_view(&view, flags, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        drawn(path, p.view, roll);
        return path;
    }
    rq = ((p.roll + 32) & 1023) / 64;   /* the fallback: sixteen steps, drawn at draw time */
    if (rq == 0 && scale256 >= 256) {
        ships_blit(s, p.view, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        drawn(1, p.view, 0);
        return 1;
    }
    if (rq == 0 || !draw_turned(s, set_i, p.view, p.sx, p.sy, scale256 > 256 ? 256 : scale256, rq * 64, 1, y0, y1, xa, xb,
                                ext, ext_y0, ext_rows)) {
        draw_scaled(s, p.view, p.sx, p.sy, scale256 > 256 ? 256 : scale256, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        drawn(SHIP_PATH_SCALED, p.view, 0);
        return SHIP_PATH_SCALED;
    }
    drawn(SHIP_PATH_TURNED, p.view, rq * 64);
    return SHIP_PATH_TURNED;
}

int ships_draw_exact(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, int detail,
                     int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    drawn(0, -1, 0);
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb0, s->nh0, &p);
    if (!p.on_screen || scale256 < 8)
        return 1;
    if (s->factor > 1) {
        /* a block mode: the ladder's smallest sprite still at least the ship's size over the factor, turned and
         * scaled to the ship's size as 2 x 2 (4 x 4) blocks */
        int k, want = scale256 / s->factor;
        for (k = s->nsizes - 1; k > 0 && s->scale[k] < want; k--)
            ;
        if (draw_turned_at(s, set_i, k, p.view, p.sx, p.sy, (int)((long)scale256 * 256 / s->scale[k]), p.roll,
                           s->factor == 4 ? 8 : 4, y0, y1, xa, xb, ext, ext_y0, ext_rows)) {
            drawn(SHIP_PATH_EXACT, p.view, p.roll);
            return 1;
        }
        return 0;
    }
    if (scale256 == 256 && ((p.roll + 8) & 1023) < 16) {
        ships_blit(s, p.view, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        drawn(SHIP_PATH_EXACT_BLIT, p.view, 0);
        return 1;
    }
    if (draw_turned(s, set_i, p.view, p.sx, p.sy, scale256, p.roll, detail, y0, y1, xa, xb, ext, ext_y0, ext_rows)) {
        drawn(SHIP_PATH_EXACT, p.view, p.roll);
        return 1;
    }
    return 0;                           /* (its full-size views did not fit, or the view is too big to turn) */
}

int ships_draw_as(int set_i, int how, int detail, const LfVec *rel, const LfBasis *cam, const LfBasis *ship,
                  int scale256, int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    if (how == SHIP_AS_EXACT && ships_draw_exact(set_i, rel, cam, ship, scale256, detail, y0, y1, xa, xb, ext, ext_y0,
                                                 ext_rows))
        return g_drawn.path;            /* (0: off the screen) */
    return ships_draw(set_i, rel, cam, ship, scale256, y0, y1, xa, xb, ext, ext_y0, ext_rows);
}

int ships_drawn(int set_i, int how, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
                LfPlace *p, int *nb, int *nh)
{
    const ShipSet __far *s = ships_set(set_i);
    ShipView view;
    int rq, roll, path;
    *nb = s->nb0;
    *nh = s->nh0;
    if (how == SHIP_AS_EXACT) {         /* as ships_draw_exact draws it */
        lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb0, s->nh0, p);
        if (!p->on_screen || scale256 < 8)
            return p->on_screen = 0;
        if (scale256 == 256 && ((p->roll + 8) & 1023) < 16) {
            p->roll = 0;
            return SHIP_PATH_EXACT_BLIT;
        }
        if (can_turn(s, set_i, p->view))
            return SHIP_PATH_EXACT;
    }                                   /* (not in: from the sets, as ships_draw_as falls back) */
    if (!s->rvs) {
        lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb0, s->nh0, p);
        if (!p->on_screen || scale256 < 8)
            return p->on_screen = 0;
        rq = ((p->roll + 32) & 1023) / 64;
        if (rq == 0 && scale256 >= 256) {
            p->roll = 0;
            return 1;
        }
        if (rq == 0 || !can_turn(s, set_i, p->view)) {
            p->roll = 0;
            return SHIP_PATH_SCALED;
        }
        p->roll = rq * 64;
        return SHIP_PATH_TURNED;
    }
    *nb = s->nb1;
    *nh = s->nh1;
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb1, s->nh1, p);
    if (!p->on_screen || scale256 < 8 || set_pick(s, p, scale256, &view, &roll, &path) < 0)
        return p->on_screen = 0;
    p->roll = roll;
    return path;
}

int ships_box(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, int how,
              int cx, int cy, int focal, int box[4])
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    ShipView view;
    LfRot r;
    int flags, rq, k, exact = how == SHIP_AS_EXACT;
    if (exact || !s->rvs) {             /* turned (and scaled) at draw time from the full-size view: the turned image */
        int roll;
        lf_place(rel, cam, ship, cx, cy, focal, s->nb0, s->nh0, &p);
        if (!p.on_screen || scale256 < 8)
            return 0;
        if (mirrored_index(s, p.view)) {
            view = s->view[lf_pair_of(p.view, s->nb0, s->nh0)];
            view.ox = view.w - 1 - view.ox;
        } else
            view = s->view[p.view];
        if (!view.w)
            return 0;
        roll = exact ? p.roll : (((p.roll + 32) & 1023) / 64) * 64;   /* (ships_draw's fallback: sixteen steps) */
        lf_rotate_setup(view.w, view.h, view.ox, view.oy, roll, scale256 > 256 && !exact ? 256 : scale256, &r);
        box[0] = p.sx - r.ocx;
        box[1] = p.sy - r.ocy;
        box[2] = box[0] + r.ow;
        box[3] = box[1] + r.oh;
        return 1;
    }
    lf_place(rel, cam, ship, cx, cy, focal, s->nb1, s->nh1, &p);   /* from the sets: the sprite ships_draw takes */
    if (!p.on_screen || scale256 < 8)
        return 0;
    if ((flags = set_pick(s, &p, scale256, &view, &rq, &k)) < 0 || !view.w)
        return 0;
    box[0] = p.sx - (flags & BLIT_H ? view.w - 1 - view.ox : view.ox);   /* (as ships_blit_view places it) */
    box[1] = p.sy - (flags & BLIT_V ? view.h - 1 - view.oy : view.oy);
    box[2] = box[0] + view.w;
    box[3] = box[1] + view.h;
    return 1;
}

int ships_draw_sky(int set_i, int v, const LfVec *dir, const LfVec *up, const LfBasis *cam, int scale256,
                   int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    lf_sky(dir, up, cam, g_cx, g_cy, g_focal, &p);
    if (!p.on_screen || scale256 < 8)
        return 0;
    if (scale256 == 256 && ((p.roll + 8) & 1023) < 16)
        return ships_blit(s, v, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
    return draw_turned(s, set_i, v, p.sx, p.sy, scale256, p.roll, 1, y0, y1, xa, xb, ext, ext_y0, ext_rows);
}

long ships_size_of(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, LfPlace *p)
{
    const ShipSet __far *s = ships_set(set_i);
    int v;
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb0, s->nh0, p);
    if (!p->on_screen)
        return 0;
    v = mirrored_index(s, p->view) ? lf_pair_of(p->view, s->nb0, s->nh0) : p->view;
    RV_CHECK(v, SHIP_VIEWS);
    return (long)s->view[v].w * s->view[v].h * scale256 / 256 * scale256 / 256 * s->factor * s->factor;
}

/* --- the kernels checked, and the paths timed --- */

/* the asm kernels against the C ones (lightfield.c) and a C mirror, on a
 * small picture at several turns and scales: returns the pixels compared,
 * or -1 on a mismatch */
long ships_selftest(void)
{
    static unsigned char __far pic[9 * 7];
    static unsigned char __far out_c[64], __far out_a[64];
    static unsigned short __far map[64];
    static unsigned char __far spans[32];
    long compared = 0;
    int i, roll, sc, y, x;
    for (i = 0; i < 9 * 7; i++)
        pic[i] = (unsigned char)((i * 7 + 3) % 11 ? 1 + i % 13 : 0);
    lf_colmap(9, 23, map);
    for (x = 0; x < 64; x++) out_c[x] = out_a[x] = (unsigned char)(200 + x);
    lf_scale_row(pic + 9 * 3, map, 2, 21, out_c);
    rv_scale_row(pic + 9 * 3, map, 2, 21, out_a);
    for (x = 0; x < 64; x++, compared++)
        if (out_c[x] != out_a[x]) {
            rv_log("selftest: scale row differs at %d (%d vs %d)", x, out_c[x], out_a[x]);
            return -1;
        }
    for (roll = 0; roll < 1024; roll += 64)
        for (sc = 96; sc <= 320; sc += 112) {
            LfRot r;
            lf_rotate_setup(9, 7, 4, 3, roll, sc, &r);
            if (r.ow > 60)
                continue;
            for (y = 0; y < r.oh; y++) {
                int xs, xe;
                for (x = 0; x < 64; x++) out_c[x] = out_a[x] = (unsigned char)(200 + x);
                lf_rotate_row(pic, 9, 7, &r, y, 0, r.ow, out_c);
                if (lf_rotate_span(&r, 9, 7, y, 0, r.ow, &xs, &xe))
                    rv_rotate_row(pic, 9, 7, &r, y, xs, xe, out_a);
                for (x = 0; x < 64; x++, compared++)
                    if (out_c[x] != out_a[x]) {
                        rv_log("selftest: turned row differs: roll %d scale %d row %d column %d (%d vs %d)", roll, sc, y, x, out_c[x], out_a[x]);
                        return -1;
                    }
            }
        }
    /* the mirrored row copy: a row of spans (3 at 2, 6 at 7, 1 at 20) drawn
     * backwards with the right edge at column 30 (sprite 24 wide), clipped
     * to [5, 28): column d takes pixel 23 - (d - 6) of the sprite */
    {
        static const unsigned char row[] = { 2, 3, 11, 12, 13, 2, 6, 21, 22, 23, 24, 25, 26, 7, 1, 31, 0, 0 };
        unsigned char sprite[24];
        int d;
        _fmemcpy(spans, row, sizeof row);
        memset(sprite, 0, sizeof sprite);
        sprite[2] = 11; sprite[3] = 12; sprite[4] = 13;
        for (i = 0; i < 6; i++) sprite[7 + i] = 21 + i;
        sprite[20] = 31;
        for (x = 0; x < 64; x++) out_c[x] = out_a[x] = (unsigned char)(200 + x);
        for (d = 6; d < 30; d++)
            if (d >= 5 && d < 28 && sprite[23 - (d - 6)])
                out_c[d] = sprite[23 - (d - 6)];
        rv_blit_row_rev(out_a, spans, 30, 5, 28);
        for (x = 0; x < 64; x++, compared++)
            if (out_c[x] != out_a[x]) {
                rv_log("selftest: mirrored row differs at column %d (%d vs %d)", x, out_c[x], out_a[x]);
                return -1;
            }
    }
    return compared;
}

void ships_time_paths(int set_i, int v, long out[5])
{
    const ShipSet __far *s = ships_set(set_i);
    ShipView view;
    int y0, y1, k, rows, i;
    long px;
    if (!full_view(s, v, &view)) {
        for (k = 0; k < 5; k++)
            out[k] = 0;
        return;
    }
    vid_dirty(0, 100);
    vid_dirty_band(&y0, &y1);
    for (k = 0; k < 5; k++) {
        int n = k == 1 ? 240 : k == 2 ? 120 : 600, scale = k == 1 || k == 2 ? 192 : 256;
        unsigned long t0, dt;
        LfRot r;
        if (k == 2) {
            lf_rotate_setup(view.w, view.h, view.ox, view.oy, 128, scale, &r);
            rows = r.oh < 100 ? r.oh : 100;
            px = (long)r.ow * rows;
        } else {
            int ow = (int)((long)view.w * scale / 256), oh = (int)((long)view.h * scale / 256);
            rows = oh < 100 ? oh : 100;
            px = (long)ow * rows;
        }
        t0 = clock240();
        for (i = 0; i < n; i++) {
            /* the origin so that the sprite's top is row 0 */
            int oy = k == 2 ? r.ocy : k == 3 ? view.h - 1 - view.oy : (int)((long)view.oy * scale / 256), ox = SCR_W / 2;
            if (k == 0)
                ships_blit_view(&view, 0, ox, oy, y0, y1, 0, SCR_W, 0, 0, 0);
            else if (k == 1)
                draw_scaled(s, v, ox, oy, scale, y0, y1, 0, SCR_W, 0, 0, 0);
            else if (k == 2)
                draw_turned(s, set_i, v, ox, oy, scale, 128, 1, y0, y1, 0, SCR_W, 0, 0, 0);
            else if (k == 3)
                ships_blit_view(&view, BLIT_V, ox, oy, y0, y1, 0, SCR_W, 0, 0, 0);
            else
                ships_blit_view(&view, BLIT_H, ox, oy, y0, y1, 0, SCR_W, 0, 0, 0);
        }
        dt = clock240() - t0;
        out[k] = dt ? px * n * 24 / (long)(dt * 100) : 0;   /* pixels a millisecond: ticks are 1000/240 ms */
    }
    vid_flip();
}
