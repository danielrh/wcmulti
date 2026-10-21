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
 * The full-size block (the exact draw's views, the densest lattice) is not
 * loaded: the view a draw needs is streamed from the file into one of a
 * few XMS slots when the frame's plan asks for it (ships_prefetch), at most
 * a read or so a frame, the least recently used slot taken; a draw whose
 * view is not in yet falls back to the sets for that frame.  So a lattice's
 * density costs disk, not XMS: a fighter type keeps 1.7 MB resident at 15
 * degrees instead of 2.5.
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
#define DENSE_BUFS 2
static unsigned char __far *g_dense_buf[DENSE_BUFS];
static int g_dense_set[DENSE_BUFS] = { -1, -1 }, g_dense_view[DENSE_BUFS] = { -1, -1 };
static unsigned g_dense_used[DENSE_BUFS], g_dense_tick;
static unsigned char __far *g_dense;    /* the one a draw is using */
static unsigned short __far *g_cmap;    /* the scaled draw's column map, LINE entries */
static unsigned char __far *g_line;     /* one source row expanded, LINE pixels */

/* the stream's slots: a full-size view each, read from its set's file */
#define STREAM_SLOTS 8
static struct {
    int set, view;                      /* what is in it (-1 none) */
    unsigned used;                      /* for the least recently used */
    unsigned frame;                     /* the frame it was last asked for: not taken in that frame */
    XmsBlock blk;
} g_slot[STREAM_SLOTS];
static int g_nslots = 4, g_reads_frame = 1, g_reads_now;
static unsigned g_slot_tick, g_frame = 1;
static unsigned long g_slot_cap;        /* a slot's bytes: the longest view of the sets loaded */
static unsigned long g_stream_reads, g_stream_ticks, g_stream_waits;

void __far __cdecl rv_blit_row(unsigned char __far *row, const unsigned char __far *spans, int left, int xa, int xb);
void __far __cdecl rv_blit_row_rev(unsigned char __far *row, const unsigned char __far *spans, int right, int xa, int xb);
void __far __cdecl rv_scale_row(const unsigned char __far *line, const unsigned short __far *map, int xa, int xb,
                                unsigned char __far *out);
void __far __cdecl rv_rotate_row(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                 unsigned char __far *out);
void __far __cdecl rv_rotate_row2(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                  unsigned char __far *out);

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
static void file_to_xms(FILE *f, unsigned long at, unsigned long n, XmsBlock blk, unsigned long to)
{
    unsigned long k;
    fseek(f, at, SEEK_SET);
    for (k = 0; k < n; k += sizeof rv_filebuf) {
        unsigned part = n - k < sizeof rv_filebuf ? (unsigned)(n - k) : sizeof rv_filebuf;
        fread(rv_filebuf, 1, part, f);
        xms_put(blk, to + k, rv_filebuf, part);
    }
}

/* the view record (w, h, ox, oy, at) of view v in a block of a set: 1 when it is stored and loaded */
static int set_view(const ShipSet __far *s, const SetBlock __far *b, int v, ShipView *out)
{
    unsigned char e[12];
    if (!b || b->loaded != 1)           /* (2: the streamed block, never resident) */
        return 0;
    xms_get(s->blk, b->index_at + 12UL * v, e, 12);
    out->w = e[0] | (e[1] << 8);
    out->h = e[2] | (e[3] << 8);
    out->ox = (short)(e[4] | (e[5] << 8));
    out->oy = (short)(e[6] | (e[7] << 8));
    out->blk = b->blk;
    out->at = e[8] | ((unsigned long)e[9] << 8) | ((unsigned long)e[10] << 16) | ((unsigned long)e[11] << 24);
    return out->w != 0;
}

static int load_block(ShipSet __far *s, SetBlock __far *b)
{
    FILE *f;
    char name[16];                      /* (near: the file calls take near pointers) */
    if (b->loaded)
        return 1;
    b->blk = xms_alloc(b->length + 2, XMS_MISSION);
    if (!b->blk)
        return 0;
    sprintf(name, "SHIP%02X.RVS", s->type);
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
    s->asym = hd[9];
    s->nb0 = hd[10] ? hd[10] : 5;
    s->nh0 = hd[11] ? hd[11] : 7;
    for (k = 0; k < 16; k++)
        s->scale[k] = hd[12 + 2 * k] | (hd[13 + 2 * k] << 8);
    s->nb1 = hd[44] ? hd[44] : 5;
    s->nh1 = hd[45] ? hd[45] : 7;
    if (hd[46] | hd[47])
        s->nviews = hd[46] | (hd[47] << 8);   /* the whole count (byte 5 is its low byte) */
    nblocks = rd32(f);
    RV_ASSERT(nblocks <= SET_BLOCKS && s->nviews <= SHIP_VIEWS && s->nrolls <= 16);
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
    /* the full-size unturned block stays in the file: its views' records,
     * with where each is and its length (they lie in view order) */
    {
        SetBlock __far *b0 = &s->block[0];
        int prev = -1;
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
            one.at = b0->at + (e[8] | ((unsigned long)e[9] << 8) | ((unsigned long)e[10] << 16) | ((unsigned long)e[11] << 24));
            one.len = 0;
            if (one.w) {
                if (prev >= 0)
                    s->view[prev].len = one.at - s->view[prev].at;
                prev = v;
            }
            s->view[v] = one;
        }
        if (prev >= 0)
            s->view[prev].len = b0->at + b0->length - s->view[prev].at;
        s->max_len = 0;
        for (v = 0; v < s->nviews; v++)
            if (s->view[v].len > s->max_len)
                s->max_len = s->view[v].len;
    }
    s->fd = open(name, O_RDONLY | O_BINARY);
    if (s->fd < 0)
        return 0;
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
    s->fd = -1;
    sprintf(name, "SHIP%02X.RVS", type);
    ok = load_rvs(s, name);
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

static void stream_measure(void);

/* the stream's slots, then the sets' other blocks: the ladder (every size,
 * unturned) for every set, then the rolls, largest size first, while the
 * budget lasts */
unsigned long ships_load_blocks(unsigned long budget)
{
    unsigned long taken = 0;
    int pass, i, k;
    g_slot_cap = 0;
    for (i = 0; i < g_nsets; i++)
        if (g_set[i]->max_len > g_slot_cap)
            g_slot_cap = g_set[i]->max_len;
    for (i = 0; i < g_nslots; i++) {
        g_slot[i].set = g_slot[i].view = -1;
        g_slot[i].blk = 0;
        if (g_slot_cap && taken + g_slot_cap + 2 <= budget) {
            g_slot[i].blk = xms_alloc(g_slot_cap + 2, XMS_MISSION);
            if (g_slot[i].blk)
                taken += g_slot_cap + 2;
        }
    }
    g_stream_reads = g_stream_ticks = g_stream_waits = 0;
    stream_measure();
    for (pass = 0; pass < 2; pass++)
        for (k = 0; k < 16; k++)
            for (i = 0; i < g_nsets; i++) {
                ShipSet __far *s = g_set[i];
                int b;
                if (!s->rvs)
                    continue;
                for (b = 0; b < s->nblocks; b++) {
                    SetBlock __far *blk = &s->block[b];
                    if (blk->loaded || blk->size != k || (pass == 0) != (blk->roll == 0))
                        continue;
                    if (taken + blk->length > budget) {
                        rv_log("ships: the budget is spent at type %02X size %d roll %d", s->type, k, blk->roll);
                        return taken;
                    }
                    if (!load_block(s, blk))
                        return taken;
                    taken += blk->length;
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
    for (i = 0; i < g_nsets; i++) {
        if (g_set[i]->fd >= 0)
            close(g_set[i]->fd);
        _ffree(g_set[i]);
    }
    g_nsets = 0;
    for (i = 0; i < DENSE_BUFS; i++)
        g_dense_set[i] = g_dense_view[i] = -1;
    for (i = 0; i < STREAM_SLOTS; i++) {
        g_slot[i].set = g_slot[i].view = -1;
        g_slot[i].blk = 0;              /* (the mission level frees the block) */
    }
}

void ships_stream_config(int slots, int reads)
{
    if (slots >= 1 && slots <= STREAM_SLOTS)
        g_nslots = slots;
    if (reads >= 0)
        g_reads_frame = reads;
}

void ships_frame(void)
{
    g_frame++;
    g_reads_now = 0;
}

/* --- the stream --- */

static int set_index(const ShipSet __far *s)
{
    int i;
    for (i = 0; i < g_nsets; i++)
        if (g_set[i] == s)
            return i;
    return -1;
}

/* a full-size view's record with its data: resident (a .RV set's), in a
 * slot, or, when may_read and the frame's reads allow, read into the least
 * recently used slot not asked for this frame.  0 when it is not in. */
static const ShipView *stream_view(const ShipSet __far *s, int set_i, int v, int may_read, ShipView *out)
{
    int i, pick = -1;
    unsigned long t0, k;
    *out = s->view[v];
    if (!out->w || out->blk)
        return out;
    for (i = 0; i < g_nslots; i++)
        if (g_slot[i].set == set_i && g_slot[i].view == v) {
            g_slot[i].frame = g_frame;
            g_slot[i].used = ++g_slot_tick;
            out->blk = g_slot[i].blk;
            out->at = 0;
            return out;
        }
    if (!may_read)
        return 0;
    if (g_reads_now >= g_reads_frame || out->len > g_slot_cap || s->fd < 0) {
        g_stream_waits++;
        return 0;
    }
    for (i = 0; i < g_nslots; i++)
        if (g_slot[i].blk && g_slot[i].frame != g_frame && (pick < 0 || g_slot[i].used < g_slot[pick].used))
            pick = i;
    if (pick < 0) {
        g_stream_waits++;
        return 0;
    }
    g_reads_now++;
    g_slot[pick].set = g_slot[pick].view = -1;
    t0 = clock240();
    lseek(s->fd, out->at, SEEK_SET);
    for (k = 0; k < out->len; k += SCRATCH) {
        unsigned part = out->len - k < SCRATCH ? (unsigned)(out->len - k) : SCRATCH, got;
        if (_dos_read(s->fd, gfx_scratch(), part, &got) != 0 || got != part) {
            rv_log("ships: type %02X view %d: the read failed at %lu", s->type, v, k);
            return 0;
        }
        xms_put(g_slot[pick].blk, k, gfx_scratch(), part);
    }
    g_stream_ticks += clock240() - t0;
    g_stream_reads++;
    g_slot[pick].set = set_i;
    g_slot[pick].view = v;
    g_slot[pick].frame = g_frame;
    g_slot[pick].used = ++g_slot_tick;
    out->blk = g_slot[pick].blk;
    out->at = 0;
    return out;
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

int ships_prefetch_view(int set_i, int v)
{
    const ShipSet __far *s = ships_set(set_i);
    ShipView view;
    int flags;
    return stream_view(s, set_i, stored_view(s, v, &flags), 1, &view) != 0;
}

int ships_prefetch(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb0, s->nh0, &p);
    if (!p.on_screen)
        return 1;
    return ships_prefetch_view(set_i, p.view);
}

/* what a read costs here: the longest view of the first set, read into slot 0 256 times */
static void stream_measure(void)
{
    const ShipSet __far *s;
    int v, best = -1, i;
    unsigned long t0;
    if (!g_nsets || !g_slot[0].blk)
        return;
    s = g_set[0];
    for (v = 0; v < s->nviews; v++)
        if (s->view[v].len == s->max_len && !s->view[v].blk)
            best = v;
    if (best < 0)
        return;
    t0 = clock240();
    for (i = 0; i < 256; i++) {
        ShipView view;
        g_slot[0].set = -1;
        g_reads_now = 0;
        stream_view(s, 0, best, 1, &view);
    }
    t0 = clock240() - t0;
    rv_log("ships: the stream: %d slots of %lu bytes; a read of %lu bytes takes %lu.%03lu ms", g_nslots, g_slot_cap,
           s->max_len, t0 * 1000 / 240 / 256, (t0 * 1000000 / 240 / 256) % 1000);
    g_slot[0].set = -1;
    g_stream_reads = g_stream_ticks = g_stream_waits = 0;
}

void ships_stream_log(void)
{
    rv_log("ships: the stream read %lu views in %lu.%02lu ms; %lu times a ship waited a frame for its view",
           g_stream_reads, g_stream_ticks * 1000 / 240, (g_stream_ticks * 100000 / 240) % 100, g_stream_waits);
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
static unsigned long __far *fetch_rows(const ShipView *view, int r0, int *r1, unsigned char __far **data)
{
    unsigned char __far *scratch = gfx_scratch();
    unsigned long __far *tab = (unsigned long __far *)scratch;
    unsigned n = *r1 - r0 + 1, room;
    xms_get(view->blk, view->at + 4UL * r0, tab, 4UL * n);
    room = SCRATCH - 4 * n - 16;
    while (tab[*r1 - r0] - tab[0] > room && *r1 > r0 + 1)
        (*r1)--;
    *data = scratch + 4 * n;
    xms_get(view->blk, view->at + 4UL * (view->h + 1) + tab[0], *data, tab[*r1 - r0] - tab[0]);
    return tab;
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

int ships_blit(const ShipSet __far *s, int v, int x, int y, int y0, int y1, int xa, int xb,
               const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    int flags;
    v = stored_view(s, v, &flags);      /* a mirrored heading not stored: its pair, mirrored */
    if (!stream_view(s, set_index(s), v, 0, &view))
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
    if (!stream_view(s, set_index(s), v, 0, &view))
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

/* the whole view dense in g_dense (one of the buffers, the one it is already
 * in or the least recently used): 0 when it does not fit */
static int make_dense(const ShipSet __far *s, int set_i, int v)
{
    ShipView view;
    int w, h, r0 = 0, i, pick = 0, mir, src;
    g_dense_tick++;
    for (i = 0; i < DENSE_BUFS; i++)
        if (g_dense_set[i] == set_i && g_dense_view[i] == v) {
            g_dense = g_dense_buf[i];
            g_dense_used[i] = g_dense_tick;
            return 1;
        }
    src = stored_view(s, v, &mir);      /* a mirrored heading not stored: its pair, backwards */
    if (!stream_view(s, set_i, src, 0, &view))
        return 0;
    w = view.w;
    h = view.h;
    if (!w || (unsigned long)w * h > DENSE_MAX)
        return 0;
    for (i = 1; i < DENSE_BUFS; i++)
        if (g_dense_used[i] < g_dense_used[pick])
            pick = i;
    if (!g_dense_buf[pick]) g_dense_buf[pick] = (unsigned char __far *)_fmalloc(DENSE_MAX);
    RV_ASSERT(g_dense_buf[pick] != 0);
    g_dense = g_dense_buf[pick];
    while (r0 < h) {
        unsigned long __far *tab;
        unsigned char __far *data;
        int r1 = h, r;
        tab = fetch_rows(&view, r0, &r1, &data);
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
    g_dense_set[pick] = set_i;
    g_dense_view[pick] = v;
    g_dense_used[pick] = g_dense_tick;
    return 1;
}

/* the turned draw: inverse mapping from a dense copy.  detail 1 samples
 * every pixel; 2 samples every other row and copies it to the next (the
 * copy is a plain rep movsd); 4 also writes each sample as a 2 x 1 block
 * (rv_rotate_row2): a ship too big for the budget is drawn coarser rather
 * than slower. */
static int draw_turned(const ShipSet __far *s, int set_i, int v, int x, int y, int scale256, int roll, int detail,
                       int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    ShipView view;
    LfRot r;
    int top, left, oy;
    if (!make_dense(s, set_i, v))
        return 0;
    if (mirrored_index(s, v)) {         /* the pair's picture, mirrored: so is its origin */
        view = s->view[lf_pair_of(v, s->nb0, s->nh0)];
        view.ox = view.w - 1 - view.ox;
    } else
        view = s->view[v];
    lf_rotate_setup(view.w, view.h, view.ox, view.oy, roll, scale256, &r);
    top = y - r.ocy;
    left = x - r.ocx;
    if (y0 < top) y0 = top;
    if (y1 > top + r.oh) y1 = top + r.oh;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H) y1 = SCR_H;
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (y0 >= y1 || left >= xb || left + r.ow <= xa)
        return 1;
    if (xa < left) xa = left;
    if (xb > left + r.ow) xb = left + r.ow;
    for (oy = y0; oy < y1; oy++) {
        int ca = xa, cb = xb, xs, xe;
        if (!row_clip(oy, &ca, &cb, ext, ext_y0, ext_rows))
            continue;
        if (detail >= 2 && ((oy - top) & 1) && oy > y0) {
            /* the odd rows: the row above, copied (the sprite's columns only) */
            if (lf_rotate_span(&r, view.w, view.h, oy - top - 1, ca - left, cb - left, &xs, &xe))
                _fmemcpy(vid_row(oy) + left + xs, vid_row(oy - 1) + left + xs, xe - xs);
            continue;
        }
        if (lf_rotate_span(&r, view.w, view.h, oy - top, ca - left, cb - left, &xs, &xe)) {
            if (detail >= 4) {
                rv_rotate_row2(g_dense, view.w, view.h, &r, oy - top, xs, xe, vid_row(oy) + left);
            } else {
                rv_rotate_row(g_dense, view.w, view.h, &r, oy - top, xs, xe, vid_row(oy) + left);
            }
        }
    }
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

int ships_draw(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256,
               int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    int rq, k;
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->rvs ? s->nb1 : s->nb0, s->rvs ? s->nh1 : s->nh0, &p);
    if (!p.on_screen || scale256 < 8)
        return 0;
    if (s->rvs) {
        ShipView view;
        int flags, step = 1024 / s->nrolls;
        rq = ((p.roll + step / 2) & 1023) / step;
        k = size_step(s, scale256);
        /* the sprite as wanted; else a coarser roll; else unturned; else smaller */
        if ((flags = find_sprite(s, k, p.view, rq, &view)) >= 0) {
            ships_blit_view(&view, flags, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
            return 1 + flags;
        }
        if ((rq & 1) && (flags = find_sprite(s, k, p.view, rq & ~1, &view)) >= 0) {
            ships_blit_view(&view, flags, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
            return 1 + flags;
        }
        for (; k < s->nsizes; k++)
            if ((flags = find_sprite(s, k, p.view, 0, &view)) >= 0) {
                ships_blit_view(&view, flags, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
                return 5;
            }
        return 0;
    }
    rq = ((p.roll + 32) & 1023) / 64;   /* the fallback: sixteen steps, drawn at draw time */
    if (rq == 0 && scale256 >= 256) {
        ships_blit(s, p.view, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        return 1;
    }
    if (rq == 0 || !draw_turned(s, set_i, p.view, p.sx, p.sy, scale256 > 256 ? 256 : scale256, rq * 64, 1, y0, y1, xa, xb,
                                ext, ext_y0, ext_rows)) {
        draw_scaled(s, p.view, p.sx, p.sy, scale256 > 256 ? 256 : scale256, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        return 6;
    }
    return 7;
}

int ships_draw_exact(int set_i, const LfVec *rel, const LfBasis *cam, const LfBasis *ship, int scale256, int detail,
                     int y0, int y1, int xa, int xb, const short __far *ext, int ext_y0, int ext_rows)
{
    const ShipSet __far *s = ships_set(set_i);
    LfPlace p;
    lf_place(rel, cam, ship, g_cx, g_cy, g_focal, s->nb0, s->nh0, &p);
    if (!p.on_screen || scale256 < 8)
        return 1;
    if (scale256 == 256 && ((p.roll + 8) & 1023) < 16) {
        ships_blit(s, p.view, p.sx, p.sy, y0, y1, xa, xb, ext, ext_y0, ext_rows);
        return 1;
    }
    return draw_turned(s, set_i, p.view, p.sx, p.sy, scale256, p.roll, detail, y0, y1, xa, xb, ext, ext_y0, ext_rows);
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
    return (long)s->view[v].w * s->view[v].h * scale256 / 256 * scale256 / 256;
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
    g_reads_now = 0;
    if (!stream_view(s, set_i, v, 1, &view)) {
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
