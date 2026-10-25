#include <stdio.h>
#include <string.h>

#include "games.h"

static const int LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                                {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

/* The 8 symmetries of the square, as where each cell comes from. */
static const int SYMS[8][9] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8}, {6, 3, 0, 7, 4, 1, 8, 5, 2}, {8, 7, 6, 5, 4, 3, 2, 1, 0},
    {2, 5, 8, 1, 4, 7, 0, 3, 6}, {2, 1, 0, 5, 4, 3, 8, 7, 6}, {6, 7, 8, 3, 4, 5, 0, 1, 2},
    {0, 3, 6, 1, 4, 7, 2, 5, 8}, {8, 5, 2, 7, 4, 1, 6, 3, 0},
};

static void initial(void *s) { memset(s, 0, 9); }

static int to_move(const void *s) {
    const uint8_t *b = s;
    int n = 0;
    for (int i = 0; i < 9; i++) n += b[i] != 0;
    return n & 1;
}

static int outcome(const void *s) {
    const uint8_t *b = s;
    for (int i = 0; i < 8; i++) {
        int a = b[LINES[i][0]];
        if (a && a == b[LINES[i][1]] && a == b[LINES[i][2]]) return a - 1;
    }
    for (int i = 0; i < 9; i++)
        if (!b[i]) return SV_PLAYING;
    return SV_NOBODY;
}

static int moves(const void *s, SvMove *out) {
    const uint8_t *b = s;
    int n = 0;
    for (int i = 0; i < 9; i++)
        if (!b[i]) out[n++] = (SvMove)i;
    return n;
}

static void apply(const void *s, SvMove m, void *out) {
    uint8_t *b = out;
    memcpy(b, s, 9);
    b[m] = (uint8_t)(to_move(s) + 1);
}

static void canonical(void *s) {
    uint8_t *b = s, best[9], t[9];
    memcpy(best, b, 9);
    for (int k = 1; k < 8; k++) {
        for (int i = 0; i < 9; i++) t[i] = b[SYMS[k][i]];
        if (memcmp(t, best, 9) < 0) memcpy(best, t, 9);
    }
    memcpy(b, best, 9);
}

static void move_name(const void *s, SvMove m, char *buf, int n) {
    /* Columns a-c from the left, rows 1-3 from the bottom. */
    snprintf(buf, (size_t)n, "%s %c%d", to_move(s) ? "O" : "X", 'a' + (int)m % 3, 3 - (int)m / 3);
}

const SvGame sv_ttt = {
    .name = "Tic-tac-toe",
    .players = 2,
    .state_size = 9,
    .initial = initial,
    .to_move = to_move,
    .outcome = outcome,
    .moves = moves,
    .apply = apply,
    .canonical = canonical,
    .move_name = move_name,
    .player_names = {"X", "O"},
};
