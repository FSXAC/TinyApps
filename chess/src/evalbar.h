/*
 * The eval bar: a vertical gauge of who is winning, drawn in the tinyapps
 * design language (see common/DESIGN.md). White's share fills from White's
 * side of the board, with the score label at the leading end.
 */
#ifndef CHESS_EVALBAR_H
#define CHESS_EVALBAR_H

#include "analysis.h"
#include "tiny.h"

/* Recommended bar width in units: room for a "0.0" / "M12" label on one row.
   Narrower bars (down to 7u) stack the label's glyphs vertically instead. */
#define EVALBAR_WIDTH 20

/* Draws the bar in the rect (renderer pixels; `u` is the design unit; keep
   the rect a whole number of units). `white_bottom` matches the board
   orientation. `e` may be NULL while the position is still being analysed:
   the bar is then drawn level with no label. */
void evalbar_draw(SDL_Renderer *r, const TinyFont *f, int x, int y, int w, int h, int u,
                  const PositionEval *e, bool white_bottom);

/* White's share of the bar, 0..1 (1 for a White mate, 0.5 for NULL). Handy
   for animating the split between positions. */
double evalbar_white_fraction(const PositionEval *e);

/* The unsigned label the bar shows: "1.5", "12", "M3", "1-0", "0-1", "1/2". */
void evalbar_label(const PositionEval *e, char *out, size_t n);

#endif
