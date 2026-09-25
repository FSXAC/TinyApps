/*
 * Shared helpers for the tinyapps SDL3 tools: window setup, pixel drawing and
 * the 5x7 bitmap font. Usable from C and C++.
 */
#ifndef TINY_H
#define TINY_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a resizable, high-pixel-density, centered window with a vsynced
   renderer (software fallback). Prints the SDL error and returns false on failure. */
bool tiny_create_window(const char *title, int w, int h, int min_w, int min_h,
                        SDL_Window **win, SDL_Renderer **ren);

/* Output pixels per window point (2 on Retina). */
float tiny_pixel_density(SDL_Window *win, SDL_Renderer *ren);

/* Fills an integer rectangle with the current draw color. */
void tiny_fill_rect(SDL_Renderer *r, int x, int y, int w, int h);

/* ------------------------------------------------------------------ */
/* 5x7 bitmap font (printable ASCII). Glyphs advance 6 units; text is drawn
   from a texture atlas scaled with nearest-neighbour, so integer scales stay
   crisp. Characters outside 0x20-0x7E are skipped (but still advance). */

#define TINY_FONT_FIRST 0x20
#define TINY_FONT_GLYPHS 95
extern const uint8_t tiny_font5x7[TINY_FONT_GLYPHS][7];

typedef struct {
    SDL_Texture *atlas;
} TinyFont;

bool tiny_font_init(TinyFont *f, SDL_Renderer *r);
void tiny_font_free(TinyFont *f);

/* Width/height in pixels of a single line of text at `scale`. */
int tiny_text_width(const char *s, int scale);
static inline int tiny_text_height(int scale) { return 7 * scale; }

/* Draws text in the renderer's current draw color, top-left at (x, y). */
void tiny_text(const TinyFont *f, const char *s, int x, int y, int scale);
/* Same, horizontally centered on cx. */
void tiny_text_centered(const TinyFont *f, const char *s, int cx, int y, int scale);

#ifdef __cplusplus
}
#endif

#endif
