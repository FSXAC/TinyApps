#include "tiny.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

int tiny_unit(SDL_Window *win, SDL_Renderer *ren) {
    int u = (int)(tiny_pixel_density(win, ren) + 0.5f);
    return u < 1 ? 1 : u;
}

void tiny_present(SDL_Renderer *r) {
    static bool checked;
    static const char *path;
    if (!checked) {
        checked = true;
        path = getenv("TINY_SCREENSHOT");
        if (path && *path) {
            SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
            if (!s || !SDL_SaveBMP(s, path)) fprintf(stderr, "screenshot: %s\n", SDL_GetError());
            SDL_DestroySurface(s);
            SDL_Event quit = {0};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
    }
    SDL_RenderPresent(r);
}

/* ------------------------------------------------------------------ */
/* Palette. */

SDL_Color tiny_color(TinyColor c) {
    switch (c) {
    case TINY_INK: return (SDL_Color){0, 0, 0, 255};
    case TINY_PAPER: return (SDL_Color){255, 255, 255, 255};
    case TINY_SHADE: return (SDL_Color){221, 221, 221, 255};
    case TINY_GRAY: return (SDL_Color){153, 153, 153, 255};
    case TINY_DIM: return (SDL_Color){102, 102, 102, 255};
    }
    return (SDL_Color){255, 0, 255, 255};
}

void tiny_set_color(SDL_Renderer *r, TinyColor c) {
    SDL_Color k = tiny_color(c);
    SDL_SetRenderDrawColor(r, k.r, k.g, k.b, k.a);
}

/* ------------------------------------------------------------------ */
/* Shapes. */

void tiny_fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    SDL_FRect rc = {(float)x, (float)y, (float)w, (float)h};
    SDL_RenderFillRect(r, &rc);
}

/* Horizontal inset, in units, of unit row j (0 = outermost) of a corner of radius R. */
static int corner_inset(int R, int j) {
    double dy = R - (j + 0.5);
    return (int)(R - sqrt((double)R * R - dy * dy) + 0.5);
}

void tiny_fill_round(SDL_Renderer *r, int x, int y, int w, int h, int radius, int u, int corners) {
    if (w <= 0 || h <= 0) return;
    if (u < 1) u = 1;
    int R = radius;
    if (2 * R * u > w) R = w / (2 * u);
    if (2 * R * u > h) R = h / (2 * u);
    if (R <= 0 || !corners) {
        tiny_fill_rect(r, x, y, w, h);
        return;
    }
    for (int j = 0; j < R; j++) {
        int in = corner_inset(R, j) * u;
        int l = corners & TINY_TL ? in : 0, rt = corners & TINY_TR ? in : 0;
        tiny_fill_rect(r, x + l, y + j * u, w - l - rt, u);
        l = corners & TINY_BL ? in : 0;
        rt = corners & TINY_BR ? in : 0;
        tiny_fill_rect(r, x + l, y + h - (j + 1) * u, w - l - rt, u);
    }
    tiny_fill_rect(r, x, y + R * u, w, h - 2 * R * u);
}

void tiny_frame_round(SDL_Renderer *r, int x, int y, int w, int h, int radius, int u, int corners,
                      TinyColor fill, TinyColor line) {
    if (u < 1) u = 1;
    tiny_set_color(r, line);
    tiny_fill_round(r, x, y, w, h, radius, u, corners);
    tiny_set_color(r, fill);
    tiny_fill_round(r, x + u, y + u, w - 2 * u, h - 2 * u, radius > 1 ? radius - 1 : 0, u, corners);
}

static int floor_div(int n, int d) { return n >= 0 ? n / d : -1 - (-n - 1) / d; }

void tiny_fill_gray50(SDL_Renderer *r, int x, int y, int w, int h, int u) {
    if (w <= 0 || h <= 0) return;
    if (u < 1) u = 1;
    tiny_set_color(r, TINY_PAPER);
    tiny_fill_rect(r, x, y, w, h);
    tiny_set_color(r, TINY_INK);
    for (int cy = floor_div(y, u); cy * u < y + h; cy++) {
        int y0 = SDL_max(cy * u, y), y1 = SDL_min((cy + 1) * u, y + h);
        for (int cx = floor_div(x, u); cx * u < x + w; cx++) {
            if ((cx + cy) & 1) continue;
            int x0 = SDL_max(cx * u, x), x1 = SDL_min((cx + 1) * u, x + w);
            tiny_fill_rect(r, x0, y0, x1 - x0, y1 - y0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Components. */

TinyColor tiny_button(SDL_Renderer *r, int x, int y, int w, int h, int u, TinyState st) {
    TinyColor face = st == TINY_PRESSED ? TINY_INK : st == TINY_HOVER ? TINY_SHADE : TINY_PAPER;
    tiny_frame_round(r, x, y, w, h, TINY_RADIUS, u, TINY_CORNERS, face, TINY_INK);
    return st == TINY_PRESSED ? TINY_PAPER : TINY_INK;
}

void tiny_panel(SDL_Renderer *r, int x, int y, int w, int h, int u) {
    if (u < 1) u = 1;
    tiny_set_color(r, TINY_INK);
    tiny_fill_round(r, x + u, y + u, w, h, TINY_RADIUS, u, TINY_CORNERS);
    tiny_frame_round(r, x, y, w, h, TINY_RADIUS, u, TINY_CORNERS, TINY_PAPER, TINY_INK);
}

/* ------------------------------------------------------------------ */
/* Font atlas: 16 columns of 6x10 cells, white glyphs on transparent; ASCII
   first, then the extras. */

enum { CELL_W = 6, CELL_H = 10, ATLAS_COLS = 16, GLYPHS = TINY_FONT_ASCII + TINY_FONT_EXTRAS };
#define ATLAS_ROWS ((GLYPHS + ATLAS_COLS - 1) / ATLAS_COLS)

static const uint8_t *glyph_rows(int g) {
    return g < TINY_FONT_ASCII ? tiny_font5x7[g] : tiny_font_extras[g - TINY_FONT_ASCII].rows;
}

bool tiny_font_init(TinyFont *f, SDL_Renderer *r) {
    f->atlas = NULL;
    SDL_Surface *s = SDL_CreateSurface(ATLAS_COLS * CELL_W, ATLAS_ROWS * CELL_H, SDL_PIXELFORMAT_RGBA32);
    if (!s) return false;
    SDL_ClearSurface(s, 0, 0, 0, 0);
    for (int g = 0; g < GLYPHS; g++) {
        const uint8_t *rows = glyph_rows(g);
        int ox = (g % ATLAS_COLS) * CELL_W, oy = (g / ATLAS_COLS) * CELL_H;
        for (int row = 0; row < 9; row++)
            for (int col = 0; col < 5; col++)
                if (rows[row] & (0x10 >> col)) {
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
    /* Nearest at integer scales; stays even at fractional ones (e.g. letterboxed). */
    if (!SDL_SetTextureScaleMode(f->atlas, SDL_SCALEMODE_PIXELART))
        SDL_SetTextureScaleMode(f->atlas, SDL_SCALEMODE_NEAREST);
    return true;
}

void tiny_font_free(TinyFont *f) {
    if (f->atlas) SDL_DestroyTexture(f->atlas);
    f->atlas = NULL;
}

/* Decodes one UTF-8 code point and advances *s (malformed bytes decode as-is). */
static uint32_t next_codepoint(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra) {
        c &= 0x3Fu >> extra;
        for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++) c = (c << 6) | (*p++ & 0x3F);
    }
    *s = (const char *)p;
    return c;
}

/* Atlas index for a code point, or -1 if the font has no glyph for it. */
static int glyph_index(uint32_t c) {
    if (c >= TINY_FONT_FIRST && c < TINY_FONT_FIRST + TINY_FONT_ASCII) return (int)(c - TINY_FONT_FIRST);
    for (int i = 0; i < TINY_FONT_EXTRAS; i++)
        if (tiny_font_extras[i].codepoint == c) return TINY_FONT_ASCII + i;
    return -1;
}

int tiny_text_width(const char *s, int scale, TinyStyle st) {
    int n = 0;
    while (*s) {
        next_codepoint(&s);
        n++;
    }
    int advance = st == TINY_BOLD ? 7 : 6;
    return n ? (n * advance - 1) * scale : 0;
}

int tiny_text(const TinyFont *f, const char *s, int x, int y, int scale, TinyStyle st) {
    int w = tiny_text_width(s, scale, st);
    if (!f->atlas) return w;
    SDL_Renderer *r = SDL_GetRendererFromTexture(f->atlas);
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_SetTextureColorMod(f->atlas, cr, cg, cb);
    SDL_SetTextureAlphaMod(f->atlas, ca);
    int advance = (st == TINY_BOLD ? 7 : 6) * scale;
    for (; *s; x += advance) {
        int g = glyph_index(next_codepoint(&s));
        if (g <= 0) continue; /* space or unsupported */
        SDL_FRect src = {(float)((g % ATLAS_COLS) * CELL_W), (float)((g / ATLAS_COLS) * CELL_H), 5, 9};
        SDL_FRect dst = {(float)x, (float)y, (float)(5 * scale), (float)(9 * scale)};
        SDL_RenderTexture(r, f->atlas, &src, &dst);
        if (st == TINY_BOLD) {
            dst.x += (float)scale;
            SDL_RenderTexture(r, f->atlas, &src, &dst);
        }
    }
    return w;
}

void tiny_text_centered(const TinyFont *f, const char *s, int cx, int y, int scale, TinyStyle st) {
    tiny_text(f, s, cx - tiny_text_width(s, scale, st) / 2, y, scale, st);
}
