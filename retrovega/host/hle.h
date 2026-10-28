/* hle.h: between host/hle.c (the DOS, BIOS and driver services the game asks for, answered on the host) and the
 * front end that shows the screen, plays the OPL3 and takes the keys and the mouse (host/play.c, SDL2). */
#ifndef RV_HLE_H
#define RV_HLE_H

#include <stdint.h>

#define HLE_W 640
#define HLE_H 400

/* the screen as the VESA card holds it, and the DAC's palette (6 bits a component, as written) */
extern uint8_t hle_fb[HLE_W * HLE_H];
extern uint8_t hle_dac[256 * 3];
extern int hle_fb_changed;              /* set when a move or the DAC changed what shows; the front end clears it */

/* the clock the timer runs on (microseconds), and the input the front end puts in */
void hle_key(int scan, int down);       /* a key by its set 1 scan code; 0x100 | code for an E0 (grey) key */
void hle_mouse_at(int x, int y);        /* the pointer at screen pixel x, y (the mickeys follow the moves) */
void hle_mouse_button(int b, int down); /* 0 left, 1 right */
void hle_mouse_tap(int b);              /* a touch: a press counted, the button never held */
int hle_mouse_state(int *x, int *y);    /* the driver's pointer; its buttons returned */
void hle_pump(void);                    /* the timer's and the keyboard's interrupts that are due, delivered */
void hle_init(void);
void hle_frame_done(void);              /* the flight drew a frame (rvFrameDone): shown */
int hle_xms_move(const void *record);   /* XMS function 0Bh's move (xms.c) */
uint8_t *hle_xms_at(unsigned handle, unsigned long off, unsigned long n);   /* a handle's memory at off */
unsigned long hle_irq_skipped(void);

/* the front end's (play.c) */
uint64_t plat_now_us(void);
void plat_events(void);                 /* SDL's events into hle_key, hle_mouse_* */
void plat_present(void);                /* hle_fb through hle_dac to the window */
void plat_sleep_us(uint32_t us);        /* the host idles (the browser runs its own work: a yield) */
void plat_opl_write(uint64_t at_us, uint16_t reg, uint8_t val);   /* an OPL3 register, as of the clock's at_us */
void plat_pcm(uint64_t at_us, uint16_t rate, const uint8_t *s, uint16_t n);   /* the sound effects' mix: n 8-bit
                                           samples at rate, the first due at at_us (sfx.c through hle.c) */
void plat_saved(void);                  /* the game wrote a save (the page keeps it) */

#endif
