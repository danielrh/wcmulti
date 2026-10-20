/* rv.h: the engine's common declarations (docs/engine.md). */
#ifndef RV_H
#define RV_H

#include <stddef.h>

/* Bounds: every index into a fixed table goes through RV_CHECK; a failure
 * stops the game with where and what (rv_fail). */
void rv_fail(const char *file, int line, const char *what, long value, long limit);
#define RV_CHECK(i, n) do { if ((unsigned long)(i) >= (unsigned long)(n)) \
    rv_fail(__FILE__, __LINE__, #i " < " #n, (long)(i), (long)(n)); } while (0)
#define RV_ASSERT(c) do { if (!(c)) rv_fail(__FILE__, __LINE__, #c, 0, 0); } while (0)
#define RV_COUNT(a) (sizeof(a) / sizeof((a)[0]))

void rv_log(const char *fmt, ...);

/* 32-bit registers for the calls int86 cannot make (lowlevel.asm) */
typedef struct { unsigned long eax, ebx, ecx, edx; unsigned short si; } RvRegs;
void __far __cdecl rv_xms(RvRegs *r);

/* XMS (xms.c): blocks of extended memory, moved with the driver's function 0Bh.
 * A block belongs to a level and every level's blocks can be freed at once. */
enum { XMS_RESIDENT, XMS_BASE, XMS_ROOM, XMS_MISSION, XMS_LEVELS };
#define XMS_MAX_BLOCKS 48
typedef unsigned short XmsBlock;       /* index into the block table, 0 = none */
int xms_init(unsigned long want_kb);   /* allocates one arena of want_kb; 0 on failure */
unsigned long xms_free_kb(void);       /* free in the arena */
XmsBlock xms_alloc(unsigned long bytes, int level);
void xms_free_level(int level);        /* frees that level and all above it */
int xms_put(XmsBlock b, unsigned long off, const void __far *src, unsigned long n);
int xms_get(XmsBlock b, unsigned long off, void __far *dst, unsigned long n);
int xms_copy_conv(void __far *dst, const void __far *src, unsigned long n); /* host-speed copy */
void xms_shutdown(void);

/* the screen (video.c): VESA 100h, 640x400x256, a back buffer in conventional
 * memory as 4 bands of 100 rows; only rows marked dirty are copied out. */
#define SCR_W 640
#define SCR_H 400
int vid_init(void);
void vid_shutdown(void);
unsigned char __far *vid_row(int y);
void vid_dirty(int y0, int y1);        /* rows y0 .. y1-1 need drawing and copying */
int vid_dirty_band(int *y0, int *y1);  /* the next dirty run of rows, 0 when none: draw them */
void vid_flip(void);                   /* copy the drawn dirty rows to the screen */
void vid_palette(const unsigned char __far *rgb6, int first, int n);
void vid_fade(int level);              /* 0 black .. 64 full: the current palette scaled */
void vid_wait_retrace(void);

/* the archive (dat.c): RV.DAT, all of it in XMS */
#include "rvasset.h"
int dat_open(const char *path);
void dat_close(void);                  /* frees the archive's XMS (the base level) and its tables */
const char *dat_base_file(int base);   /* RV.DAT for the carrier, RVBASEn.DAT for base n */
const RvAsset __far *dat_asset(unsigned i);
const RvRoom __far *dat_room(unsigned i);
const RvPerson __far *dat_person(unsigned i);
const RvCockpit __far *dat_cockpit(unsigned i);
const RvDatHeader __far *dat_header(void);
void dat_read(unsigned long at, void __far *dst, unsigned long n);

/* drawing (gfx.c), every call clipped to the rows y0 .. y1-1 being drawn */
#define AT_ASSET (-32768)              /* x or y: where the asset says */
unsigned char __far *gfx_scratch(void);   /* the 32 KB scratch buffer (sprites rows pass through it) */
void gfx_use_palette(unsigned asset);  /* full palette asset: colours, shade and text tables */
void gfx_use_part(unsigned asset, int with_text);  /* a palette part (close-ups) */
void gfx_image(unsigned a, int x, int y, int y0, int y1);
void gfx_sprite(unsigned a, int dx, int dy, int y0, int y1);
void gfx_sprite_dither(unsigned a, int dx, int dy, int y0, int y1, int which);  /* every other pixel */
int gfx_sprite_hit(unsigned a, int dx, int dy, int px, int py);
void gfx_fill(int x, int y, int w, int h, unsigned char c, int y0, int y1);
void gfx_shade(int x, int y, int w, int h, int y0, int y1);
void gfx_mix_to_black(int x, int y, int w, int h, int y0, int y1);
void gfx_line(int xa, int ya, int xb, int yb, unsigned char c, int dash, int y0, int y1);
void gfx_circle(int cx, int cy, int r, unsigned char c, int dotted, int y0, int y1);
void gfx_frame(int x, int y, int w, int h, unsigned char c, int y0, int y1);
enum { FONT_BODY, FONT_TITLE };
int gfx_font_load(void);
int gfx_text(int font, int x, int y, const char *s, int colour, int y0, int y1);   /* RV_TEXT_*; returns width */
int gfx_text_width(int font, const char *s);
int gfx_font_height(int font);
int gfx_wrap(int font, int x, int y, int w, const char *s, int colour, int line_gap, int y0, int y1);  /* lines drawn */

/* input (input.c): the mouse and the keys, or a test script's events */
enum { KEY_NONE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_F12, KEY_TAB, KEY_BACKTAB,
       KEY_CHAR };
typedef struct { int x, y, clicked, key, ch, moved, keyboard; } Input;
int in_init(void);
void in_poll(Input *in);
void in_warp(int x, int y);
void in_steer(int on);                 /* the arrows steer the pointer (on unless a menu takes them) */
int in_arrows(void);                   /* the arrows held: bit 0 left, 1 right, 2 up, 3 down (a script's holds too) */
int in_key_down(int scan);             /* a key held, by scan code */
unsigned long ticks(void);             /* BIOS ticks, 18.2 a second */
int script_open(const char *path);
int script_active(void);
void shot_save(const char *name);      /* the back buffer as NAME.BMP, with the palette shown */
const unsigned char *shot_palette(void);

#endif
