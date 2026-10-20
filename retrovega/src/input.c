/* input.c: the mouse (int 33h), the keys (int 16h and int 9), and test scripts.
 *
 * Mouse and keys together (docs/engine.md): the arrow keys steer the
 * pointer, faster the longer they are held (int 9 says which are down), and
 * Tab and Shift+Tab put it on the next or the previous hotspot (room.c).
 * Either puts the game in keyboard mode, and the mouse driver is pinned to
 * the pointer.  Mouse motion is then ignored until it adds up to more than
 * KEYBOARD_SLACK mickeys, or a button is pressed: then the mouse has the
 * pointer again.
 *
 * A test script (RV.EXE /script FILE) replaces both: one command a line,
 *     wait N       N ticks (18.2 a second)
 *     move X Y     the pointer to X, Y (as a mouse move)
 *     click        the left button, at the pointer
 *     key NAME     left right up down enter esc backspace f12 tab backtab,
 *                  or one letter
 *     hold NAME N  an arrow key held down for N ticks (the script goes on:
 *                  two holds at once steer diagonally)
 *     type TEXT    the characters of TEXT, then enter
 *     shot NAME    the screen as NAME.BMP
 *     log TEXT     a line in RVLOG.TXT
 *     quit         leave the game
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include "rv.h"
#include "music.h"

#define KEYBOARD_SLACK 6
#define STEER_MIN 150L                  /* pixels a second as an arrow goes down */
#define STEER_ACC 1000L                 /* ... and more each second it is held */
#define STEER_MAX 700L

static int g_mouse, g_x = 320, g_y = 200, g_keyboard, g_slack, g_buttons;
static int g_steer = 1;
static unsigned long g_steer_at, g_held;    /* clock240: the last step; how long the arrows are held */
static long g_fx, g_fy;                     /* the pointer while steering, in 1/256 pixel */
static unsigned long g_hold_until[4];       /* a script's held arrows: until this BIOS tick */

/* int 9: which keys are down, by scan code; the BIOS's handler still runs */
static void (__interrupt __far *g_old9)(void);
static volatile unsigned char g_down[128];
static volatile unsigned char g_e0;
static const unsigned char g_arrow_scan[4] = { 0x4B, 0x4D, 0x48, 0x50 };   /* left right up down */
static FILE *g_script;
static unsigned long g_wake;
static char g_typed[64];
static int g_typed_at = -1;
static int g_quit;

unsigned long ticks(void)
{
    return *(volatile unsigned long __far *)MK_FP(0x40, 0x6C);
}

static void __interrupt __far key_isr(void)
{
    unsigned char sc = inp(0x60);
    if (sc == 0xE0) {
        g_e0 = 1;
    } else {
        if (!(g_e0 && ((sc & 0x7F) == 0x2A || (sc & 0x7F) == 0x36)))   /* not the shifts faked around grey keys */
            g_down[sc & 0x7F] = !(sc & 0x80);
        g_e0 = 0;
    }
    _chain_intr(g_old9);
}

static void in_shutdown(void)
{
    if (g_old9) {
        _dos_setvect(9, g_old9);
        g_old9 = 0;
    }
}

int in_init(void)
{
    union REGS r;
    g_old9 = _dos_getvect(9);
    _dos_setvect(9, key_isr);
    atexit(in_shutdown);
    r.x.ax = 0;
    int86(0x33, &r, &r);
    g_mouse = r.x.ax == 0xFFFF;
    if (g_mouse) {
        r.x.ax = 7; r.x.cx = 0; r.x.dx = SCR_W - 1; int86(0x33, &r, &r);
        r.x.ax = 8; r.x.cx = 0; r.x.dx = SCR_H - 1; int86(0x33, &r, &r);
        in_warp(SCR_W / 2, SCR_H / 2);
    }
    rv_log("input: mouse %s", g_mouse ? "found" : "missing");
    return 1;
}

void in_warp(int x, int y)
{
    union REGS r;
    g_x = x;
    g_y = y;
    if (g_mouse && !g_script) {
        r.x.ax = 4; r.x.cx = x; r.x.dx = y;
        int86(0x33, &r, &r);
    }
}

/* a keystroke from the BIOS, or KEY_NONE: the enhanced functions (10h, 11h),
 * which also give the grey keys and F11/F12; the zero flag says "none" (it
 * is in REGPACK's flags: REGS's cflag is the carry flag alone) */
static int bios_key(int *ch)
{
    union REGPACK r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x11;
    intr(0x16, &r);
    if (r.x.flags & INTR_ZF)
        return KEY_NONE;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x10;
    intr(0x16, &r);
    *ch = r.h.al;
    switch (r.h.ah) {
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x1C: return KEY_ENTER;
    case 0xE0: return *ch == 0x0D ? KEY_ENTER : KEY_NONE;     /* the keypad's Enter */
    case 0x01: return KEY_ESC;
    case 0x0E: return KEY_BACKSPACE;
    case 0x86: return KEY_F12;
    case 0x0F: return *ch == 9 ? KEY_TAB : KEY_BACKTAB;
    }
    return *ch >= 32 && *ch < 127 ? KEY_CHAR : KEY_NONE;
}

int script_open(const char *path)
{
    g_script = fopen(path, "r");
    rv_log("script: %s %s", path, g_script ? "opened" : "missing");
    return g_script != 0;
}

int script_active(void) { return g_script != 0 || g_quit; }

static int key_named(const char *s, int *ch)
{
    static const char *names[] = { "", "left", "right", "up", "down", "enter", "esc", "backspace", "f12", "tab", "backtab" };
    int i;
    for (i = 1; i < (int)RV_COUNT(names); i++)
        if (!strcmp(s, names[i]))
            return i;
    *ch = s[0];
    return KEY_CHAR;
}

/* the next script event into in; returns 1 when the game should quit */
static void script_step(Input *in)
{
    char line[96], *arg;
    if (g_typed_at >= 0) {
        if (g_typed[g_typed_at]) {
            in->key = KEY_CHAR;
            in->ch = g_typed[g_typed_at++];
        } else {
            in->key = KEY_ENTER;
            g_typed_at = -1;
        }
        return;
    }
    if (ticks() < g_wake)
        return;
    while (fgets(line, sizeof line, g_script)) {
        char *e = line + strlen(line);
        while (e > line && isspace((unsigned char)e[-1]))
            *--e = 0;
        arg = strchr(line, ' ');
        if (arg)
            *arg++ = 0;
        else
            arg = e;
        if (!line[0] || line[0] == '#')
            continue;
        if (!strcmp(line, "wait")) {
            g_wake = ticks() + atoi(arg);
            return;
        } else if (!strcmp(line, "move")) {
            int x, y;
            if (sscanf(arg, "%d %d", &x, &y) == 2) {
                g_x = x;
                g_y = y;
                in->moved = 1;
                in->keyboard = g_keyboard = 0;
            }
            return;
        } else if (!strcmp(line, "click")) {
            in->clicked = 1;
            in->keyboard = g_keyboard = 0;
            return;
        } else if (!strcmp(line, "key")) {
            in->key = key_named(arg, &in->ch);
            return;
        } else if (!strcmp(line, "hold")) {
            char name[16];
            int n, ch, k;
            if (sscanf(arg, "%15s %d", name, &n) == 2 && (k = key_named(name, &ch)) >= KEY_LEFT && k <= KEY_DOWN)
                g_hold_until[k - KEY_LEFT] = ticks() + n;
            else
                rv_log("script: hold %s?", arg);
            continue;
        } else if (!strcmp(line, "type")) {
            strncpy(g_typed, arg, sizeof g_typed - 1);
            g_typed_at = 0;
            return;
        } else if (!strcmp(line, "shot")) {
            shot_save(arg);
        } else if (!strcmp(line, "log")) {
            rv_log("script: %s", arg);
        } else if (!strcmp(line, "quit")) {
            in->key = KEY_ESC;
            g_quit = 1;
            fclose(g_script);
            g_script = 0;
            return;
        } else {
            rv_log("script: unknown %s", line);
        }
    }
    fclose(g_script);
    g_script = 0;
    g_quit = 1;
}

/* the arrows held down: bit 0 left, 1 right, 2 up, 3 down */
static int arrows(void)
{
    int i, m = 0;
    for (i = 0; i < 4; i++)
        if (g_script ? ticks() < g_hold_until[i] : g_down[g_arrow_scan[i]] != 0)
            m |= 1 << i;
    return m;
}

/* the arrows steer the pointer, faster the longer they are held: 1 when it moved */
static int steer(void)
{
    int m = g_steer ? arrows() : 0, nx, ny;
    int dx = ((m >> 1) & 1) - (m & 1), dy = ((m >> 3) & 1) - ((m >> 2) & 1);
    unsigned long now = clock240();
    long dt = (long)(now - g_steer_at), speed, step;
    g_steer_at = now;
    if (!dx && !dy) {
        if (g_held)
            rv_log("keys: steered to %d,%d", g_x, g_y);
        g_held = 0;
        return 0;
    }
    if (dt > 24)
        dt = 24;
    if (!g_held || (int)(g_fx >> 8) != g_x || (int)(g_fy >> 8) != g_y) {
        g_fx = ((long)g_x << 8) + 128;      /* from where the pointer is (the mouse may have moved it) */
        g_fy = ((long)g_y << 8) + 128;
    }
    g_held += dt;
    speed = STEER_MIN + STEER_ACC * (long)g_held / 240;
    if (speed > STEER_MAX)
        speed = STEER_MAX;
    step = speed * dt * 256 / 240;
    if (dx && dy)
        step = step * 181 / 256;            /* a diagonal goes no faster */
    g_fx += dx * step;
    g_fy += dy * step;
    if (g_fx < 0) g_fx = 0;
    if (g_fy < 0) g_fy = 0;
    if (g_fx > ((long)(SCR_W - 1) << 8)) g_fx = (long)(SCR_W - 1) << 8;
    if (g_fy > ((long)(SCR_H - 1) << 8)) g_fy = (long)(SCR_H - 1) << 8;
    nx = (int)(g_fx >> 8);
    ny = (int)(g_fy >> 8);
    if (nx == g_x && ny == g_y)
        return 0;
    g_x = nx;
    g_y = ny;
    return 1;
}

void in_steer(int on)
{
    g_steer = on;
}

void in_poll(Input *in)
{
    union REGS r;
    int ch = 0, key;
    in->clicked = in->key = in->ch = in->moved = 0;
    if (g_script || g_quit) {
        if (g_quit) {
            in->key = KEY_ESC;
            in->ch = 'q';
        } else {
            script_step(in);
        }
        if (in->key == KEY_TAB || in->key == KEY_BACKTAB)
            g_keyboard = 1;
        if (steer())
            g_keyboard = in->moved = 1;
        in->x = g_x;
        in->y = g_y;
        in->keyboard = g_keyboard;
        return;
    }
    key = bios_key(&ch);
    in->key = key;
    in->ch = ch;
    if (key == KEY_TAB || key == KEY_BACKTAB || steer()) {
        if (key != KEY_TAB && key != KEY_BACKTAB)
            in->moved = 1;
        g_keyboard = 1;
        g_slack = 0;
    }
    if (g_mouse) {
        int buttons;
        r.x.ax = 3;
        int86(0x33, &r, &r);
        buttons = r.x.bx & 1;
        if (g_keyboard) {
            /* pinned: count the motion, keep the driver where the pointer is */
            r.x.ax = 0x0B;
            int86(0x33, &r, &r);
            g_slack += abs((short)r.x.cx) + abs((short)r.x.dx);
            if (g_slack > KEYBOARD_SLACK || (buttons && !g_buttons)) {
                g_keyboard = 0;
            } else {
                r.x.ax = 4; r.x.cx = g_x; r.x.dx = g_y;
                int86(0x33, &r, &r);
            }
        }
        if (!g_keyboard) {
            r.x.ax = 3;
            int86(0x33, &r, &r);
            if ((int)r.x.cx != g_x || (int)r.x.dx != g_y)
                in->moved = 1;
            g_x = r.x.cx;
            g_y = r.x.dx;
            r.x.ax = 0x0B;
            int86(0x33, &r, &r);     /* drain the motion counters */
        }
        if (buttons && !g_buttons)
            in->clicked = 1;
        g_buttons = buttons;
    }
    in->x = g_x;
    in->y = g_y;
    in->keyboard = g_keyboard;
}

/* --- screenshots --- */

void shot_save(const char *name)
{
    char path[20];
    FILE *f;
    const unsigned char *pal = shot_palette();
    unsigned long size = 54 + 1024 + (unsigned long)SCR_W * SCR_H;
    unsigned char h[54];
    int y, i;
    sprintf(path, "%.8s.BMP", name);
    f = fopen(path, "wb");
    if (!f)
        return;
    memset(h, 0, sizeof h);
    h[0] = 'B'; h[1] = 'M';
    *(unsigned long *)(h + 2) = size;
    *(unsigned long *)(h + 10) = 54 + 1024;
    *(unsigned long *)(h + 14) = 40;
    *(long *)(h + 18) = SCR_W;
    *(long *)(h + 22) = SCR_H;
    *(unsigned short *)(h + 26) = 1;
    *(unsigned short *)(h + 28) = 8;
    *(unsigned long *)(h + 34) = (unsigned long)SCR_W * SCR_H;
    *(unsigned long *)(h + 46) = 256;
    fwrite(h, 1, 54, f);
    for (i = 0; i < 256; i++) {
        unsigned char q[4];
        q[0] = (unsigned char)(pal[i * 3 + 2] * 255 / 63);
        q[1] = (unsigned char)(pal[i * 3 + 1] * 255 / 63);
        q[2] = (unsigned char)(pal[i * 3] * 255 / 63);
        q[3] = 0;
        fwrite(q, 1, 4, f);
    }
    for (y = SCR_H - 1; y >= 0; y--) {
        static unsigned char row[SCR_W];
        _fmemcpy(row, vid_row(y), SCR_W);
        fwrite(row, 1, SCR_W, f);
    }
    fclose(f);
    rv_log("shot: %s, the pointer at %d,%d", path, g_x, g_y);
}
