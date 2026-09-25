/*
 * Tempo - a tap BPM counter built on SDL3.
 * Tap any key or click anywhere; the count restarts after a pause.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tempo.h"
#include "tiny.h"

#define FLASH_NS (90 * SDL_NS_PER_MS)

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TinyFont font;
    Tempo tempo;
    uint64_t flash_until;
} App;

static void tap(App *a, uint64_t when) {
    tempo_tap(&a->tempo, when);
    a->flash_until = SDL_GetTicksNS() + FLASH_NS;
}

static void render(App *a) {
    SDL_Renderer *r = a->ren;
    uint64_t now = SDL_GetTicksNS();
    bool flash = now < a->flash_until;
    bool expired = tempo_expired(&a->tempo, now);
    double bpm = tempo_bpm(&a->tempo);

    int w, h;
    SDL_GetRenderOutputSize(r, &w, &h);

    if (flash) SDL_SetRenderDrawColor(r, 48, 48, 56, 255);
    else SDL_SetRenderDrawColor(r, 24, 24, 28, 255);
    SDL_RenderClear(r);

    /* Size the big number for 3 digits so it doesn't jump around. */
    int big = SDL_min(w * 7 / 10 / tiny_text_width("000", 1), h * 2 / 5 / 7);
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
    tiny_text_centered(&a->font, num, w / 2, y, big);

    SDL_SetRenderDrawColor(r, 150, 150, 158, 255);
    tiny_text_centered(&a->font, sub, w / 2, y + 7 * big + 4 * small, small);

    SDL_SetRenderDrawColor(r, 90, 90, 98, 255);
    tiny_text_centered(&a->font, status, w / 2, h - 7 * small - 5 * small, small);

    SDL_RenderPresent(r);
}

/* Milliseconds until the display next changes on its own, or -1 for none. */
static int next_timeout(const App *a) {
    uint64_t now = SDL_GetTicksNS();
    uint64_t due = 0;
    if (now < a->flash_until) due = a->flash_until;
    if (a->tempo.count > 0 && !tempo_expired(&a->tempo, now)) {
        uint64_t e = tempo_last(&a->tempo) + TEMPO_RESET_NS + 1;
        if (!due || e < due) due = e;
    }
    if (!due) return -1;
    if (due <= now) return 1;
    return (int)((due - now + SDL_NS_PER_MS - 1) / SDL_NS_PER_MS);
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
    case SDL_EVENT_QUIT:
        return false;
    case SDL_EVENT_KEY_DOWN: {
        const SDL_KeyboardEvent *k = &e->key;
        if (k->repeat || is_modifier(k->key)) break;
        if (k->mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
            if (k->key == SDLK_Q || k->key == SDLK_W) return false;
            break;
        }
        if (k->key == SDLK_ESCAPE || k->key == SDLK_BACKSPACE || k->key == SDLK_DELETE) {
            tempo_reset(&a->tempo);
            break;
        }
        tap(a, k->timestamp);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
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
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!tiny_create_window("Tempo", 360, 260, 160, 120, &a->win, &a->ren) ||
        !tiny_font_init(&a->font, a->ren)) {
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

    tiny_font_free(&a->font);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}
