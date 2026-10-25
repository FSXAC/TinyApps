#include "games.h"
#include "views.h"

static void draw(SDL_Renderer *r, const TinyFont *f, const void *s, int x, int y, int size, int u) {
    const uint8_t *b = s;
    int cs = size / 3;
    tiny_set_color(r, TINY_PAPER);
    tiny_fill_rect(r, x, y, size, size);
    if (cs < 12 * u) {
        /* Thumbnail: X a solid square, O a hollow one. */
        int gap = cs >= 4 * u ? u : 0;
        for (int i = 0; i < 9; i++) {
            int cx = x + i % 3 * cs + gap, cy = y + i / 3 * cs + gap, w = cs - 2 * gap;
            if (b[i] == 1) {
                tiny_set_color(r, TINY_INK);
                tiny_fill_rect(r, cx, cy, w, w);
            } else if (b[i] == 2) {
                tiny_frame_round(r, cx, cy, w, w, 0, u, 0, TINY_PAPER, TINY_INK);
            } else {
                tiny_set_color(r, TINY_SHADE);
                tiny_fill_rect(r, cx, cy, w, w);
            }
        }
        return;
    }
    tiny_set_color(r, TINY_INK);
    for (int k = 1; k < 3; k++) {
        tiny_fill_rect(r, x + k * cs - u / 2, y, u, size);
        tiny_fill_rect(r, x, y + k * cs - u / 2, size, u);
    }
    /* Bitmap glyphs about 60% of a cell tall. */
    int scale = SDL_max(cs * 6 / 10 / 7 / u, 1) * u;
    for (int i = 0; i < 9; i++) {
        if (!b[i]) continue;
        const char *g = b[i] == 1 ? "X" : "O";
        tiny_text_centered(f, g, x + i % 3 * cs + cs / 2, y + i / 3 * cs + (cs - tiny_text_height(scale)) / 2, scale,
                           TINY_BOLD);
    }
}

static bool move_hit(const void *s, SvMove m, int size, int px, int py) {
    (void)s;
    int cs = size / 3;
    return px / cs == (int)m % 3 && py / cs == (int)m / 3 && px >= 0 && py >= 0;
}

/* Each open square gets the mark of the value it leads to. */
static void annotate(SDL_Renderer *r, const TinyFont *f, const void *s, const SvMove *moves, const int8_t *values,
                     int n, int x, int y, int size, int u, int fx, int fy) {
    (void)f;
    int cs = size / 3;
    for (int i = 0; i < n; i++) {
        int cx = x + (int)moves[i] % 3 * cs, cy = y + (int)moves[i] / 3 * cs;
        if (move_hit(s, moves[i], size, fx, fy)) {
            tiny_set_color(r, TINY_SHADE);
            tiny_fill_rect(r, cx + 2 * u, cy + 2 * u, cs - 4 * u, cs - 4 * u);
        }
        int k = cs / 4 / u * u;
        sv_value_fill(r, cx + (cs - k) / 2, cy + (cs - k) / 2, k, k, values[i], u);
    }
}

const SvView sv_ttt_view = {draw, move_hit, annotate};
