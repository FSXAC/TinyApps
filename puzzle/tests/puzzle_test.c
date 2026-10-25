#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "puzzle.h"

/* Draws a solution as 7 rows of piece digits, '#' for the date, '.' off board. */
static void draw(const PzPlace *pl, int month, int day, char out[PZ_BOARD][PZ_BOARD + 1]) {
    PzCell d[2];
    pz_date_cells(month, day, d);
    for (int r = 0; r < PZ_BOARD; r++) {
        for (int c = 0; c < PZ_BOARD; c++) {
            int p = pz_owner(pl, r, c);
            out[r][c] = !pz_board_cell(r, c) ? '.' : p >= 0 ? (char)('0' + p) : '#';
        }
        out[r][PZ_BOARD] = 0;
    }
    (void)d;
}

static void test_pieces(void) {
    int total = 0;
    for (int p = 0; p < PZ_PIECES; p++) total += pz_piece_size(p);
    CHECK(total == 41); /* 43 board cells minus the two showing */
    for (int o = 0; o < 8; o++) {
        CHECK(pz_rotate(pz_rotate(pz_rotate(pz_rotate(o)))) == o);
        CHECK(pz_flip(pz_flip(o)) == o);
    }
    /* Flip is a left-right mirror on screen, whatever the rotation. */
    for (int o = 0; o < 8; o++) {
        PzCell a[PZ_MAX_CELLS], b[PZ_MAX_CELLS];
        int n = pz_piece_cells(0, o, a);
        pz_piece_cells(0, pz_flip(o), b);
        int w = 0;
        for (int i = 0; i < n; i++) w = a[i].c > w ? a[i].c : w;
        for (int i = 0; i < n; i++) CHECK(b[i].r == a[i].r && b[i].c == w - a[i].c);
    }
}

static void test_board(void) {
    int n = 0;
    for (int r = 0; r < PZ_BOARD; r++)
        for (int c = 0; c < PZ_BOARD; c++) n += pz_board_cell(r, c);
    CHECK(n == 43);
    CHECK(strcmp(pz_label(0, 0), "Jan") == 0);
    CHECK(strcmp(pz_label(1, 5), "Dec") == 0);
    CHECK(strcmp(pz_label(2, 6), "7") == 0);
    CHECK(strcmp(pz_label(6, 4), "31") == 0);
    CHECK(pz_label(0, 6) == NULL);
    for (int m = 1; m <= 12; m++)
        for (int d = 1; d <= 31; d++) {
            PzCell c[2];
            pz_date_cells(m, d, c);
            CHECK(strncmp(pz_label(c[0].r, c[0].c), pz_month_name(m), 3) == 0);
            CHECK(atoi(pz_label(c[1].r, c[1].c)) == d);
        }
}

static void test_tray(void) {
    PzPlace pl[PZ_PIECES];
    pz_tray(pl);
    for (int p = 0; p < PZ_PIECES; p++) {
        CHECK(pz_fits(pl, p, pl[p]));
        /* The tray is entirely off the board. */
        PzCell cells[PZ_MAX_CELLS];
        int n = pz_piece_cells(p, pl[p].orient, cells);
        for (int i = 0; i < n; i++) CHECK(!pz_board_cell(pl[p].r + cells[i].r, pl[p].c + cells[i].c));
    }
    CHECK(!pz_solved(pl, 10, 2));
    PzPlace bad = {0, 0, 5}; /* P5 hits the gap at (0,6) */
    CHECK(!pz_fits(pl, 3, bad));
    bad = (PzPlace){0, 0, 9}; /* overlaps Z5 */
    CHECK(!pz_fits(pl, 3, bad));
}

static void test_photo_solution(void) {
    /* The arrangement in the photo of the real board, for Oct 2. */
    static const char *want[PZ_BOARD] = {
        "007711.", "077#51.", "0#25511", "0222544", "6623884", "6633844", "..338..",
    };
    static PzPlace sols[4096 * PZ_PIECES];
    int n = pz_solve(10, 2, sols, 4096);
    CHECK(n > 1 && n <= 4096);
    bool found = false;
    for (int i = 0; i < n && i < 4096; i++) {
        char g[PZ_BOARD][PZ_BOARD + 1];
        CHECK(pz_solved(sols + i * PZ_PIECES, 10, 2));
        draw(sols + i * PZ_PIECES, 10, 2, g);
        bool same = true;
        for (int r = 0; r < PZ_BOARD; r++) same &= strcmp(g[r], want[r]) == 0;
        found |= same;
    }
    CHECK(found);
}

static void test_every_date_solvable(void) {
    int min = 1 << 30, max = 0;
    for (int m = 1; m <= 12; m++)
        for (int d = 1; d <= pz_days_in_month(2024, m); d++) {
            int n = pz_solve(m, d, NULL, 0);
            CHECK(n > 0);
            if (n < min) min = n;
            if (n > max) max = n;
        }
    printf("solutions per date: %d to %d\n", min, max);
}

static void test_calendar(void) {
    CHECK(pz_weekday(2026, 10, 2) == 5); /* Friday */
    CHECK(pz_weekday(2000, 1, 1) == 6);
    CHECK(pz_days_in_month(2024, 2) == 29);
    CHECK(pz_days_in_month(2026, 2) == 28);
    CHECK(pz_days_in_month(1900, 2) == 28);
}

static void test_log(void) {
    static PzLog l, back;
    CHECK(pz_log_add(&l, pz_date(2026, 10, 2)));
    CHECK(!pz_log_add(&l, pz_date(2026, 10, 2)));
    CHECK(pz_log_add(&l, pz_date(2025, 12, 31)));
    CHECK(pz_log_count_year(&l, 2026) == 1);
    char path[] = "/tmp/puzzle_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(pz_log_save(&l, path));
    CHECK(pz_log_load(&back, path));
    CHECK(back.n == 2 && pz_log_has(&back, pz_date(2026, 10, 2)) && pz_log_has(&back, pz_date(2025, 12, 31)));
    remove(path);
    CHECK(pz_log_load(&back, path)); /* missing file: empty */
    CHECK(back.n == 0);
}

int main(void) {
    test_pieces();
    test_board();
    test_tray();
    test_photo_solution();
    test_every_date_solvable();
    test_calendar();
    test_log();
    return check_report();
}
