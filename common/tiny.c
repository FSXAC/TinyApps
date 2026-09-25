#include "tiny.h"

#include <stdio.h>
#include <string.h>

bool tiny_create_window(const char *title, int w, int h, int min_w, int min_h,
                        SDL_Window **win, SDL_Renderer **ren) {
    *win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!*win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(*win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (min_w > 0 && min_h > 0) SDL_SetWindowMinimumSize(*win, min_w, min_h);
    *ren = SDL_CreateRenderer(*win, NULL);
    if (!*ren) *ren = SDL_CreateRenderer(*win, SDL_SOFTWARE_RENDERER);
    if (!*ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(*win);
        *win = NULL;
        return false;
    }
    SDL_SetRenderVSync(*ren, 1);
    return true;
}

float tiny_pixel_density(SDL_Window *win, SDL_Renderer *ren) {
    int ww = 0, wh = 0, rw = 0, rh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRenderOutputSize(ren, &rw, &rh);
    return ww > 0 ? (float)rw / (float)ww : 1.0f;
}

void tiny_fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_FRect rc = {(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(r, &rc);
}

/* ------------------------------------------------------------------ */
/* Font atlas: 16 columns x 6 rows of 6x8 cells, white glyphs on transparent. */

enum { CELL_W = 6, CELL_H = 8, ATLAS_COLS = 16 };
#define ATLAS_ROWS ((TINY_FONT_GLYPHS + ATLAS_COLS - 1) / ATLAS_COLS)

bool tiny_font_init(TinyFont *f, SDL_Renderer *r) {
    f->atlas = NULL;
    SDL_Surface *s = SDL_CreateSurface(ATLAS_COLS * CELL_W, ATLAS_ROWS * CELL_H, SDL_PIXELFORMAT_RGBA32);
    if (!s) return false;
    SDL_ClearSurface(s, 0, 0, 0, 0);
    for (int g = 0; g < TINY_FONT_GLYPHS; g++) {
        int ox = (g % ATLAS_COLS) * CELL_W, oy = (g / ATLAS_COLS) * CELL_H;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (tiny_font5x7[g][row] & (0x10 >> col)) {
                    uint8_t *p = (uint8_t *)s->pixels + (oy + row) * s->pitch + (ox + col) * 4;
                    p[0] = p[1] = p[2] = p[3] = 255;
                }
    }
    f->atlas = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (!f->atlas) {
        fprintf(stderr, "font atlas: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(f->atlas, SDL_SCALEMODE_NEAREST);
    return true;
}

void tiny_font_free(TinyFont *f) {
    if (f->atlas) SDL_DestroyTexture(f->atlas);
    f->atlas = NULL;
}

int tiny_text_width(const char *s, int scale) {
    int n = (int)strlen(s);
    return n ? (n * 6 - 1) * scale : 0;
}

void tiny_text(const TinyFont *f, const char *s, int x, int y, int scale) {
    if (!f->atlas) return;
    SDL_Renderer *r = SDL_GetRendererFromTexture(f->atlas);
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_SetTextureColorMod(f->atlas, cr, cg, cb);
    SDL_SetTextureAlphaMod(f->atlas, ca);
    for (; *s; s++, x += 6 * scale) {
        int g = (unsigned char)*s - TINY_FONT_FIRST;
        if (g <= 0 || g >= TINY_FONT_GLYPHS) continue; /* space or unsupported */
        SDL_FRect src = {(float)((g % ATLAS_COLS) * CELL_W), (float)((g / ATLAS_COLS) * CELL_H), 5, 7};
        SDL_FRect dst = {(float)x, (float)y, (float)(5 * scale), (float)(7 * scale)};
        SDL_RenderTexture(r, f->atlas, &src, &dst);
    }
}

void tiny_text_centered(const TinyFont *f, const char *s, int cx, int y, int scale) {
    tiny_text(f, s, cx - tiny_text_width(s, scale) / 2, y, scale);
}
