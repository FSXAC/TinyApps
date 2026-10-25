#include "games.h"
#include "puzzle.h"
#include "views.h"

static int owner(const void *s, int b) {
    for (int p = 0; p < PZ_PIECES; p++) {
        int m = sv_puzzle_placed(s, p);
        if (m >= 0 && (sv_puzzle_mask((SvMove)m) >> b & 1)) return p;
    }
    return -1;
}

static void draw(SDL_Renderer *r, const TinyFont *f, const void *s, int x, int y, int size, int u) {
    int cs = size / 7, gap = cs >= 5 * u ? u : 0;
    bool big = cs >= 14 * u;
    uint64_t board = sv_puzzle_board_mask(), date = sv_puzzle_date_mask();
    int own[PZ_BOARD * PZ_BOARD];
    for (int b = 0; b < PZ_BOARD * PZ_BOARD; b++) own[b] = board >> b & 1 ? owner(s, b) : -2;
    tiny_set_color(r, TINY_PAPER);
    tiny_fill_rect(r, x, y, size, size);
    for (int b = 0; b < PZ_BOARD * PZ_BOARD; b++) {
        if (own[b] == -2) continue;
        int row = b / PZ_BOARD, col = b % PZ_BOARD, cx = x + col * cs, cy = y + row * cs;
        if (own[b] >= 0) {
            /* Pieces in ink, with a paper seam where two pieces meet. */
            tiny_set_color(r, TINY_INK);
            tiny_fill_rect(r, cx + gap, cy + gap, cs - 2 * gap, cs - 2 * gap);
            bool right = col + 1 < PZ_BOARD && own[b + 1] == own[b];
            bool down = row + 1 < PZ_BOARD && own[b + PZ_BOARD] == own[b];
            if (right) tiny_fill_rect(r, cx + cs - gap, cy + gap, 2 * gap, cs - 2 * gap);
            if (down) tiny_fill_rect(r, cx + gap, cy + cs - gap, cs - 2 * gap, 2 * gap);
            if (right && down && own[b + PZ_BOARD + 1] == own[b]) tiny_fill_rect(r, cx + cs - gap, cy + cs - gap, 2 * gap, 2 * gap);
            continue;
        }
        bool is_date = date >> b & 1;
        if (!big) {
            tiny_set_color(r, is_date ? TINY_GRAY : TINY_SHADE);
            tiny_fill_rect(r, cx + gap, cy + gap, cs - 2 * gap, cs - 2 * gap);
            continue;
        }
        /* Big: the empty board as a grid of labels, the date framed. */
        tiny_frame_round(r, cx, cy, cs, cs, 0, u, 0, TINY_PAPER, TINY_SHADE);
        if (is_date) tiny_frame_round(r, cx + 2 * u, cy + 2 * u, cs - 4 * u, cs - 4 * u, 0, u, 0, TINY_PAPER, TINY_INK);
        const char *label = pz_label(row, col);
        tiny_set_color(r, is_date ? TINY_INK : TINY_DIM);
        tiny_text_centered(f, label, cx + cs / 2, cy + (is_date ? (cs - tiny_text_height(u)) / 2 : 3 * u), u,
                           is_date ? TINY_BOLD : TINY_PLAIN);
    }
}

static bool move_hit(const void *s, SvMove m, int size, int px, int py) {
    (void)s;
    int cs = size / 7;
    if (px < 0 || py < 0 || px >= 7 * cs || py >= 7 * cs) return false;
    return sv_puzzle_mask(m) >> (py / cs * PZ_BOARD + px / cs) & 1;
}

/* On each empty cell, how many moves covering it keep the puzzle solvable;
   a cell nothing solvable can cover is dithered. From a dead state, the
   count of moves that fit at all instead, in gray. Under the lowest-cell
   rule most cells have no moves yet, which says nothing, so they stay blank. */
static void annotate_rule(SDL_Renderer *r, const TinyFont *f, const void *s, const SvMove *moves, const int8_t *values,
                          int n, int x, int y, int size, int u, int fx, int fy, bool lowest) {
    int cs = size / 7;
    if (cs < 14 * u) return;
    bool alive = false;
    for (int i = 0; i < n; i++) alive |= values[i] == 0;
    uint64_t board = sv_puzzle_board_mask(), date = sv_puzzle_date_mask();
    for (int b = 0; b < PZ_BOARD * PZ_BOARD; b++) {
        if (!(board >> b & 1) || (date >> b & 1) || owner(s, b) >= 0) continue;
        int k = 0, legal = 0;
        for (int i = 0; i < n; i++) {
            bool covers = sv_puzzle_mask(moves[i]) >> b & 1;
            legal += covers, k += covers && (!alive || values[i] == 0);
        }
        if (lowest && !legal) continue;
        int cx = x + b % PZ_BOARD * cs, cy = y + b / PZ_BOARD * cs;
        if (!k) {
            tiny_fill_gray50(r, cx + u, cy + 3 * u + tiny_text_height(u) + u, cs - 2 * u, cs - 5 * u - tiny_text_height(u), u);
            continue;
        }
        char buf[8];
        SDL_snprintf(buf, sizeof buf, "%d", k);
        tiny_set_color(r, alive ? TINY_INK : TINY_GRAY);
        tiny_text_centered(f, buf, cx + cs / 2, cy + cs - 3 * u - tiny_text_height(u), u, TINY_BOLD);
    }
    if (fx >= 0 && fy >= 0 && fx < 7 * cs && fy < 7 * cs) {
        int cx = x + fx / cs * cs, cy = y + fy / cs * cs;
        tiny_set_color(r, TINY_INK);
        tiny_fill_rect(r, cx + u, cy + u, cs - 2 * u, u);
        tiny_fill_rect(r, cx + u, cy + cs - 2 * u, cs - 2 * u, u);
        tiny_fill_rect(r, cx + u, cy + u, u, cs - 2 * u);
        tiny_fill_rect(r, cx + cs - 2 * u, cy + u, u, cs - 2 * u);
    }
}

static void annotate(SDL_Renderer *r, const TinyFont *f, const void *s, const SvMove *moves, const int8_t *values,
                     int n, int x, int y, int size, int u, int fx, int fy) {
    annotate_rule(r, f, s, moves, values, n, x, y, size, u, fx, fy, false);
}

static void annotate_lowest(SDL_Renderer *r, const TinyFont *f, const void *s, const SvMove *moves,
                            const int8_t *values, int n, int x, int y, int size, int u, int fx, int fy) {
    annotate_rule(r, f, s, moves, values, n, x, y, size, u, fx, fy, true);
}

const SvView sv_puzzle_view = {draw, move_hit, annotate};
const SvView sv_puzzle_lowest_view = {draw, move_hit, annotate_lowest};
