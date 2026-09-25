/*
 * Shared helpers for the tinyapps SDL3 tools: window setup, the design
 * language (see DESIGN.md) and the 5x7 bitmap font. Usable from C and C++.
 *
 * Sizes are in renderer pixels; `u` is the design unit (1 point, i.e. one
 * classic-Mac pixel) in those pixels -- see tiny_unit().
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
/* The design unit: whole output pixels per point, at least 1. */
int tiny_unit(SDL_Window *win, SDL_Renderer *ren);

/* Presents the frame. With TINY_SCREENSHOT=<file.bmp> in the environment, the
   first frame is also saved there and a quit event is queued, so visuals can
   be checked without driving the UI. */
void tiny_present(SDL_Renderer *r);

/* ------------------------------------------------------------------ */
/* Palette: black ink on white paper plus three grays. No accent color;
   selection and pressed states are shown by inverting ink and paper. */

typedef enum {
    TINY_INK,   /* text, frames, "on" pixels, pressed fills */
    TINY_PAPER, /* backgrounds, button faces */
    TINY_SHADE, /* hover fill, subtle surfaces */
    TINY_GRAY,  /* disabled / inactive, out-of-bounds areas */
    TINY_DIM,   /* secondary text */
} TinyColor;

SDL_Color tiny_color(TinyColor c);
void tiny_set_color(SDL_Renderer *r, TinyColor c);

/* Spacing and radii, in units. */
enum {
    TINY_GAP = 4,          /* between related controls */
    TINY_PAD = 8,          /* inside panels, between groups */
    TINY_MARGIN = 16,      /* window edge to content */
    TINY_RADIUS = 4,       /* buttons, panels */
    TINY_RADIUS_SMALL = 2, /* keys, chips, small controls */
};

/* ------------------------------------------------------------------ */
/* Shapes. Rounded corners are stepped in whole units, like QuickDraw's
   FillRoundRect, never anti-aliased. */

enum { TINY_TL = 1, TINY_TR = 2, TINY_BR = 4, TINY_BL = 8, TINY_CORNERS = 15 };

/* Fills an integer rectangle with the current draw color. */
void tiny_fill_rect(SDL_Renderer *r, int x, int y, int w, int h);
/* Fills a rounded rectangle (radius in units, only the `corners` given are
   rounded) with the current draw color. */
void tiny_fill_round(SDL_Renderer *r, int x, int y, int w, int h, int radius, int u, int corners);
/* A rounded rectangle with a 1-unit `line` frame around a `fill` face. */
void tiny_frame_round(SDL_Renderer *r, int x, int y, int w, int h, int radius, int u, int corners,
                      TinyColor fill, TinyColor line);
/* 50% gray: an ink/paper checkerboard of 1-unit cells aligned to the window,
   so adjacent fills line up. */
void tiny_fill_gray50(SDL_Renderer *r, int x, int y, int w, int h, int u);

/* ------------------------------------------------------------------ */
/* Components. */

typedef enum { TINY_NORMAL, TINY_HOVER, TINY_PRESSED } TinyState;

/* Push button face: rounded, 1-unit ink frame. Hover shades the face, pressed
   (or selected) inverts it. Returns the color to draw the label/icon in. */
TinyColor tiny_button(SDL_Renderer *r, int x, int y, int w, int h, int u, TinyState st);
/* Panel: paper face, 1-unit ink frame and a 1-unit drop shadow (drawn outside
   the rect, bottom-right). */
void tiny_panel(SDL_Renderer *r, int x, int y, int w, int h, int u);

/* ------------------------------------------------------------------ */
/* 5x7 bitmap font: printable ASCII plus a few UI symbols (see font5x7.c),
   UTF-8 encoded. Unknown characters are skipped but still take a cell.
   Text size is a role times the unit: caption 1u, body 2u, title 3u; larger
   "display" sizes (e.g. a big readout) are sized to fit. Bold is QuickDraw
   style: the glyph drawn twice, one font pixel apart. */

enum { TINY_CAPTION = 1, TINY_BODY = 2, TINY_TITLE = 3 };
typedef enum { TINY_PLAIN, TINY_BOLD } TinyStyle;

#define TINY_FONT_FIRST 0x20
#define TINY_FONT_ASCII 95
#define TINY_FONT_EXTRAS 10
typedef struct {
    uint32_t codepoint;
    uint8_t rows[9];
} TinyGlyph;
extern const uint8_t tiny_font5x7[TINY_FONT_ASCII][9];
extern const TinyGlyph tiny_font_extras[TINY_FONT_EXTRAS];

typedef struct {
    SDL_Texture *atlas;
} TinyFont;

bool tiny_font_init(TinyFont *f, SDL_Renderer *r);
void tiny_font_free(TinyFont *f);

/* Size in pixels of a single line of text at `scale` pixels per font pixel.
   The height is the cap height (use it for centering); descenders extend 2
   font pixels below it, so stacked lines need tiny_line_height() apart. */
int tiny_text_width(const char *s, int scale, TinyStyle st);
static inline int tiny_text_height(int scale) { return 7 * scale; }
static inline int tiny_line_height(int scale) { return 11 * scale; }

/* Draws text in the renderer's current draw color, top-left at (x, y).
   Returns the width drawn. */
int tiny_text(const TinyFont *f, const char *s, int x, int y, int scale, TinyStyle st);
/* Same, horizontally centered on cx. */
void tiny_text_centered(const TinyFont *f, const char *s, int cx, int y, int scale, TinyStyle st);

#ifdef __cplusplus
}
#endif

#endif
