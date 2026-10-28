/* sfx.c: the flight's sound effects (sfx.h).
 *
 * SFX.DAT (tools/sfx.py) is read into XMS when the game starts: its table, then every sound, signed 8-bit at its
 * rate (22,050 Hz).  A voice plays one sound at a volume and a pitch; eight at once, the quietest giving way.  The
 * mixer works in chunks: each playing voice's next stretch fetched from XMS into a small buffer, added into an
 * accumulator at its volume (its pitch by a 1/256 sample step), then the sum clipped to 8 bits.
 *
 * On DOS the card plays the mix from a ring in conventional memory by auto-initialising DMA (DSP 1Ch, 2.0 and
 * later), its rate set by command 41h (the SB16) or a time constant; the ring's interrupts are only acknowledged.
 * Each frame sfx_poll reads where the DMA is (the channel's count register), mixes up to LEAD bytes ahead of it,
 * and silences what the card has played since the last poll, so a stall (a load) plays silence, never stale sound.
 * On the native build the host's clock says how much to mix (hle.c), and the host's audio plays it a fixed lag
 * later, as it plays the OPL3. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <dos.h>
#include <conio.h>
#include <i86.h>
#include "rv.h"
#include "rvdata.h"
#include "sfx.h"

#define VOICES 8
#define CHUNK 512
#define MASTER 176                      /* the sum's scale, /256 (room for a few loud voices at once) */

typedef struct { uint32_t at, len; } SfxEntry;   /* in the block: a sound's samples */
typedef struct {
    uint8_t on, id;
    int16_t vol;
    uint16_t step;                      /* 1/256 samples a sample out */
    uint32_t pos;                       /* 1/256 samples */
} Voice;

static XmsBlock __far g_bank;
static SfxEntry __far g_tab[RV_SFX_COUNT];
static uint16_t __far g_rate;
static Voice __far g_v[VOICES];
static int16_t __far g_acc[CHUNK];
static int8_t __far g_src[2 * CHUNK + 8];
static int16_t __far g_on;
static uint32_t __far g_mixed;

int16_t sfx_reserve(void)
{
    static const char __far s_magic[] = "RVSFX", __far s_none[] = "sfx: no SFX.DAT: no sound effects",
        __far s_in[] = "sfx: %u sounds, %lu KB at %u Hz, in XMS", __far s_file[] = "SFX.DAT", __far s_rb[] = "rb";
    char name[12], mode[4];
    FILE *f;
    uint8_t *buf = rv_filebuf;
    uint16_t n, i;
    uint32_t total = 0, at;
    g_bank = 0;
    _fstrcpy(name, s_file);
    _fstrcpy(mode, s_rb);
    if ((f = fopen(name, mode)) == 0) {
        rv_logf(s_none);
        return 0;
    }
    if (fread(buf, 1, 16, f) != 16 || _fmemcmp(buf, s_magic, 5) || (buf[8] | buf[9] << 8) != 1
        || (n = buf[10] | buf[11] << 8) != RV_SFX_COUNT) {
        fclose(f);
        rv_logf(s_none);
        return 0;
    }
    g_rate = buf[12] | buf[13] << 8;
    for (i = 0; i < n; i++) {
        fread(buf, 1, 8, f);
        g_tab[i].at = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
        g_tab[i].len = buf[4] | (uint32_t)buf[5] << 8 | (uint32_t)buf[6] << 16 | (uint32_t)buf[7] << 24;
        total += g_tab[i].len;
    }
    /* the samples as the file has them, after its table: the table's offsets less where they begin */
    at = 16 + 8UL * n;
    for (i = 0; i < n; i++)
        g_tab[i].at -= at;
    if ((g_bank = xms_alloc(total + 4, XMS_RESIDENT)) == 0) {
        fclose(f);
        return 0;
    }
    for (at = 0; at < total;) {
        uint16_t k = (uint16_t)(total - at < sizeof rv_filebuf ? total - at : sizeof rv_filebuf);
        if (fread(buf, 1, k, f) != k)
            break;
        xms_put(g_bank, at, buf, k);
        at += k;
    }
    fclose(f);
    rv_logf(s_in, n, total / 1024, g_rate);
    return 1;
}

void sfx_play(int16_t id, int16_t vol, int16_t pitch256)
{
    int16_t i, v = -1, same = 0, oldest = -1;
    if (!g_on || id < 0 || id >= RV_SFX_COUNT || vol < 6 || !g_tab[id].len)
        return;
    if (vol > 256)
        vol = 256;
    for (i = 0; i < VOICES; i++) {
        if (!g_v[i].on) {
            if (v < 0)
                v = i;
            continue;
        }
        if (g_v[i].id == id) {
            same++;
            if (oldest < 0 || g_v[i].pos > g_v[oldest].pos)
                oldest = i;
        }
    }
    if (same >= 3)                      /* (three of one sound at most: the oldest of them gives way) */
        v = oldest;
    if (v < 0) {                        /* all playing: the quietest gives way, if it is quieter */
        v = 0;
        for (i = 1; i < VOICES; i++)
            if (g_v[i].vol < g_v[v].vol)
                v = i;
        if (g_v[v].vol > vol)
            return;
    }
    g_v[v].on = 1;
    g_v[v].id = (uint8_t)id;
    g_v[v].vol = vol;
    g_v[v].step = (uint16_t)(pitch256 < 128 ? 128 : pitch256 > 512 ? 512 : pitch256);   /* (an octave either way:
                                           a chunk's samples then fit g_src) */
    g_v[v].pos = 0;
}

/* n (<= CHUNK) samples of every voice, summed, as unsigned 8-bit into out */
static void mix(uint8_t __far *out, uint16_t n)
{
    int16_t k;
    uint16_t i;
    _fmemset(g_acc, 0, n * sizeof g_acc[0]);
    for (k = 0; k < VOICES; k++) {
        Voice __far *v = &g_v[k];
        uint32_t start, avail, need;
        uint16_t frac, got;
        int16_t vol = v->vol;
        if (!v->on)
            continue;
        start = v->pos >> 8;
        frac = (uint16_t)(v->pos & 255);
        avail = g_tab[v->id].len - start;
        need = ((frac + (uint32_t)v->step * n) >> 8) + 2;
        got = (uint16_t)(need < avail ? need : avail);
        xms_get(g_bank, g_tab[v->id].at + start, g_src, got);
        if (v->step == 256) {           /* its own pitch: sample for sample */
            uint16_t m = got < n ? got : n;
            for (i = 0; i < m; i++)
                g_acc[i] += (int16_t)(g_src[i] * vol >> 8);
        } else {
            uint32_t p = frac;          /* (1/256 samples from start) */
            for (i = 0; i < n && (p >> 8) < got; i++, p += v->step)
                g_acc[i] += (int16_t)(g_src[p >> 8] * vol >> 8);
        }
        v->pos += (uint32_t)v->step * n;
        if ((v->pos >> 8) >= g_tab[v->id].len)
            v->on = 0;
    }
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)((int32_t)g_acc[i] * MASTER >> 8);
        out[i] = (uint8_t)((s > 127 ? 127 : s < -128 ? -128 : s) + 128);
    }
    g_mixed += n;
}

uint32_t sfx_mixed(void) { return g_mixed; }

#ifdef RV_HOST
/* --- the native build: the host's clock asks, its audio plays (hle.c) --- */

uint16_t hle_pcm_due(uint16_t rate);    /* samples to mix now */
void hle_pcm_put(const uint8_t *s, uint16_t n);
void hle_pcm_stop(void);

int16_t sfx_flight_start(void)
{
    sfx_flight_end();
    if (!g_bank)
        return 0;
    _fmemset(g_v, 0, sizeof g_v);
    g_mixed = 0;
    g_on = 1;
    return 1;
}

void sfx_flight_end(void)
{
    if (g_on)
        hle_pcm_stop();
    g_on = 0;
}

void sfx_poll(void)
{
    static uint8_t buf[CHUNK];
    uint16_t n, k;
    if (!g_on)
        return;
    for (n = hle_pcm_due(g_rate); n; n -= k) {
        k = n < CHUNK ? n : CHUNK;
        mix(buf, k);
        hle_pcm_put(buf, k);
    }
}

#else
/* --- DOS: the Sound Blaster --- */

#define RING 4096u                      /* the DMA ring: 186 ms */
#define LEAD 1792u                      /* mixed ahead of the card: 81 ms, two frames and a half */

static uint16_t __far g_base, __far g_irq, __far g_dma, __far g_dsp;
static uint8_t __far *__far g_mem;      /* the ring's allocation (twice its size: a half that no 64 KB page splits) */
static uint8_t __far *__far g_ring;
static uint16_t __far g_w, __far g_played;
static void (__interrupt __far *__far g_old)(void);
static uint8_t __far g_mask_was;
static uint32_t __far g_irqs;

static int16_t dsp_write(uint8_t v)
{
    uint16_t i;
    for (i = 0; i < 0xFFFF && (inp(g_base + 0xC) & 0x80); i++)
        ;
    outp(g_base + 0xC, v);
    return 1;
}

static int16_t dsp_read(void)
{
    uint16_t i;
    for (i = 0; i < 0xFFFF; i++)
        if (inp(g_base + 0xE) & 0x80)
            return inp(g_base + 0xA);
    return -1;
}

static int16_t dsp_reset(void)
{
    int16_t i;
    outp(g_base + 6, 1);
    for (i = 0; i < 16; i++)            /* (3 us at least: an ISA read is about 1) */
        inp(g_base + 6);
    outp(g_base + 6, 0);
    for (i = 0; i < 100; i++)
        if (dsp_read() == 0xAA)
            return 1;
    return 0;
}

/* a number of BLASTER's in a base (no strtoul: its table would sit in DGROUP) */
static uint16_t number(const char *p, uint16_t base)
{
    uint16_t v = 0, d;
    for (;; p++) {
        if (*p >= '0' && *p <= '9')
            d = (uint16_t)(*p - '0');
        else if (base == 16 && (*p | 0x20) >= 'a' && (*p | 0x20) <= 'f')
            d = (uint16_t)((*p | 0x20) - 'a' + 10);
        else
            return v;
        v = (uint16_t)(v * base + d);
    }
}

/* the BLASTER variable: A220 I7 D1 ... */
static int16_t blaster(void)
{
    static const char __far s_name[] = "BLASTER";
    char name[8];
    const char *e, *p;
    _fstrcpy(name, s_name);
    if ((e = getenv(name)) == 0)
        return 0;
    g_base = 0x220;
    g_irq = 7;
    g_dma = 1;
    for (p = e; *p; p++) {
        if (*p == 'A' || *p == 'a')
            g_base = number(p + 1, 16);
        else if (*p == 'I' || *p == 'i')
            g_irq = number(p + 1, 10);
        else if (*p == 'D' || *p == 'd')
            g_dma = number(p + 1, 10);
    }
    return g_dma <= 3 && g_irq >= 2 && g_irq <= 15;
}

static void __interrupt __far sb_isr(void)
{
    inp(g_base + 0xE);                  /* the 8-bit interrupt acknowledged */
    g_irqs++;
    if (g_irq >= 8)
        outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

static uint16_t dma_count(void)
{
    uint16_t port = g_dma * 2 + 1, lo, hi;
    outp(0x0C, 0);
    lo = inp(port);
    hi = inp(port);
    return lo | hi << 8;
}

/* where in the ring the card is reading */
static uint16_t dma_pos(void)
{
    uint16_t a = dma_count(), b = dma_count();
    if ((uint16_t)(a - b) > 16)         /* (read again across a carry) */
        a = dma_count();
    return (uint16_t)((RING - 1 - a) & (RING - 1));
}

int16_t sfx_flight_start(void)
{
    static const char __far s_card[] = "sfx: a Sound Blaster at %Xh, IRQ %u, DMA %u, DSP %u.%02u: %u Hz",
        __far s_no[] = "sfx: no sound effects (%s)", __far s_why0[] = "no SFX.DAT", __far s_why1[] = "no BLASTER",
        __far s_why2[] = "the DSP did not answer", __far s_why3[] = "a DSP before 2.0", __far s_why4[] = "no memory";
    static const uint8_t __far k_page[4] = { 0x87, 0x83, 0x81, 0x82 };
    const char __far *why = 0;
    uint32_t phys;
    uint16_t vec, tc, maj = 0, min = 0;
    char w[32];
    sfx_flight_end();
    if (!g_bank)
        why = s_why0;
    else if (!blaster())
        why = s_why1;
    else if (!dsp_reset())
        why = s_why2;
    if (!why) {
        dsp_write(0xE1);
        maj = (uint16_t)dsp_read();
        min = (uint16_t)dsp_read();
        g_dsp = maj << 8 | min;
        if (maj < 2 || maj > 9)
            why = s_why3;
    }
    if (!why && (g_mem = (uint8_t __far *)_fmalloc(2 * RING)) == 0)
        why = s_why4;
    if (why) {
        _fstrcpy(w, why);
        rv_logf(s_no, w);
        return 0;
    }
    phys = ((uint32_t)FP_SEG(g_mem) << 4) + FP_OFF(g_mem);
    g_ring = g_mem;
    if ((phys & 0xFFFF) + RING > 0x10000UL) {   /* (the page's end inside the ring: start at the next page) */
        uint16_t skip = (uint16_t)(0x10000UL - (phys & 0xFFFF));
        g_ring = g_mem + skip;
        phys += skip;
    }
    _fmemset(g_ring, 0x80, RING);
    _fmemset(g_v, 0, sizeof g_v);
    g_w = (uint16_t)LEAD;
    g_played = 0;
    g_mixed = g_irqs = 0;
    /* the interrupt: acknowledged, nothing else */
    vec = g_irq < 8 ? 0x08 + g_irq : 0x70 + g_irq - 8;
    g_old = _dos_getvect(vec);
    _dos_setvect(vec, sb_isr);
    if (g_irq < 8) {
        g_mask_was = (uint8_t)inp(0x21);
        outp(0x21, g_mask_was & ~(1 << g_irq));
    } else {
        g_mask_was = (uint8_t)inp(0xA1);
        outp(0xA1, g_mask_was & ~(1 << (g_irq - 8)));
    }
    /* the DMA channel: single, auto-initialising, memory to the card, the ring */
    outp(0x0A, 4 | g_dma);
    outp(0x0C, 0);
    outp(0x0B, 0x58 | g_dma);
    outp(g_dma * 2, (uint8_t)phys);
    outp(g_dma * 2, (uint8_t)(phys >> 8));
    outp(k_page[g_dma], (uint8_t)(phys >> 16));
    outp(g_dma * 2 + 1, (uint8_t)(RING - 1));
    outp(g_dma * 2 + 1, (uint8_t)((RING - 1) >> 8));
    outp(0x0A, g_dma);
    /* the card: the speaker on, the rate, the block (two a ring), go */
    dsp_write(0xD1);
    if (maj >= 4) {
        dsp_write(0x41);
        dsp_write((uint8_t)(g_rate >> 8));
        dsp_write((uint8_t)g_rate);
    } else {
        tc = (uint16_t)(256 - 1000000UL / g_rate);
        dsp_write(0x40);
        dsp_write((uint8_t)tc);
    }
    dsp_write(0x48);
    dsp_write((uint8_t)(RING / 2 - 1));
    dsp_write((uint8_t)((RING / 2 - 1) >> 8));
    dsp_write(0x1C);
    g_on = 1;
    rv_logf(s_card, g_base, g_irq, g_dma, maj, min, g_rate);
    return 1;
}

void sfx_flight_end(void)
{
    static const char __far s_end[] = "sfx: %lu samples mixed, %lu interrupts";
    uint16_t vec;
    if (g_on) {
        dsp_write(0xD0);                /* halt the DMA, leave auto-init, the speaker off */
        dsp_write(0xDA);
        dsp_write(0xD3);
        outp(0x0A, 4 | g_dma);
        vec = g_irq < 8 ? 0x08 + g_irq : 0x70 + g_irq - 8;
        if (g_irq < 8)
            outp(0x21, (inp(0x21) & ~(1 << g_irq)) | (g_mask_was & (1 << g_irq)));
        else
            outp(0xA1, (inp(0xA1) & ~(1 << (g_irq - 8))) | (g_mask_was & (1 << (g_irq - 8))));
        _dos_setvect(vec, g_old);
        rv_logf(s_end, g_mixed, g_irqs);
    }
    if (g_mem)
        _ffree(g_mem);
    g_mem = g_ring = 0;
    g_on = 0;
}

void sfx_poll(void)
{
    uint16_t p, ahead, n;
    if (!g_on)
        return;
    p = dma_pos();
    /* what the card has played since the last poll: silence (a stall plays nothing stale) */
    while (g_played != p) {
        n = p > g_played ? p - g_played : RING - g_played;
        _fmemset(g_ring + g_played, 0x80, n);
        g_played = (uint16_t)((g_played + n) & (RING - 1));
    }
    ahead = (uint16_t)((g_w - p) & (RING - 1));
    if (ahead > LEAD + 512) {           /* (the card passed the mix: start again just ahead of it) */
        g_w = (uint16_t)((p + 64) & (RING - 1));
        ahead = 64;
    }
    while (ahead < LEAD) {
        n = LEAD - ahead;
        if (n > CHUNK)
            n = CHUNK;
        if (n > RING - g_w)
            n = RING - g_w;
        mix(g_ring + g_w, n);
        g_w = (uint16_t)((g_w + n) & (RING - 1));
        ahead += n;
    }
}
#endif
