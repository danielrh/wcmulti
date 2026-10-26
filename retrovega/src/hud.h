/* hud.h: drawing over the flight's view (hud.c): runs, lines, crosses and brackets clipped to the band being drawn
 * and to the cockpit's opening, as the world is.  Call them only from a band's draw (logic_hud): the screen is drawn
 * a band of rows at a time, back to front, and each call here draws the part of its shape in that band. */
#ifndef HUD_H
#define HUD_H

/* the band's rows and the opening's extents (flight.c sets them before each band's logic_hud) */
void hud_band(int y0, int y1, const short __far *ext, int ext_y0, int ext_rows);

void hud_run(int xa, int xb, int y, int colour);   /* pixels xa .. xb-1 of row y */
void hud_dot(int x, int y, int colour);
void hud_vline(int x, int ya, int yb, int colour);   /* rows ya .. yb-1 of column x */
void hud_line(int xa, int ya, int xb, int yb, int colour);   /* a line, pixel by pixel (short ones: 2,000 px at most) */
void hud_cross(int x, int y, int arm, int colour);   /* a + of arms `arm` pixels long */
void hud_brackets(int x0, int y0, int x1, int y1, int colour);   /* corner brackets round x0 .. x1-1, y0 .. y1-1 */
void hud_diamond(int x, int y, int r, int colour);   /* an outline, r pixels from the middle to each corner */

#endif
