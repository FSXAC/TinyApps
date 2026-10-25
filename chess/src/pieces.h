/*
 * 32x32 one-bit piece art, as TinyPainter would draw it, turned into textures.
 */
#ifndef CHESS_PIECES_H
#define CHESS_PIECES_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define PIECE_ART 32

enum { AVATAR_YOU, AVATAR_BOT, AVATAR_COUNT };

typedef struct {
    SDL_Texture *tex[2][7]; /* [color][type] */
    SDL_Texture *avatar[AVATAR_COUNT];
} PieceSet;

bool pieces_init(PieceSet *s, SDL_Renderer *r);
void pieces_free(PieceSet *s);
/* Draws `piece` (see board.h) into a size x size square at (x, y). Sizes that
   are multiples of 32 keep the pixels square; others are centered at the
   largest multiple that fits. `alpha` 255 is opaque. */
void pieces_draw(const PieceSet *s, SDL_Renderer *r, uint8_t piece, int x, int y, int size, uint8_t alpha);

/* Draws a 32x32 avatar, ink on paper, scaled to size x size. */
void pieces_draw_avatar(const PieceSet *s, SDL_Renderer *r, int which, int x, int y, int size);

#endif
