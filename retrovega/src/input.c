/* input.c: the mouse (int 33h), the keys (int 16h), and test scripts.
 *
 * Mouse and keys together (docs/engine.md): an arrow key puts the game in
 * keyboard mode, the game moves the pointer between hotspots, and the mouse
 * driver is pinned to the pointer.  Mouse motion is then ignored until it
 * adds up to more than KEYBOARD_SLACK mickeys, or a button is pressed: then
 * the mouse has the pointer again.
 *
 * A test script (RV.EXE /script FILE) replaces both: one command a line,
 *     wait N       N ticks (18.2 a second)
 *     move X Y     the pointer to X, Y (as a mouse move)
 *     click        the left button, at the pointer
 *     key NAME     left right up down enter esc backspace f12, or one letter
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
#include "rv.h"

#define KEYBOARD_SLACK 6

static int g_mouse, g_x = 320, g_y = 200, g_keyboard, g_slack, g_buttons;
static FILE *g_script;
static unsigned long g_wake;
static char g_typed[64];
static int g_typed_at = -1;
static int g_quit;

unsigned long ticks(void)
{
    return *(volatile unsigned long __far *)MK_FP(0x40, 0x6C);
}

int in_init(void)
{
    union REGS r;
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

static int bios_key(int *ch)
{
    union REGS r;
    r.h.ah = 1;
    int86(0x16, &r, &r);
    if (r.x.cflag & INTR_ZF)
        return KEY_NONE;
    r.h.ah = 0;
    int86(0x16, &r, &r);
    *ch = r.h.al;
    switch (r.h.ah) {
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x1C: return KEY_ENTER;
    case 0x01: return KEY_ESC;
    case 0x0E: return KEY_BACKSPACE;
    case 0x58: return KEY_F12;
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
    static const char *names[] = { "", "left", "right", "up", "down", "enter", "esc", "backspace", "f12" };
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
        if (in->key >= KEY_LEFT && in->key <= KEY_DOWN)
            g_keyboard = 1;
        in->x = g_x;
        in->y = g_y;
        in->keyboard = g_keyboard;
        return;
    }
    key = bios_key(&ch);
    in->key = key;
    in->ch = ch;
    if (key >= KEY_LEFT && key <= KEY_DOWN) {
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
    rv_log("shot: %s", path);
}
