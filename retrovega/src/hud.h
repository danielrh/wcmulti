/* hud.h: drawing over the flight's view (hud.c): runs, lines, crosses and brackets clipped to the band being drawn
 * and to the cockpit's opening, as the world is.  Call them only from a band's draw (logic_hud): the screen is drawn
 * a band of rows at a time, back to front, and each call here draws the part of its shape in that band. */
#ifndef HUD_H
#define HUD_H

#include "rvtypes.h"

/* the band's rows and the opening's extents (flight.c sets them before each band's logic_hud) */
void hud_band(int16_t y0, int16_t y1, const int16_t __far *ext, int16_t ext_y0, int16_t ext_rows);

void hud_run(int16_t xa, int16_t xb, int16_t y, int16_t colour);   /* pixels xa .. xb-1 of row y */
void hud_dot(int16_t x, int16_t y, int16_t colour);
void hud_vline(int16_t x, int16_t ya, int16_t yb, int16_t colour);   /* rows ya .. yb-1 of column x */
/* a line, pixel by pixel (short ones: 2,000 px at most) */
void hud_line(int16_t xa, int16_t ya, int16_t xb, int16_t yb, int16_t colour);
void hud_cross(int16_t x, int16_t y, int16_t arm, int16_t colour);   /* a + of arms `arm` pixels long */
/* corner brackets round x0 .. x1-1, y0 .. y1-1 */
void hud_brackets(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t colour);
void hud_diamond(int16_t x, int16_t y, int16_t r, int16_t colour);   /* an outline, r px from the middle to a corner */

#endif
