/* i86.h on the host: the 8086 words the game uses (Watcom's <i86.h>), answered by host/hle.c instead of the BIOS,
 * DOS and the drivers (docs/engine.md, "The native build").  A far pointer is the host's own (32 bits: the game
 * is built 32-bit, -m32 or wasm32): FP_SEG and FP_OFF are its two halves, so the XMS moves that carry an address
 * as seg:off (xms.c) carry the host's; MK_FP makes a pointer of a real-mode address the game knows (the BIOS data
 * at 40h, the VESA window at A000h, DOS's lists, an upper memory block hle.c handed out) or puts two halves back. */
#ifndef RV_HOST_I86_H
#define RV_HOST_I86_H

#include <stdint.h>

struct WORDREGS { unsigned short ax, bx, cx, dx, si, di; unsigned int cflag; };
struct BYTEREGS { unsigned char al, ah, bl, bh, cl, ch, dl, dh; };
union REGS { struct WORDREGS x; struct BYTEREGS h; };
struct SREGS { unsigned short es, cs, ss, ds; };
struct REGPACKW { unsigned short ax, bx, cx, dx, bp, si, di, ds, es; unsigned int flags; };
union REGPACK { struct REGPACKW x; struct REGPACKW w; struct BYTEREGS h; };
#define INTR_CF 0x0001
#define INTR_ZF 0x0040

int int86(int n, const union REGS *in, union REGS *out);
int int86x(int n, const union REGS *in, union REGS *out, struct SREGS *s);
void intr(int n, union REGPACK *r);
int intdos(const union REGS *in, union REGS *out);
int intdosx(const union REGS *in, union REGS *out, struct SREGS *s);
void segread(struct SREGS *s);

void *hle_mk_fp(unsigned seg, unsigned off);
#define MK_FP(s, o) hle_mk_fp((unsigned)(s), (unsigned)(o))
#define FP_SEG(p) ((unsigned short)((uintptr_t)(p) >> 16))
#define FP_OFF(p) ((unsigned short)((uintptr_t)(p) & 0xFFFF))

/* interrupts off and on: the host's "interrupts" (the timer's, the keyboard's) come only where the game lets them,
 * at _enable among other places (hle.c), as a pending IRQ does when IF goes back on */
void _disable(void);
void _enable(void);

#endif
