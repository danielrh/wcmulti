/* opl.c: MIDI channel messages played on the OPL3's eighteen two-operator
 * voices (docs/engine.md, "Music").
 *
 * Every General MIDI program (and channel 10's every key) names a patch of
 * our own bank, RVOPL.BNK (tools/oplbank.py).  A patch is one or two layers,
 * each a two-operator voice with its own registers, transposition, detune
 * and stereo side: two layers detuned a little and put left and right make
 * a wide pad, a short metallic one over a soft one an electric piano.
 *
 * A note takes a voice per layer: a quiet one first (never used, or its
 * release over), then one whose sound has died away by itself while held
 * (the bank says how long a layer is heard), then the oldest still
 * releasing, and only then the oldest sounding (counted in opl_steals).
 *
 * Loudness is attenuation in the operators' total level (0.75 dB steps):
 * the patch's own, then channel volume (7) and expression (11) on a 40 log10
 * curve as General MIDI has it, velocity and the master fade on 20 log10
 * (gentler: the quiet songs play softly without fading into the floor).
 * In FM the modulator's level is the patch's brightness, so only the
 * carrier is turned down (and the modulator by vsense for soft notes); in
 * additive mode both are.
 *
 * Plain C, no DOS: the platform writes the registers (opl_write). */
#include "opl.h"

#define VOICES 18
#define TICKS_20TH 12               /* music clock ticks in a twentieth of a second */

typedef struct {
    unsigned char ch;               /* the MIDI channel; 0xFF: quiet since the last silence */
    unsigned char note, vel;
    unsigned char on;               /* keyed on */
    unsigned char held;             /* key up, but the sustain pedal keeps it on */
    unsigned char b0;               /* block and F-number's top as written, without the key bit */
    unsigned char cached;           /* 1 + patch * 2 + layer whose registers are in place (0: none) */
    unsigned short at;              /* the clock at key on, or at key off */
    const OplLayer OPLFAR *l;
} Voice;

typedef struct {
    unsigned char prog, vol, expr, pan, sustain;
} Channel;

static const OplBankHeader OPLFAR *g_hdr;
static const OplPatch OPLFAR *g_patch;
static Voice g_v[VOICES];
static Channel g_ch[16];
static unsigned short g_now;
static unsigned char g_fade_att;
unsigned long opl_steals;
int opl_peak;

/* 40 log10(127 / x) in 0.75 dB steps, at most 63 */
static const unsigned char g_att40[128] = {
    63, 63, 63, 63, 63, 63, 63, 63, 63, 61, 59, 57, 55, 53, 51, 49, 48, 47, 45, 44, 43, 42, 41, 40, 39, 38, 37,
    36, 35, 34, 33, 33, 32, 31, 31, 30, 29, 29, 28, 27, 27, 26, 26, 25, 25, 24, 24, 23, 23, 22, 22, 21, 21, 20,
    20, 19, 19, 19, 18, 18, 17, 17, 17, 16, 16, 16, 15, 15, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 11, 11, 11,
    10, 10, 10, 10, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0
};

/* F-numbers of C4..B4 at block 4 */
static const unsigned short g_fnum[12] = { 345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 615, 651 };
static const unsigned char g_op[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

#define BANK(v) ((v) >= 9 ? 0x100 : 0)
#define MOD(v) (BANK(v) + g_op[(v) % 9])
#define CAR(v) (MOD(v) + 3)
#define CHAN(v) (BANK(v) + (v) % 9)

int opl_bank(const unsigned char OPLFAR *data, unsigned long size)
{
    const OplBankHeader OPLFAR *h = (const OplBankHeader OPLFAR *)data;
    int i;
    if (size < sizeof(OplBankHeader))
        return 0;
    for (i = 0; i < 6; i++)
        if (h->magic[i] != OPL_BANK_MAGIC[i])
            return 0;
    if (h->version != 1 || !h->npatches || size < sizeof(OplBankHeader) + (unsigned long)h->npatches * sizeof(OplPatch))
        return 0;
    g_hdr = h;
    g_patch = (const OplPatch OPLFAR *)(data + sizeof(OplBankHeader));
    return 1;
}

static void reset_channels(void)
{
    int c;
    for (c = 0; c < 16; c++) {
        g_ch[c].prog = 0;
        g_ch[c].vol = 100;
        g_ch[c].expr = 127;
        g_ch[c].pan = 64;
        g_ch[c].sustain = 0;
    }
}

void opl_init(void)
{
    int v;
    opl_write(0x105, 1);            /* OPL3 */
    opl_write(0x104, 0);            /* no four-operator voices */
    opl_write(0x001, 0x20);         /* waveforms */
    opl_write(0x008, 0);
    opl_write(0x0BD, 0);            /* shallow tremolo (1 dB) and vibrato (7 cents), no rhythm mode */
    for (v = 0; v < VOICES; v++) {
        opl_write(0xB0 + CHAN(v), 0);
        opl_write(0x40 + MOD(v), 0x3F);
        opl_write(0x40 + CAR(v), 0x3F);
        opl_write(0x80 + MOD(v), 0xFF);
        opl_write(0x80 + CAR(v), 0xFF);
        opl_write(0xC0 + CHAN(v), 0x30);
        g_v[v].ch = 0xFF;
        g_v[v].on = g_v[v].held = 0;
        g_v[v].cached = 0;
    }
    g_fade_att = 0;
    reset_channels();
}

void opl_tick(void)
{
    g_now++;
}

/* the voice's two levels: the patch's, the note's, the channel's, the fade */
static void level(int v)
{
    const Voice *p = &g_v[v];
    const OplLayer OPLFAR *l = p->l;
    const Channel *c = &g_ch[p->ch];
    int att = ((g_att40[p->vel] + 1) >> 1) + g_att40[c->vol] + g_att40[c->expr] + g_fade_att;
    int car = (l->car[1] & 63) + att, mod;
    if (l->c0 & 1)
        mod = (l->mod[1] & 63) + att;
    else
        mod = (l->mod[1] & 63) + ((l->vsense * (127 - p->vel)) >> 7);
    opl_write(0x40 + CAR(v), (unsigned char)((l->car[1] & 0xC0) | (car > 63 ? 63 : car)));
    opl_write(0x40 + MOD(v), (unsigned char)((l->mod[1] & 0xC0) | (mod > 63 ? 63 : mod)));
}

static void key_off(int v)
{
    Voice *p = &g_v[v];
    opl_write(0xB0 + CHAN(v), p->b0);
    p->on = p->held = 0;
    p->at = g_now;
}

/* the voice to play a new note on (see the top) */
static int take_voice(void)
{
    int v, best = 0, busy = 0;
    unsigned bscore = 0;
    for (v = 0; v < VOICES; v++) {
        const Voice *p = &g_v[v];
        unsigned age = (unsigned short)(g_now - p->at), score;
        if (age > 0x3FFF)
            age = 0x3FFF;
        if (p->ch == 0xFF)
            score = 0xFFFF;
        else if (!p->on)
            score = age >= (unsigned)p->l->rel * TICKS_20TH ? 0xC000 + age : 0x4000 + age;
        else if (p->l->life && age >= (unsigned)p->l->life * TICKS_20TH)
            score = 0x8000 + age;
        else
            score = age;
        if (score < 0x8000)
            busy++;                     /* sounding, or still releasing */
        if (score > bscore || v == 0) {
            bscore = score;
            best = v;
        }
    }
    if (bscore < 0x4000)
        opl_steals++;
    if (busy + 1 > opl_peak)
        opl_peak = busy + 1;
    return best;
}

static void note_on(int ch, int note, int vel)
{
    const OplPatch OPLFAR *pt;
    int pi, k;
    if (!g_hdr)
        return;
    pi = ch == 9 ? g_hdr->drum[note] : g_hdr->program[g_ch[ch].prog];
    if (pi == 0xFF || pi >= g_hdr->npatches) {
        if (ch == 9)
            return;
        pi = 0;
    }
    pt = &g_patch[pi];
    for (k = 0; k < pt->layers && k < 2; k++) {
        const OplLayer OPLFAR *l = &pt->layer[k];
        int v = take_voice(), key, block, side;
        unsigned fnum;
        Voice *p = &g_v[v];
        if (p->ch != 0xFF && p->on)
            opl_write(0xB0 + CHAN(v), p->b0);          /* a key on must follow a key off */
        p->ch = (unsigned char)ch;
        p->note = (unsigned char)note;
        p->vel = (unsigned char)vel;
        p->l = l;
        p->on = 1;
        p->held = 0;
        p->at = g_now;
        if (p->cached != 1 + pi * 2 + k) {
            int i;
            for (i = 0; i < 5; i++) {
                static const unsigned char base[5] = { 0x20, 0x40, 0x60, 0x80, 0xE0 };
                if (i == 1)
                    continue;                           /* the levels: level() */
                opl_write(base[i] + MOD(v), l->mod[i]);
                opl_write(base[i] + CAR(v), l->car[i]);
            }
            p->cached = (unsigned char)(1 + pi * 2 + k);
        }
        side = l->pan == 1 ? 0x10 : l->pan == 2 ? 0x20 : g_ch[ch].pan < 43 ? 0x10 : g_ch[ch].pan > 85 ? 0x20 : 0x30;
        opl_write(0xC0 + CHAN(v), (unsigned char)(side | (l->c0 & 0x0F)));
        level(v);
        key = (pt->note ? pt->note : note) + l->transpose;
        if (key < 0) key = 0;
        if (key > 127) key = 127;
        block = key / 12 - 1;
        fnum = g_fnum[key % 12];
        if (block < 0) {
            fnum >>= -block;
            block = 0;
        }
        while (block > 7) {
            fnum <<= 1;
            block--;
        }
        fnum += (int)(((long)l->detune * (long)fnum) >> 9);
        if (fnum > 1023)
            fnum = 1023;
        p->b0 = (unsigned char)((block << 2) | (fnum >> 8));
        opl_write(0xA0 + CHAN(v), (unsigned char)(fnum & 0xFF));
        opl_write(0xB0 + CHAN(v), (unsigned char)(p->b0 | 0x20));
    }
}

static void note_off(int ch, int note)
{
    int v;
    for (v = 0; v < VOICES; v++) {
        Voice *p = &g_v[v];
        if (p->ch == ch && p->note == note && p->on && !p->held) {
            if (g_ch[ch].sustain)
                p->held = 1;
            else
                key_off(v);
        }
    }
}

static void control(int ch, int cc, int val)
{
    int v;
    Channel *c = &g_ch[ch];
    switch (cc) {
    case 7:
    case 11:
        if (cc == 7)
            c->vol = (unsigned char)val;
        else
            c->expr = (unsigned char)val;
        for (v = 0; v < VOICES; v++)
            if (g_v[v].ch == ch)
                level(v);
        break;
    case 10:
        c->pan = (unsigned char)val;
        break;
    case 64:
        c->sustain = val >= 64;
        if (!c->sustain)
            for (v = 0; v < VOICES; v++)
                if (g_v[v].ch == ch && g_v[v].held)
                    key_off(v);
        break;
    case 121:                       /* reset controllers: not volume or pan */
        c->expr = 127;
        control(ch, 64, 0);
        break;
    case 120:
    case 123:                       /* all notes (or sound) off */
        for (v = 0; v < VOICES; v++)
            if (g_v[v].ch == ch && g_v[v].on)
                key_off(v);
        break;
    }
}

void opl_event(unsigned char st, unsigned char d1, unsigned char d2)
{
    int ch = st & 15;
    d1 &= 127;
    d2 &= 127;
    switch (st & 0xF0) {
    case 0x90:
        if (d2) {
            note_on(ch, d1, d2);
            break;
        }
        /* a key on at velocity 0 is a key off */
    case 0x80:
        note_off(ch, d1);
        break;
    case 0xB0:
        control(ch, d1, d2);
        break;
    case 0xC0:
        g_ch[ch].prog = d1;
        break;
    }
}

void opl_fade(int level_)
{
    int v;
    unsigned char att = level_ >= 1024 ? 0 : level_ <= 0 ? 63
                      : (unsigned char)((g_att40[(int)((long)level_ * 127 >> 10)] + 1) >> 1);
    if (att == g_fade_att)
        return;
    g_fade_att = att;
    for (v = 0; v < VOICES; v++)
        if (g_v[v].ch != 0xFF)
            level(v);
}

void opl_uncache(void)
{
    int v;
    for (v = 0; v < VOICES; v++)
        g_v[v].cached = 0;
}

void opl_silence(void)
{
    int v;
    for (v = 0; v < VOICES; v++) {
        Voice *p = &g_v[v];
        if (p->ch != 0xFF) {
            opl_write(0x80 + MOD(v), (unsigned char)(p->l->mod[3] | 0x0F));    /* the fastest release */
            opl_write(0x80 + CAR(v), (unsigned char)(p->l->car[3] | 0x0F));
            opl_write(0xB0 + CHAN(v), p->b0);
        }
        p->ch = 0xFF;
        p->on = p->held = 0;
        p->cached = 0;
    }
    reset_channels();
}
