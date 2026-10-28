/* asm_twins.c: the host's lowlevel.asm, lasers.asm and rocks.asm: each rv_x is its C twin, c_x (src/twins.c), which
 * RVP.EXE /twins checks against the asm, and tests/host/twins_test.sh shows computes the same on the host as on DOS. */
#include "rv.h"
#include "twins.h"

/* lasers.asm's */
int rv_bolts_place(const LzBolt *b, int n, const LzCam *cam, LzLine *lines, int maxlines, LzFat *fats, int maxfats,
                   int *nfats)
{
    return c_bolts_place(b, n, cam, lines, maxlines, fats, maxfats, nfats);
}

void rv_lines_draw(const LzLine *l, int n, int colour, unsigned char *band, int band_y0, int y0, int y1,
                   const short *ext, int ext_y0)
{
    c_lines_draw(l, n, colour, band, band_y0, y0, y1, ext, ext_y0);
}

void rv_bolts_move(LzBolt *b, int n, long k)
{
    c_bolts_move(b, n, k);
}

void rv_pass_draw(const LzPass *p, unsigned char *band, int band_y0, int y0, int y1, const short *ext, int ext_y0,
                  const unsigned char *roots)
{
    c_pass_draw(p, band, band_y0, y0, y1, ext, ext_y0, roots);
}

int rv_fat_setup(const LzFat *f, const LzStyle *st, LzPass *pass, LzLine *core, int *core_line)
{
    return c_fat_setup(f, st, pass, core, core_line);
}

void rv_capsule_setup(const LzFat *f, int colour, LzPass *pass)
{
    c_capsule_setup(f, colour, pass);
}

void rv_flame_setup(int ax, int ay, int tx, int ty, int r16, int seed, LzFlame *f, LzFat *fp)
{
    c_flame_setup(ax, ay, tx, ty, r16, seed, f, fp);
}

long rv_balls_place(const LzBall *b, int n, const LzCam *cam, const LzBallKind *k, int dmax, unsigned anim,
                    LzBallOut *out, int maxout, const unsigned char *small)
{
    return c_balls_place(b, n, cam, k, dmax, anim, out, maxout, small);
}

void rv_balls_draw(const LzBallOut *o, int n, unsigned char *band, int band_y0, int y0, int y1, const short *ext,
                   int ext_y0)
{
    c_balls_draw(o, n, band, band_y0, y0, y1, ext, ext_y0);
}

void rv_flame_draw(const LzPass *p, unsigned char *band, int band_y0, int y0, int y1, const short *ext, int ext_y0,
                   const unsigned char *roots, const LzFlame *f, const LzFlameLook *look)
{
    c_flame_draw(p, band, band_y0, y0, y1, ext, ext_y0, roots, f, look);
}

/* rocks.asm's */
int rv_rocks_place(LzRock *r, int n, const LzCam *c, LzRockCam *rc, LzRockOut *out, int maxout, short *reap,
                   short *hitl)
{
    return c_rocks_place(r, n, c, rc, out, maxout, reap, hitl);
}

void rv_rocks_xms(const unsigned char *hdr, int rows, int x, int y, unsigned handle, unsigned long off,
                  unsigned char *band, int band_y0, int y0, int y1, const short *ext, int ext_y0)
{
    c_rocks_xms(hdr, rows, x, y, handle, off, band, band_y0, y0, y1, ext, ext_y0);
}

void rv_rocks_copy(const unsigned char *hdr, int rows, int x, int y, int k, const unsigned char *pix,
                   unsigned char *band, int band_y0, int y0, int y1, const short *ext, int ext_y0)
{
    c_rocks_copy(hdr, rows, x, y, k, pix, band, band_y0, y0, y1, ext, ext_y0);
}

/* lowlevel.asm's (but rv_xms: the host's XMS is its own, host/hle.c) */
void rv_blit_row(unsigned char *row, const unsigned char *spans, int left, int xa, int xb)
{
    c_blit_row(row, spans, left, xa, xb);
}

void rv_blit_row_rev(unsigned char *row, const unsigned char *spans, int right, int xa, int xb)
{
    c_blit_row_rev(row, spans, right, xa, xb);
}

void rv_scale_row(const unsigned char *line, const unsigned short *map, int xa, int xb, unsigned char *out)
{
    c_scale_row(line, map, xa, xb, out);
}

void rv_rotate_row(const unsigned char *dense, int w, int h, const LfRot *r, int y, int xs, int xe, unsigned char *out)
{
    c_rotate_row(dense, w, h, r, y, xs, xe, out);
}

void rv_rotate_row2(const unsigned char *dense, int w, int h, const LfRot *r, int y, int xs, int xe, unsigned char *out)
{
    c_rotate_row2(dense, w, h, r, y, xs, xe, out);
}

long rv_rotate_edges(const unsigned char *dense, int w, int h, const LfRot *r, int y, int xs, int xe)
{
    return c_rotate_edges(dense, w, h, r, y, xs, xe);
}

long rv_turn_drawn(const unsigned char *dense, int w, int h, const LfRot *r, int y, int xs, int xe, unsigned char *out)
{
    return c_turn_drawn(dense, w, h, r, y, xs, xe, out);
}

long rv_turn_dropped(const unsigned char *dense, int w, int h, const LfRot *r, int y, int xs, int xe, int pl, int pr,
                     unsigned char *out, const unsigned char *above)
{
    return c_turn_dropped(dense, w, h, r, y, xs, xe, pl, pr, out, above);
}

int rv_stars_place(const short *stars, int n, const long *basis, int cx, int cy, int rows, short *out)
{
    return c_stars_place(stars, n, basis, cx, cy, rows, out);
}
