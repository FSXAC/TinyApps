/* Eval bar visual check: a row of sample evaluations with White at the
   bottom, the same row flipped, and a row of narrow bars (stacked labels).
   Run with TINY_SCREENSHOT=out.bmp to save the first frame and quit. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>

#include "evalbar.h"

#define BEST ((Move){12, 28, 0, 0}) /* any move: "not decided" */

static const PositionEval SAMPLES[] = {
    {0, 0, BEST},       {150, 0, BEST},  {-320, 0, BEST}, {1200, 0, BEST},
    {EVAL_MATE_SCORE, 2, BEST}, {-EVAL_MATE_SCORE, -5, BEST}, {EVAL_MATE_SCORE, 12, BEST},
    {EVAL_MATE_SCORE, 0, MOVE_NONE}, {0, 0, MOVE_NONE},
};
#define NSAMPLES (int)(sizeof SAMPLES / sizeof SAMPLES[0])

static void render(SDL_Window *win, SDL_Renderer *r, const TinyFont *f) {
    int u = tiny_unit(win, r);
    tiny_set_color(r, TINY_PAPER);
    SDL_RenderClear(r);

    int m = TINY_MARGIN * u, cap = TINY_CAPTION * u, bar_h = 120 * u, pitch = (EVALBAR_WIDTH + 12) * u;
    int y = m;
    const struct { int w; bool white_bottom; const char *title; } rows[] = {
        {EVALBAR_WIDTH, true, "White at the bottom"},
        {EVALBAR_WIDTH, false, "Flipped"},
        {10, true, "10u wide: stacked labels"},
    };
    for (int row = 0; row < 3; row++) {
        tiny_set_color(r, TINY_INK);
        tiny_text(f, rows[row].title, m, y, cap, TINY_BOLD);
        y += tiny_line_height(cap);
        for (int i = 0; i < NSAMPLES; i++) {
            int x = m + i * pitch + (EVALBAR_WIDTH - rows[row].w) / 2 * u;
            evalbar_draw(r, f, x, y, rows[row].w * u, bar_h, u, &SAMPLES[i], rows[row].white_bottom);
            char s[16];
            analysis_format(&SAMPLES[i], s, sizeof s);
            tiny_set_color(r, TINY_DIM);
            tiny_text_centered(f, s, m + i * pitch + EVALBAR_WIDTH * u / 2, y + bar_h + TINY_GAP * u, cap,
                               TINY_PLAIN);
        }
        y += bar_h + TINY_GAP * u + tiny_line_height(cap) + TINY_PAD * u;
    }
    tiny_present(r);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    TinyFont font;
    int w = 2 * TINY_MARGIN + NSAMPLES * (EVALBAR_WIDTH + 12) - 12;
    if (!tiny_create_window("Eval bar", w, 470, 200, 200, &win, &ren) || !tiny_font_init(&font, ren)) {
        SDL_Quit();
        return 1;
    }
    bool running = true;
    render(win, ren, &font);
    while (running) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) break;
        do {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && (e.key.mod & SDL_KMOD_GUI) &&
                (e.key.key == SDLK_Q || e.key.key == SDLK_W))
                running = false;
        } while (running && SDL_PollEvent(&e));
        if (running) render(win, ren, &font);
    }
    tiny_font_free(&font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
