/* twins.h: the assembly's C twins (twins.c).  c_x does what lowlevel.asm's, lasers.asm's or rocks.asm's rv_x does,
 * to the bit, with the same parameters: the host build draws with them (host/asm_twins.c: each rv_x calls its c_x),
 * and the test build checks them against the asm (RVT.EXE /twins). */
#ifndef TWINS_H
#define TWINS_H

#include "lightfield.h"
#include "lasers.h"
#include "rocks.h"

/* lasers.asm's: the adapters to lasers.c (the asm's parameters to the reference's) */
int c_bolts_place(const LzBolt LZ_FAR *b, int n, const LzCam LZ_FAR *cam, LzLine LZ_FAR *lines, int maxlines,
                  LzFat LZ_FAR *fats, int maxfats, int LZ_FAR *nfats);
void c_lines_draw(const LzLine LZ_FAR *l, int n, int colour, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0);
void c_bolts_move(LzBolt LZ_FAR *b, int n, long k);
void c_pass_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                 const short LZ_FAR *ext, int ext_y0, const unsigned char LZ_FAR *roots);
int c_fat_setup(const LzFat LZ_FAR *f, const LzStyle LZ_FAR *st, LzPass LZ_FAR *pass, LzLine LZ_FAR *core,
                int LZ_FAR *core_line);
void c_capsule_setup(const LzFat LZ_FAR *f, int colour, LzPass LZ_FAR *pass);
void c_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame LZ_FAR *f, LzFat LZ_FAR *fp);
long c_balls_place(const LzBall LZ_FAR *b, int n, const LzCam LZ_FAR *cam, const LzBallKind LZ_FAR *k, int dmax,
                   unsigned anim, LzBallOut LZ_FAR *out, int maxout, const unsigned char LZ_FAR *small);
void c_balls_draw(const LzBallOut LZ_FAR *o, int n, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0);
void c_flame_draw(const LzPass LZ_FAR *p, unsigned char LZ_FAR *band, int band_y0, int y0, int y1,
                  const short LZ_FAR *ext, int ext_y0, const unsigned char LZ_FAR *roots, const LzFlame LZ_FAR *f,
                  const LzFlameLook LZ_FAR *look);

/* rocks.asm's: the placing an adapter (lz_rocks_place, here: the asm's reference), the big rocks' rows new */
int c_rocks_place(LzRock LZ_FAR *r, int n, const LzCam LZ_FAR *c, LzRockCam LZ_FAR *rc, LzRockOut LZ_FAR *out,
                  int maxout, short LZ_FAR *reap, short LZ_FAR *hitl);
void c_rocks_xms(const unsigned char LZ_FAR *hdr, int rows, int x, int y, unsigned handle, unsigned long off,
                 unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext, int ext_y0);
void c_rocks_copy(const unsigned char LZ_FAR *hdr, int rows, int x, int y, int k, const unsigned char LZ_FAR *pix,
                  unsigned char LZ_FAR *band, int band_y0, int y0, int y1, const short LZ_FAR *ext, int ext_y0);

/* lowlevel.asm's: the scaled and turned rows adapters to lightfield.c, the rest new */
void c_blit_row(unsigned char LZ_FAR *row, const unsigned char LZ_FAR *spans, int left, int xa, int xb);
void c_blit_row_rev(unsigned char LZ_FAR *row, const unsigned char LZ_FAR *spans, int right, int xa, int xb);
void c_scale_row(const unsigned char LZ_FAR *line, const unsigned short LZ_FAR *map, int xa, int xb,
                 unsigned char LZ_FAR *out);
void c_rotate_row(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                  unsigned char LZ_FAR *out);
void c_rotate_row2(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                   unsigned char LZ_FAR *out);
long c_rotate_edges(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe);
long c_turn_drawn(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                  unsigned char LZ_FAR *out);
long c_turn_dropped(const unsigned char LZ_FAR *dense, int w, int h, const LfRot *r, int y, int xs, int xe, int pl,
                    int pr, unsigned char LZ_FAR *out, const unsigned char LZ_FAR *above);
int c_stars_place(const short LZ_FAR *stars, int n, const long LZ_FAR *basis, int cx, int cy, int rows,
                  short LZ_FAR *out);

#if defined(__WATCOMC__) || defined(RV_HOST)
/* lowlevel.asm's (ships.c and flight.c declare them too), on the host host/asm_twins.c's */
void __far __cdecl rv_blit_row(unsigned char __far *row, const unsigned char __far *spans, int left, int xa, int xb);
void __far __cdecl rv_blit_row_rev(unsigned char __far *row, const unsigned char __far *spans, int right, int xa, int xb);
void __far __cdecl rv_scale_row(const unsigned char __far *line, const unsigned short __far *map, int xa, int xb,
                                unsigned char __far *out);
void __far __cdecl rv_rotate_row(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                 unsigned char __far *out);
void __far __cdecl rv_rotate_row2(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                  unsigned char __far *out);
long __far __cdecl rv_rotate_edges(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs,
                                   int xe);
long __far __cdecl rv_turn_drawn(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs, int xe,
                                 unsigned char __far *out);
long __far __cdecl rv_turn_dropped(const unsigned char __far *dense, int w, int h, const LfRot *r, int y, int xs,
                                   int xe, int pl, int pr, unsigned char __far *out, const unsigned char __far *above);
int __far __cdecl rv_stars_place(const short __far *stars, int n, const long __far *basis, int cx, int cy, int rows,
                                 short __far *out);
#endif

#ifdef RV_TESTS
long twins_selftest(void);              /* RVT.EXE /twins: every twin against its asm; < 0 on a mismatch */
#endif

#endif
