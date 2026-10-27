/* hle.c: what RV.EXE asks of DOS, the BIOS, its drivers and the PC's ports, answered on the host, so the game's
 * own sources (src/) build unchanged for a 32-bit host or the browser (docs/engine.md, "The native build").
 *
 * Only what the game uses is here: int 10h (VESA mode 100h, its bank window), int 16h (the enhanced keystroke
 * calls), int 21h (upper memory blocks, the largest free block, DOS's list of lists for the memory walk), int 2Fh
 * and the driver's calls (XMS: one arena of host memory), int 33h (the mouse), and the ports: the DAC (3C8h,
 * 3C9h), the retrace (3DAh), the OPL3 (388h-38Bh: to the front end's chip, the status faked for the detection),
 * the PIT (40h, 43h: the timer's rate), the PIC (20h), the keyboard's data (60h); an MPU-401 that is not there.
 *
 * Interrupts: the timer's (int 8, at the rate the game programs: 240 a second while the music runs) and the
 * keyboard's (int 9, a scan code at a time) come on the host clock, delivered where the game lets them: at
 * _enable, at rv_halt (the game's sti/hlt), and wherever it reads the BIOS clock or asks the mouse or the keyboard.
 * The handlers the game installs run as they would (music.c's isr, input.c's key_isr) and chain to the BIOS's,
 * which are here (the tick count at 40:6C, the keystroke buffer).
 *
 * A far pointer is the host's (the build is 32-bit): FP_SEG and FP_OFF are its halves, put back together by the
 * XMS move; MK_FP of a real-mode segment the game knows gives the memory it means (i86.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "dos.h"
#include "conio.h"
#include "malloc.h"
#include "rv.h"
#include "hle.h"

uint8_t hle_fb[HLE_W * HLE_H];
uint8_t hle_dac[256 * 3];
int hle_fb_changed = 1;

static void hle_stop(const char *what, long value)
{
    fprintf(stderr, "hle: %s (%ld)\n", what, value);
    rv_fail(__FILE__, __LINE__, what, value, 0);
}

/* --- real-mode memory the game addresses by segment --- */

static uint8_t g_low[0x1000];           /* 0:0 .. 100:0: the BIOS's data (40:6C the ticks), DOS's list and an MCB */
static uint8_t g_vga_win[0x10000];      /* A000:0 (only its addresses are used: a move to or from it is the screen's) */
static uint8_t g_umb[0x20000] __attribute__((aligned(16)));   /* D000:0 .. F000:0, DOSBox's 128 KB of UMBs */
static uint32_t g_umb_top;
#define LIST_SEG 0x50                   /* DOS's list of lists: its word at -2, the first MCB's segment */
#define LIST_OFF 0x10
#define MCB_SEG 0x80                    /* the one MCB: free, the rest of conventional memory */
#define DOS_FREE_KB 288                 /* (what a DOSBox run logs at the start) */

void *hle_mk_fp(unsigned seg, unsigned off)
{
    seg &= 0xFFFF;
    off &= 0xFFFF;
    if (seg < 0x100) {
        if (seg == 0x40)
            hle_pump();                 /* (the BIOS's clock read: the time goes on) */
        if (seg * 16U + off >= sizeof g_low)
            hle_stop("MK_FP into low memory past what is kept", (long)seg * 16 + off);
        return g_low + seg * 16U + off;
    }
    if (seg == 0xA000)
        return g_vga_win + off;
    if (seg >= 0xD000 && seg < 0xF000)
        return g_umb + ((uint32_t)(seg - 0xD000) << 4) + off;
    return (void *)(uintptr_t)(((uint32_t)seg << 16) | off);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

/* --- the clock and the interrupts --- */

#define PIT_HZ 1193182ULL
static hle_isr g_vec[256];
static int g_if = 1, g_in_irq;
static uint32_t g_div = 65536;          /* the PIT's divisor (0 written: 65536) */
static uint64_t g_base_us, g_base_irq, g_irq_done;   /* the timer's interrupts counted from base, at the divisor */
static uint64_t g_irq_us;               /* the one being delivered: when it was due (the OPL's writes are stamped so) */
static uint64_t g_events_us, g_present_us, g_yield_us;
static unsigned long g_irq_skipped;
static uint8_t g_pit_lo, g_pit_phase;
static uint8_t g_port60;
static uint16_t g_keyq[64];             /* scan codes waiting for int 9 */
static unsigned g_keyq_rd, g_keyq_wr;
static unsigned long g_irq_any;         /* every interrupt delivered (rv_halt waits for the next) */

static uint64_t irq_due_us(uint64_t k)
{
    return g_base_us + (k - g_base_irq) * g_div * 1000000ULL / PIT_HZ;
}

static void bios_int8(void)
{
    wr32(g_low + 0x46C, rd32(g_low + 0x46C) + 1);
}

/* the BIOS's int 9: a make code to the keystroke buffer, as the enhanced BIOS gives them (scan code, character) */
static uint16_t g_kbuf[32];
static unsigned g_kbuf_rd, g_kbuf_wr;
static int g_shift_l, g_shift_r, g_ctrl, g_alt, g_caps, g_e0;
static const char k_plain[0x3A] =
    "\0\x1b" "1234567890-=\b\t" "qwertyuiop[]\r\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 ";
static const char k_shift[0x3A] =
    "\0\x1b" "!@#$%^&*()_+\b\0" "QWERTYUIOP{}\r\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?\0*\0 ";

static void bios_put(uint8_t scan, uint8_t ch)
{
    if ((g_kbuf_wr + 1) % 32 == g_kbuf_rd % 32 && g_kbuf_wr - g_kbuf_rd >= 31)
        return;                         /* (full: the BIOS beeps) */
    g_kbuf[g_kbuf_wr++ % 32] = (uint16_t)(scan << 8 | ch);
}

static void bios_int9(void)
{
    uint8_t sc = g_port60, code = sc & 0x7F;
    int down = !(sc & 0x80), e0 = g_e0;
    if (sc == 0xE0) {
        g_e0 = 1;
        return;
    }
    g_e0 = 0;
    switch (code) {
    case 0x2A: if (!e0) g_shift_l = down; return;
    case 0x36: if (!e0) g_shift_r = down; return;
    case 0x1D: g_ctrl = down; return;
    case 0x38: g_alt = down; return;
    case 0x3A: if (down) g_caps ^= 1; return;
    }
    if (!down)
        return;
    if (e0) {
        if (code == 0x1C) bios_put(0xE0, 0x0D);                 /* the keypad's Enter */
        else if (code == 0x35) bios_put(0xE0, '/');
        else bios_put(code, 0xE0);                              /* the grey arrows, Home, End ... */
        return;
    }
    if (code == 0x57) { bios_put(0x85, 0); return; }            /* F11 */
    if (code == 0x58) { bios_put(0x86, 0); return; }            /* F12 */
    if (code < 0x3A) {
        int shift = g_shift_l || g_shift_r;
        char c = k_plain[code];
        if (c >= 'a' && c <= 'z' && g_caps)
            shift = !shift;
        c = shift ? k_shift[code] : c;
        if (code == 0x0F && shift)
            c = 0;                                              /* Shift+Tab */
        if (g_ctrl && c >= 'a' - 32 && (c | 32) >= 'a' && (c | 32) <= 'z')
            c = (char)((c | 32) - 'a' + 1);
        if (g_alt)
            c = 0;
        bios_put(code, (uint8_t)c);
        return;
    }
    bios_put(code, 0);                                          /* F1 .. F10, the keypad's keys */
}

void hle_key(int scan, int down)
{
    if (scan & 0x100) {
        if (g_keyq_wr - g_keyq_rd < 63)
            g_keyq[g_keyq_wr++ % 64] = 0xE0;
        scan &= 0xFF;
    }
    if (g_keyq_wr - g_keyq_rd < 64)
        g_keyq[g_keyq_wr++ % 64] = (uint16_t)(scan | (down ? 0 : 0x80));
}

void hle_pump(void)
{
    uint64_t now, due;
    if (!g_if || g_in_irq)
        return;
    now = plat_now_us();
    if (!g_base_us) {
        g_base_us = now;
        g_events_us = now;
    }
    if (now - g_events_us >= 2000) {
        g_events_us = now;
        plat_events();
    }
    g_in_irq = 1;
    while (g_keyq_rd != g_keyq_wr) {
        g_port60 = (uint8_t)g_keyq[g_keyq_rd++ % 64];
        g_irq_us = now;
        g_irq_any++;
        if (g_vec[9])
            g_vec[9]();
    }
    due = g_base_irq + (now - g_base_us) * PIT_HZ / 1000000ULL / g_div;
    if (due - g_irq_done > PIT_HZ / g_div / 4 + 2) {             /* a quarter second behind (the host was away) */
        g_irq_skipped += (unsigned long)(due - g_irq_done - 1);
        g_irq_done = due - 1;
    }
    while (g_irq_done < due) {
        g_irq_done++;
        g_irq_us = irq_due_us(g_irq_done);
        g_irq_any++;
        if (g_vec[8])
            g_vec[8]();
    }
    g_in_irq = 0;
}

void _disable(void) { g_if = 0; }
void _enable(void) { g_if = 1; hle_pump(); }

hle_isr _dos_getvect(int n) { return g_vec[n & 255]; }
void _dos_setvect(int n, hle_isr isr) { g_vec[n & 255] = isr; }
void _chain_intr(hle_isr isr) { if (isr) isr(); }

static void maybe_present(int now_anyway)
{
    uint64_t now = plat_now_us();
    if (hle_fb_changed && (now_anyway || now - g_present_us >= 15000)) {
        g_present_us = now;
        plat_present();
    }
}

static void idle_us(uint32_t us)
{
    plat_sleep_us(us);
    g_yield_us = plat_now_us();
}

/* the game's sti/hlt: until the next interrupt */
void rv_halt(void)
{
    unsigned long before = g_irq_any;
    uint64_t next, now;
    g_if = 1;
    hle_pump();
    maybe_present(0);
    if (g_irq_any != before)
        return;
    next = irq_due_us(g_irq_done + 1);
    now = plat_now_us();
    idle_us(next > now ? (uint32_t)(next - now) : 0);
    hle_pump();
}

/* the browser shows nothing and takes no input until the game gives it a turn: a game busy for long (a flight
 * slower than its frame rate) gives one at its frames and its polls */
static void keep_alive(void)
{
    if (plat_now_us() - g_yield_us > 40000)
        idle_us(0);
}

void hle_frame_done(void)
{
    maybe_present(1);
    keep_alive();
}

/* --- the VGA and the VESA card --- */

static unsigned g_bank, g_dac_at, g_dac_part, g_retrace;

static unsigned retrace(void)
{
    if (g_retrace) {
        g_retrace = 0;
        return 8;
    }
    maybe_present(1);
    idle_us(16667);                     /* (a frame of a 60 Hz screen) */
    g_retrace = 1;
    return 0;
}

static void int10(union REGS *r, struct SREGS *s)
{
    switch (r->x.ax) {
    case 0x0003:
        break;
    case 0x4F01: {
        uint8_t *m = (uint8_t *)(uintptr_t)(((uint32_t)(s ? s->es : 0) << 16) | r->x.di);
        memset(m, 0, 256);
        wr16(m, 0x9B);                  /* supported, colour, graphics, windowed */
        m[2] = 7;                       /* window A: there, readable, writable */
        wr16(m + 4, 64);                /* granularity, KB */
        wr16(m + 6, 64);                /* size, KB */
        wr16(m + 8, 0xA000);
        wr16(m + 16, HLE_W);            /* bytes a line */
        wr16(m + 18, HLE_W);
        wr16(m + 20, HLE_H);
        m[25] = 8;                      /* bits a pixel */
        r->x.ax = 0x004F;
        break;
    }
    case 0x4F02:
        r->x.ax = 0x004F;
        break;
    case 0x4F05:
        if (r->h.bh == 0)
            g_bank = r->x.dx;
        r->x.ax = 0x004F;
        break;
    default:
        hle_stop("int 10h: a call not answered", r->x.ax);
    }
}

/* --- the keyboard (int 16h) and the mouse (int 33h) --- */

static int intr16(union REGPACK *r)
{
    switch (r->h.ah) {
    case 0x10:
        while (g_kbuf_rd == g_kbuf_wr)
            rv_halt();
        r->x.ax = g_kbuf[g_kbuf_rd++ % 32];
        return 0;
    case 0x11:
        hle_pump();
        keep_alive();
        if (g_kbuf_rd == g_kbuf_wr)
            return 1;                   /* ZF: none */
        r->x.ax = g_kbuf[g_kbuf_rd % 32];
        return 0;
    }
    hle_stop("int 16h: a call not answered", r->h.ah);
    return 0;
}

static int g_mx = HLE_W / 2, g_my = HLE_H / 2, g_mxmin = 0, g_mxmax = HLE_W - 1, g_mymin = 0, g_mymax = HLE_H - 1;
static int g_mbuttons, g_mick_x, g_mick_y, g_mpress[2], g_mpress_x[2], g_mpress_y[2], g_mhost_x = -1, g_mhost_y;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void hle_mouse_at(int x, int y)
{
    if (g_mhost_x >= 0) {
        g_mick_x += x - g_mhost_x;
        g_mick_y += (y - g_mhost_y) * 2;   /* (the driver's ratios: 8 mickeys to 8 pixels across, 16 down) */
    }
    g_mhost_x = x;
    g_mhost_y = y;
    g_mx = clampi(x, g_mxmin, g_mxmax);
    g_my = clampi(y, g_mymin, g_mymax);
}

void hle_mouse_button(int b, int down)
{
    if (b < 0 || b > 1)
        return;
    if (down && !(g_mbuttons & (1 << b))) {
        g_mpress[b]++;
        g_mpress_x[b] = g_mx;
        g_mpress_y[b] = g_my;
    }
    g_mbuttons = down ? g_mbuttons | 1 << b : g_mbuttons & ~(1 << b);
}

/* a touch's press: counted (a click, int 33h function 5), never held: in flight the held button is the guns, and
 * a finger on the picture is there to steer (the touch controls' Fire is the trigger) */
void hle_mouse_tap(int b)
{
    if (b < 0 || b > 1)
        return;
    g_mpress[b]++;
    g_mpress_x[b] = g_mx;
    g_mpress_y[b] = g_my;
}

int hle_mouse_state(int *x, int *y) { *x = g_mx; *y = g_my; return g_mbuttons; }

static void int33(union REGS *r)
{
    int b;
    hle_pump();
    switch (r->x.ax) {
    case 0x00:
        r->x.ax = 0xFFFF;
        r->x.bx = 2;
        break;
    case 0x01: case 0x02:
        break;
    case 0x03:
        keep_alive();
        r->x.bx = (unsigned short)g_mbuttons;
        r->x.cx = (unsigned short)g_mx;
        r->x.dx = (unsigned short)g_my;
        break;
    case 0x04:
        g_mx = clampi((short)r->x.cx, g_mxmin, g_mxmax);
        g_my = clampi((short)r->x.dx, g_mymin, g_mymax);
        break;
    case 0x05:
        b = r->x.bx & 1;
        r->x.ax = (unsigned short)g_mbuttons;
        r->x.bx = (unsigned short)g_mpress[b];
        r->x.cx = (unsigned short)g_mpress_x[b];
        r->x.dx = (unsigned short)g_mpress_y[b];
        g_mpress[b] = 0;
        break;
    case 0x07:
        g_mxmin = (short)r->x.cx;
        g_mxmax = (short)r->x.dx;
        break;
    case 0x08:
        g_mymin = (short)r->x.cx;
        g_mymax = (short)r->x.dx;
        break;
    case 0x0B:
        r->x.cx = (unsigned short)g_mick_x;
        r->x.dx = (unsigned short)g_mick_y;
        g_mick_x = g_mick_y = 0;
        break;
    default:
        hle_stop("int 33h: a call not answered", r->x.ax);
    }
}

/* --- DOS (int 21h) and the XMS driver --- */

static unsigned g_strategy, g_umb_link;
static size_t g_conv_used;              /* the far heap's (hle_fmalloc): what DOS would have handed it */

static void int21(union REGS *r, struct SREGS *s)
{
    r->x.cflag = 0;
    switch (r->h.ah) {
    case 0x48: {
        uint32_t bytes = (uint32_t)r->x.bx << 4;
        if ((g_strategy & 0x40) && g_umb_link && r->x.bx != 0xFFFF && g_umb_top + bytes <= sizeof g_umb) {
            r->x.ax = (unsigned short)(0xD000 + (g_umb_top >> 4));
            g_umb_top += bytes;
            break;
        }
        {                               /* conventional memory: none handed out; the largest block, as DOS would say */
            size_t used = (g_conv_used + 1023) / 1024, kb = used < DOS_FREE_KB ? DOS_FREE_KB - used : 0;
            r->x.ax = (g_strategy & 0x40) ? 8 : 8;
            r->x.bx = (unsigned short)((g_strategy & 0x40) && g_umb_link ? (sizeof g_umb - g_umb_top) >> 4 : kb * 64);
            r->x.cflag = 1;
        }
        break;
    }
    case 0x52:
        if (s)
            s->es = LIST_SEG;
        r->x.bx = LIST_OFF;
        break;
    case 0x58:
        switch (r->h.al) {
        case 0: r->x.ax = (unsigned short)g_strategy; break;
        case 1: g_strategy = r->x.bx; break;
        case 2: r->h.al = (unsigned char)g_umb_link; break;
        case 3: g_umb_link = r->x.bx; break;
        }
        break;
    default:
        hle_stop("int 21h: a call not answered", r->h.ah);
    }
}

static uint8_t *g_xms;
static uint32_t g_xms_kb = 63424;       /* (what DOSBox's memsize=63 gives: the page's) */
static uint32_t g_xms_bytes;

void rv_xms(RvRegs *x)
{
    switch ((x->eax >> 8) & 0xFF) {
    case 0x88:
        x->eax = g_xms ? 0 : g_xms_kb;
        x->edx = g_xms ? 0 : g_xms_kb;
        x->ebx &= ~0xFFUL;
        break;
    case 0x89:
        if (g_xms || x->edx > g_xms_kb || !(g_xms = calloc(x->edx ? x->edx : 1, 1024))) {
            x->eax = 0;
            x->ebx = (x->ebx & ~0xFFUL) | 0xA0;
            break;
        }
        g_xms_bytes = x->edx * 1024;
        x->eax = 1;
        x->edx = 1;
        break;
    case 0x0A:
        free(g_xms);
        g_xms = 0;
        g_xms_bytes = 0;
        x->eax = 1;
        break;
    default:
        hle_stop("XMS: a call not answered", (long)x->eax);
    }
}

/* the memory a move's end means: the arena (handle 1), the screen through the bank window, or the host's */
static uint8_t *move_end(uint16_t handle, uint32_t off, uint32_t n, int *screen)
{
    *screen = 0;
    if (handle == 1) {
        if (!g_xms || off > g_xms_bytes || n > g_xms_bytes - off)
            hle_stop("XMS move past the arena", (long)off);
        return g_xms + off;
    }
    if (handle != 0)
        hle_stop("XMS move with an unknown handle", handle);
    if (off >= (uint32_t)(uintptr_t)g_vga_win && off < (uint32_t)(uintptr_t)g_vga_win + sizeof g_vga_win) {
        uint32_t at = g_bank * 0x10000U + (off - (uint32_t)(uintptr_t)g_vga_win);
        if (at + n > sizeof hle_fb)
            hle_stop("a move past the screen", (long)at);
        *screen = 1;
        return hle_fb + at;
    }
    return (uint8_t *)(uintptr_t)off;
}

/* XMS function 0Bh: the move record (length, source handle and offset, destination handle and offset), a
 * conventional address (handle 0) as seg:off: the host pointer's halves */
int hle_xms_move(const void *record)
{
    const uint8_t *m = (const uint8_t *)record;
    uint32_t n = rd32(m);
    int s_screen, d_screen;
    uint8_t *src = move_end(rd16(m + 4), rd32(m + 6), n, &s_screen);
    uint8_t *dst = move_end(rd16(m + 10), rd32(m + 12), n, &d_screen);
    memmove(dst, src, n);
    if (d_screen)
        hle_fb_changed = 1;
    return 1;
}

/* xms.c's moves into one handle's memory by its own offset (rocks.c's rows) */
uint8_t *hle_xms_at(unsigned handle, unsigned long off, unsigned long n)
{
    int screen;
    return move_end((uint16_t)handle, (uint32_t)off, (uint32_t)n, &screen);
}

/* --- the interrupt calls --- */

int int86x(int n, const union REGS *in, union REGS *out, struct SREGS *s)
{
    union REGS r = *in;
    switch (n) {
    case 0x10: int10(&r, s); break;
    case 0x21: int21(&r, s); break;
    case 0x2F:
        if (r.x.ax == 0x4300)
            r.h.al = 0x80;
        else if (r.x.ax == 0x4310) {
            if (s)
                s->es = 0xF000;
            r.x.bx = 0;
        } else
            hle_stop("int 2Fh: a call not answered", r.x.ax);
        break;
    case 0x33: int33(&r); break;
    default:
        hle_stop("an interrupt not answered", n);
    }
    *out = r;
    return r.x.ax;
}

int int86(int n, const union REGS *in, union REGS *out) { return int86x(n, in, out, 0); }
int intdos(const union REGS *in, union REGS *out) { return int86x(0x21, in, out, 0); }
int intdosx(const union REGS *in, union REGS *out, struct SREGS *s) { return int86x(0x21, in, out, s); }
void segread(struct SREGS *s) { memset(s, 0, sizeof *s); }

void intr(int n, union REGPACK *r)
{
    if (n != 0x16)
        hle_stop("intr: an interrupt not answered", n);
    r->x.flags = intr16(r) ? INTR_ZF : 0;
}

/* --- the ports --- */

static uint8_t g_opl_addr_lo, g_opl_status;
static uint16_t g_opl_addr_hi;

static void opl_reg(uint16_t reg, uint8_t v)
{
    if (reg == 4) {                     /* the timers' control: only what the detection reads back */
        if (v & 0x80)
            g_opl_status = 0;
        else if ((v & 1) && !(v & 0x40))
            g_opl_status = 0xC0;        /* timer 1 started: taken as run out already */
    }
    plat_opl_write(g_in_irq ? g_irq_us : plat_now_us(), reg, v);
}

unsigned outp(unsigned port, unsigned v)
{
    v &= 0xFF;
    switch (port) {
    case 0x3C8: g_dac_at = v; g_dac_part = 0; break;
    case 0x3C9:
        hle_dac[(g_dac_at & 255) * 3 + g_dac_part] = (uint8_t)(v & 63);
        hle_fb_changed = 1;
        if (++g_dac_part == 3) {
            g_dac_part = 0;
            g_dac_at++;
        }
        break;
    case 0x388: g_opl_addr_lo = (uint8_t)v; break;
    case 0x389: opl_reg(g_opl_addr_lo, (uint8_t)v); break;
    case 0x38A: g_opl_addr_hi = (uint16_t)(0x100 | v); break;
    case 0x38B: opl_reg(g_opl_addr_hi, (uint8_t)v); break;
    case 0x43:
        g_pit_phase = 0;
        break;
    case 0x40:
        if (g_pit_phase++ == 0)
            g_pit_lo = (uint8_t)v;
        else {
            uint64_t now = plat_now_us();
            g_pit_phase = 0;
            g_div = g_pit_lo | v << 8;
            if (!g_div)
                g_div = 65536;
            g_base_us = now;            /* (the count goes on from here at the new rate) */
            g_base_irq = g_irq_done;
        }
        break;
    case 0x20:
    case 0x330: case 0x331:             /* (no MPU-401: what it is sent goes nowhere) */
        break;
    default:
        hle_stop("a port written that is not answered", (long)port);
    }
    return v;
}

unsigned inp(unsigned port)
{
    switch (port) {
    case 0x3DA: return retrace();
    case 0x388: case 0x38A: return g_opl_status;
    case 0x60: return g_port60;
    case 0x330: case 0x331: return 0xFF;
    }
    hle_stop("a port read that is not answered", (long)port);
    return 0xFF;
}

/* --- files, the heap --- */

int rv_host_open(const char *name, int flags, ...);

unsigned _dos_open(const char *path, unsigned mode, int *fd)
{
    *fd = rv_host_open(path, O_RDONLY);    /* (fsguard.c: a plain name in the game's directory) */
    (void)mode;
    return *fd < 0 ? 2 : 0;
}

unsigned _dos_read(int fd, void *buf, unsigned n, unsigned *got)
{
    ssize_t k = read(fd, buf, n);
    *got = k < 0 ? 0 : (unsigned)k;
    return k < 0 ? 5 : 0;
}

unsigned _dos_close(int fd) { return close(fd) ? 6 : 0; }

int _fheapwalk(struct _heapinfo *hi) { (void)hi; return _HEAPEND; }
int _fheapshrink(void) { return 0; }

/* the far heap: the host's, its total kept (int 21h's largest block says what DOS would have left) */
void *hle_fmalloc(size_t n)
{
    size_t *p = malloc(n + 16);
    if (!p)
        return 0;
    p[0] = n;
    g_conv_used += (n + 15) / 16 * 16 + 16;
    return (uint8_t *)p + 16;
}

void hle_ffree(void *q)
{
    size_t *p;
    if (!q)
        return;
    p = (size_t *)((uint8_t *)q - 16);
    g_conv_used -= (p[0] + 15) / 16 * 16 + 16;
    free(p);
}

/* --- start --- */

void hle_init(void)
{
    g_vec[8] = bios_int8;
    g_vec[9] = bios_int9;
    wr16(hle_mk_fp(LIST_SEG, LIST_OFF - 2), MCB_SEG);
    {
        uint8_t *mcb = hle_mk_fp(MCB_SEG, 0);
        mcb[0] = 'Z';
        wr16(mcb + 1, 0);
        wr16(mcb + 3, DOS_FREE_KB * 64);
    }
    g_present_us = g_yield_us = plat_now_us();
}

unsigned long hle_irq_skipped(void) { return g_irq_skipped; }
