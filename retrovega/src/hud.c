/* hud.c: drawing over the flight's view (hud.h), clipped to the band and the opening's extents. */
#include <string.h>
#include <stdlib.h>
#include "rv.h"
#include "hud.h"

static int __far g_y0, __far g_y1, __far g_ey0, __far g_rows;
static const short __far *__far g_ext;

void hud_band(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows)
{
    g_y0 = y0;
    g_y1 = y1;
    g_ext = ext;
    g_ey0 = ext_y0;
    g_rows = ext_rows;
}

void hud_run(int xa, int xb, int y, int colour)
{
    unsigned char __far *row;
    if (y < g_y0 || y >= g_y1)
        return;
    if (g_ext) {
        if (y < g_ey0 || y >= g_ey0 + g_rows)
            return;
        if (xa < g_ext[2 * (y - g_ey0)]) xa = g_ext[2 * (y - g_ey0)];
        if (xb > g_ext[2 * (y - g_ey0) + 1]) xb = g_ext[2 * (y - g_ey0) + 1];
    }
    if (xa < 0) xa = 0;
    if (xb > SCR_W) xb = SCR_W;
    if (xa >= xb)
        return;
    row = vid_row(y);
    _fmemset(row + xa, colour, xb - xa);
}

void hud_dot(int x, int y, int colour) { hud_run(x, x + 1, y, colour); }

void hud_vline(int x, int ya, int yb, int colour)
{
    if (ya < g_y0) ya = g_y0;
    if (yb > g_y1) yb = g_y1;
    for (; ya < yb; ya++)
        hud_run(x, x + 1, ya, colour);
}

void hud_line(int xa, int ya, int xb, int yb, int colour)
{
    int dx = xb - xa, dy = yb - ya, n = abs(dx) > abs(dy) ? abs(dx) : abs(dy), i;
    if (!n)
        n = 1;
    if (n > 2000)
        return;
    for (i = 0; i <= n; i++) {
        int x = xa + (int)((long)dx * i / n), y = ya + (int)((long)dy * i / n);
        hud_run(x, x + 1, y, colour);
    }
}

void hud_cross(int x, int y, int arm, int colour)
{
    hud_run(x - arm, x + arm + 1, y, colour);
    hud_vline(x, y - arm, y + arm + 1, colour);
}

void hud_brackets(int x0, int y0, int x1, int y1, int colour)
{
    int aw = (x1 - x0) / 4, ah = (y1 - y0) / 4;
    if (aw > 10) aw = 10;
    if (ah > 10) ah = 10;
    if (aw < 2) aw = 2;
    if (ah < 2) ah = 2;
    hud_run(x0, x0 + aw, y0, colour);
    hud_run(x1 - aw, x1, y0, colour);
    hud_run(x0, x0 + aw, y1 - 1, colour);
    hud_run(x1 - aw, x1, y1 - 1, colour);
    hud_vline(x0, y0, y0 + ah, colour);
    hud_vline(x1 - 1, y0, y0 + ah, colour);
    hud_vline(x0, y1 - ah, y1, colour);
    hud_vline(x1 - 1, y1 - ah, y1, colour);
}

void hud_diamond(int x, int y, int r, int colour)
{
    int k;
    for (k = 0; k <= r; k++) {
        hud_dot(x - r + k, y - k, colour);
        hud_dot(x + r - k, y - k, colour);
        hud_dot(x - r + k, y + k, colour);
        hud_dot(x + r - k, y + k, colour);
    }
}
