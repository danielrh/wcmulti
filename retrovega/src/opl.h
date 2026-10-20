/* opl.h: MIDI on the OPL3 (opl.c), with our own instrument bank (RVOPL.BNK,
 * tools/oplbank.py).  Plain C: the same file builds into RV.EXE and into the
 * host's tools/oplrender, which plays it through DOSBox's own OPL3. */
#ifndef OPL_H
#define OPL_H

#ifdef __WATCOMC__
#define OPLFAR __far
#else
#define OPLFAR
#endif

/* RVOPL.BNK: a header, then the patches */
#define OPL_BANK_MAGIC "RVOPL\x1A"
typedef struct {
    unsigned char mod[5], car[5];   /* each operator's 20h 40h 60h 80h E0h */
    unsigned char c0;               /* feedback << 1 | connection */
    signed char transpose;          /* semitones */
    signed char detune;             /* F-number steps at F-number 512 (about 3.4 cents) */
    unsigned char vsense;           /* modulator attenuation at velocity 0 (FM): soft notes mellower */
    unsigned char pan;              /* 0 the channel's, 1 left, 2 right */
    unsigned char life;             /* 1/20 s: heard this long while held (0: as long as held) */
    unsigned char rel;              /* 1/20 s: heard this long after the key goes up */
    unsigned char pad[3];
} OplLayer;                         /* 20 bytes */

typedef struct {
    OplLayer layer[2];
    unsigned char layers;           /* 1 or 2 */
    unsigned char note;             /* drums: the key it sounds at (0: the key played) */
    unsigned char pad[2];
} OplPatch;                         /* 44 bytes */

typedef struct {
    char magic[6];
    unsigned char version, npatches;
    unsigned char program[128];     /* General MIDI program -> patch */
    unsigned char drum[128];        /* channel 10's key -> patch (0xFF: none) */
} OplBankHeader;                    /* 264 bytes, then npatches OplPatch */

int opl_bank(const unsigned char OPLFAR *data, unsigned long size);   /* 1 if it is a bank */
void opl_init(void);                /* OPL3 mode, every voice quiet */
void opl_event(unsigned char st, unsigned char d1, unsigned char d2);  /* a MIDI channel message */
void opl_fade(int level);           /* the master fade, 0..1024 */
void opl_silence(void);             /* every voice off at once; controllers back to their defaults */
void opl_tick(void);                /* the music clock (240 Hz): ages the voices */
extern unsigned long opl_steals;    /* notes that took a sounding voice */
extern int opl_peak;                /* the most voices sounding at once */

/* the platform's: one register write (0x000-0x0FF first bank, 0x100-0x1FF second) */
void opl_write(unsigned reg, unsigned char val);
#endif
