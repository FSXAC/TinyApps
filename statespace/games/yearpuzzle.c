/* The Whole Year Puzzle as an SvGame, built on puzzlecore's public API. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games.h"
#include "puzzle.h"

#define MAX_PLACEMENTS 2048

static const char *const PIECE_NAMES[PZ_PIECES] = {"L5", "Z5", "X5", "P5", "U5", "T4", "O4", "S4", "L4"};

/* Every placement of every piece on the board (date-independent), distinct
   by the cells covered. A move is an index into this table. */
static uint64_t pl_mask[MAX_PLACEMENTS];
static int8_t pl_piece[MAX_PLACEMENTS];
static int npl, piece_start[PZ_PIECES + 1];

static uint64_t board_mask, date_mask;
static int month = 1, day = 1;
static char name[64], name_lowest[80];

static int bit(int r, int c) { return r * PZ_BOARD + c; }

static void build_placements(void) {
    if (npl) return;
    for (int r = 0; r < PZ_BOARD; r++)
        for (int c = 0; c < PZ_BOARD; c++)
            if (pz_board_cell(r, c)) board_mask |= 1ull << bit(r, c);
    for (int p = 0; p < PZ_PIECES; p++) {
        piece_start[p] = npl;
        for (int o = 0; o < 8; o++)
            for (int r = 0; r < PZ_BOARD; r++)
                for (int c = 0; c < PZ_BOARD; c++) {
                    PzCell cells[PZ_MAX_CELLS];
                    int k = pz_piece_cells(p, o, cells);
                    uint64_t m = 0;
                    bool ok = true;
                    for (int i = 0; i < k && ok; i++) {
                        int rr = r + cells[i].r, cc = c + cells[i].c;
                        if (!pz_board_cell(rr, cc)) ok = false;
                        else m |= 1ull << bit(rr, cc);
                    }
                    for (int j = piece_start[p]; j < npl && ok; j++) ok = pl_mask[j] != m;
                    if (ok) pl_mask[npl] = m, pl_piece[npl++] = (int8_t)p;
                }
    }
    piece_start[PZ_PIECES] = npl;
}

bool sv_puzzle_set_date(int m, int d) {
    if (m < 1 || m > 12 || d < 1 || d > pz_days_in_month(2024, m)) return false; /* 2024: Feb 29 is a date */
    build_placements();
    month = m, day = d;
    PzCell dc[2];
    pz_date_cells(m, d, dc);
    date_mask = (1ull << bit(dc[0].r, dc[0].c)) | (1ull << bit(dc[1].r, dc[1].c));
    snprintf(name, sizeof name, "Year puzzle · %.3s %d", pz_month_name(m), d);
    snprintf(name_lowest, sizeof name_lowest, "%s · lowest cell first", name);
    return true;
}

uint64_t sv_puzzle_mask(SvMove m) { return pl_mask[m]; }
uint64_t sv_puzzle_date_mask(void) { return date_mask; }
uint64_t sv_puzzle_board_mask(void) { return board_mask; }

/* State: per piece, 0 if unplaced or 1 + its placement, as 2 bytes. */
int sv_puzzle_placed(const void *s, int p) {
    const uint8_t *b = s;
    return (b[2 * p] | b[2 * p + 1] << 8) - 1;
}

static void set_placed(void *s, int p, int m) {
    uint8_t *b = s;
    b[2 * p] = (uint8_t)((m + 1) & 0xff), b[2 * p + 1] = (uint8_t)((m + 1) >> 8);
}

static uint64_t filled(const void *s) {
    uint64_t f = date_mask;
    for (int p = 0; p < PZ_PIECES; p++) {
        int m = sv_puzzle_placed(s, p);
        if (m >= 0) f |= pl_mask[m];
    }
    return f;
}

static void initial(void *s) {
    build_placements();
    memset(s, 0, 2 * PZ_PIECES);
}

static int to_move(const void *s) {
    (void)s;
    return 0;
}

static int outcome(const void *s) {
    for (int p = 0; p < PZ_PIECES; p++)
        if (sv_puzzle_placed(s, p) < 0) return SV_PLAYING;
    return 0; /* nine disjoint pieces fill the 41 cells left */
}

/* Placements of unplaced pieces that fit; with `lowest`, only those covering
   the lowest empty cell. */
static int gen_moves(const void *s, SvMove *out, bool lowest) {
    uint64_t f = filled(s), empty = ~f & board_mask, need = empty & (0 - empty); /* the lowest empty cell */
    int n = 0;
    for (int p = 0; p < PZ_PIECES; p++) {
        if (sv_puzzle_placed(s, p) >= 0) continue;
        for (int m = piece_start[p]; m < piece_start[p + 1]; m++)
            if (!(pl_mask[m] & f) && (!lowest || (pl_mask[m] & need))) out[n++] = (SvMove)m;
    }
    return n;
}

static int moves_free(const void *s, SvMove *out) { return gen_moves(s, out, false); }
static int moves_lowest(const void *s, SvMove *out) { return gen_moves(s, out, true); }

static void apply(const void *s, SvMove m, void *out) {
    memcpy(out, s, 2 * PZ_PIECES);
    set_placed(out, pl_piece[m], (int)m);
}

static int unmoves(const void *s, SvMove *out) {
    int n = 0;
    for (int p = 0; p < PZ_PIECES; p++) {
        int m = sv_puzzle_placed(s, p);
        if (m >= 0) out[n++] = (SvMove)m;
    }
    return n;
}

static void unapply(const void *s, SvMove m, void *out) {
    memcpy(out, s, 2 * PZ_PIECES);
    set_placed(out, pl_piece[m], -1);
}

static uint64_t cells_mask(int p, PzPlace at) {
    PzCell cells[PZ_MAX_CELLS];
    int k = pz_piece_cells(p, at.orient, cells);
    uint64_t m = 0;
    for (int i = 0; i < k; i++) m |= 1ull << bit(at.r + cells[i].r, at.c + cells[i].c);
    return m;
}

static void goals(void (*emit)(void *ctx, const void *s), void *ctx) {
    enum { MAX_SOLUTIONS = 4096 };
    PzPlace *sols = malloc(sizeof *sols * PZ_PIECES * MAX_SOLUTIONS);
    if (!sols) return;
    int n = pz_solve(month, day, sols, MAX_SOLUTIONS);
    if (n > MAX_SOLUTIONS) n = MAX_SOLUTIONS;
    for (int i = 0; i < n; i++) {
        uint8_t s[2 * PZ_PIECES];
        for (int p = 0; p < PZ_PIECES; p++) {
            uint64_t m = cells_mask(p, sols[i * PZ_PIECES + p]);
            for (int j = piece_start[p]; j < piece_start[p + 1]; j++)
                if (pl_mask[j] == m) set_placed(s, p, j);
        }
        emit(ctx, s);
    }
    free(sols);
}

static void move_name(const void *s, SvMove m, char *buf, int n) {
    (void)s;
    int b = __builtin_ctzll(pl_mask[m]);
    snprintf(buf, (size_t)n, "%s on %s", PIECE_NAMES[(int)pl_piece[m]], pz_label(b / PZ_BOARD, b % PZ_BOARD));
}

const SvGame sv_puzzle = {
    .name = name,
    .players = 1,
    .state_size = 2 * PZ_PIECES,
    .initial = initial,
    .to_move = to_move,
    .outcome = outcome,
    .moves = moves_free,
    .apply = apply,
    .move_name = move_name,
    .player_names = {"You"},
    .goals = goals,
    .unmoves = unmoves,
    .unapply = unapply,
};

const SvGame sv_puzzle_lowest = {
    .name = name_lowest,
    .players = 1,
    .state_size = 2 * PZ_PIECES,
    .initial = initial,
    .to_move = to_move,
    .outcome = outcome,
    .moves = moves_lowest,
    .apply = apply,
    .move_name = move_name,
    .player_names = {"You"},
};
