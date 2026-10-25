/* How each game draws its states (SDL side of a game module). */
#ifndef VIEWS_H
#define VIEWS_H

#include "sv.h"
#include "tiny.h"

typedef struct {
    /* Draws a state as a square board, `size` pixels wide, at (x, y). It must
       read at any size from about 9 units (a thumbnail) up. */
    void (*draw)(SDL_Renderer *r, const TinyFont *f, const void *s, int x, int y, int size, int u);
    /* Whether a move involves the point (px, py) of a board drawn `size`
       wide. Clicking a point with one such move plays it; with several, the
       side panel lists them. */
    bool (*move_hit)(const void *s, SvMove m, int size, int px, int py);
    /* Optional: drawn over the side board. `values` holds the value each of
       the n legal moves leads to; (fx, fy) is the point in focus, or -1. */
    void (*annotate)(SDL_Renderer *r, const TinyFont *f, const void *s, const SvMove *moves, const int8_t *values,
                     int n, int x, int y, int size, int u, int fx, int fy);
} SvView;

extern const SvView sv_ttt_view, sv_puzzle_view, sv_puzzle_lowest_view;

/* A value's mark: player 0's win (or solvable) solid ink, a draw (or dead
   end) gray, player 1's win hollow, unknown shade. */
void sv_value_fill(SDL_Renderer *r, int x, int y, int w, int h, int v, int u);

#endif
