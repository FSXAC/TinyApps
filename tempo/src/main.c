/*
 * TapTempo - a tap BPM counter built on SDL2.
 * Tap any key or click anywhere; the count restarts after a pause.
 */
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tempo.h"

#define FLASH_MS 90

/* ------------------------------------------------------------------ */
/* Tiny 5x7 bitmap font (only the glyphs used by the UI).               */

typedef struct { char c; uint8_t rows[7]; } Glyph;
static const Glyph FONT[] = {
    {'0', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
};

static const Glyph *find_glyph(char c) {
    for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
        if (FONT[i].c == c) return &FONT[i];
    return NULL;
}

static int text_width(const char *s, int scale) {
    int n = (int)strlen(s);
    return n ? (n * 6 - 1) * scale : 0;
}

static void draw_text(SDL_Renderer *r, const char *s, int x, int y, int scale) {
    for (; *s; s++, x += 6 * scale) {
        const Glyph *g = find_glyph(*s);
        if (!g) continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g->rows[row] & (0x10 >> col)) {
                    SDL_Rect rc = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(r, &rc);
                }
    }
}

static void draw_centered(SDL_Renderer *r, const char *s, int cx, int y, int scale) {
    draw_text(r, s, cx - text_width(s, scale) / 2, y, scale);
}

/* ------------------------------------------------------------------ */

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    Tempo tempo;
    uint32_t flash_until;
} App;

static void tap(App *a, uint32_t when) {
    tempo_tap(&a->tempo, when);
    a->flash_until = SDL_GetTicks() + FLASH_MS;
}

static void render(App *a) {
    SDL_Renderer *r = a->ren;
    uint32_t now = SDL_GetTicks();
    bool flash = (int32_t)(a->flash_until - now) > 0;
    bool expired = tempo_expired(&a->tempo, now);
    double bpm = tempo_bpm(&a->tempo);

    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);

    if (flash) SDL_SetRenderDrawColor(r, 48, 48, 56, 255);
    else SDL_SetRenderDrawColor(r, 24, 24, 28, 255);
    SDL_RenderClear(r);

    /* Size the big number for 3 digits so it doesn't jump around. */
    int big = SDL_min(w * 7 / 10 / text_width("000", 1), h * 2 / 5 / 7);
    if (big < 1) big = 1;
    int small = SDL_max(1, SDL_min(w, h) / 110);

    char num[16], sub[32], status[32];
    if (bpm > 0) {
        double shown = SDL_min(bpm, 999.0);
        snprintf(num, sizeof num, "%d", (int)(shown + 0.5));
        snprintf(sub, sizeof sub, "%.1f BPM", shown);
    } else {
        snprintf(num, sizeof num, "---");
        snprintf(sub, sizeof sub, "BPM");
    }
    if (a->tempo.count == 0 || expired) snprintf(status, sizeof status, "TAP A KEY OR CLICK");
    else snprintf(status, sizeof status, "%d TAP%s", a->tempo.count, a->tempo.count == 1 ? "" : "S");

    int block_h = 7 * big + 4 * small + 7 * small;
    int y = (h - block_h) / 2 - 4 * small;

    /* Dim the reading once it has expired: the next tap starts over. */
    if (bpm > 0 && !expired) SDL_SetRenderDrawColor(r, 240, 240, 240, 255);
    else SDL_SetRenderDrawColor(r, 110, 110, 118, 255);
    draw_centered(r, num, w / 2, y, big);

    SDL_SetRenderDrawColor(r, 150, 150, 158, 255);
    draw_centered(r, sub, w / 2, y + 7 * big + 4 * small, small);

    SDL_SetRenderDrawColor(r, 90, 90, 98, 255);
    draw_centered(r, status, w / 2, h - 7 * small - 5 * small, small);

    SDL_RenderPresent(r);
}

/* Milliseconds until the display next changes on its own, or -1 for none. */
static int next_timeout(const App *a) {
    uint32_t now = SDL_GetTicks();
    int t = -1;
    int32_t flash = (int32_t)(a->flash_until - now);
    if (flash > 0) t = flash;
    if (a->tempo.count > 0 && !tempo_expired(&a->tempo, now)) {
        int e = (int)(tempo_last(&a->tempo) + TEMPO_RESET_MS - now) + 1;
        if (e < 1) e = 1;
        if (t < 0 || e < t) t = e;
    }
    return t;
}

static bool is_modifier(SDL_Keycode k) {
    switch (k) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL:
    case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI:
    case SDLK_CAPSLOCK: case SDLK_MODE:
        return true;
    default:
        return false;
    }
}

/* Returns false to quit. */
static bool handle_event(App *a, const SDL_Event *e) {
    switch (e->type) {
    case SDL_QUIT:
        return false;
    case SDL_KEYDOWN: {
        const SDL_Keysym *ks = &e->key.keysym;
        if (e->key.repeat || is_modifier(ks->sym)) break;
        if (ks->mod & (KMOD_GUI | KMOD_CTRL)) {
            if (ks->sym == SDLK_q || ks->sym == SDLK_w) return false;
            break;
        }
        if (ks->sym == SDLK_ESCAPE || ks->sym == SDLK_BACKSPACE || ks->sym == SDLK_DELETE) {
            tempo_reset(&a->tempo);
            break;
        }
        tap(a, e->key.timestamp);
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        tap(a, e->button.timestamp);
        break;
    }
    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    App app = {0};
    App *a = &app;
    tempo_reset(&a->tempo);

    /* The click that focuses the window should count as a tap. */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    a->win = SDL_CreateWindow("Tempo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 360, 260,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!a->win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(a->win, 160, 120);
    a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a->ren) a->ren = SDL_CreateRenderer(a->win, -1, SDL_RENDERER_SOFTWARE);
    if (!a->ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(a->win);
        SDL_Quit();
        return 1;
    }

    /* Event-driven: redraw only after events or when a flash / auto-reset is due. */
    bool running = true;
    render(a);
    while (running) {
        SDL_Event e;
        int timeout = next_timeout(a);
        bool got = timeout < 0 ? SDL_WaitEvent(&e) : SDL_WaitEventTimeout(&e, timeout);
        if (got) {
            running = handle_event(a, &e);
            while (running && SDL_PollEvent(&e)) running = handle_event(a, &e);
        }
        if (running) render(a);
    }

    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}
