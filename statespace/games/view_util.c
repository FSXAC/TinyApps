#include "views.h"

void sv_value_fill(SDL_Renderer *r, int x, int y, int w, int h, int v, int u) {
    if (v == 1 && SDL_min(w, h) >= 3 * u) {
        tiny_frame_round(r, x, y, w, h, 0, u, 0, TINY_PAPER, TINY_INK);
        return;
    }
    tiny_set_color(r, v == 0 ? TINY_INK : v == SV_NOBODY ? TINY_GRAY : v == 1 ? TINY_DIM : TINY_SHADE);
    tiny_fill_rect(r, x, y, w, h);
}
