/*
 * The Whole Year Puzzle: nine pieces cover a 43-cell calendar board, leaving
 * exactly one month and one day showing. SDL-free so it can be unit tested.
 *
 * Everything lives on one "table" grid of PZ_TABLE_W x PZ_TABLE_H cells. The
 * board sits at its top-left (rows 0-6, cols 0-6); the rest is the tray.
 * Pieces are placed by the top-left of their bounding box in table cells.
 */
#ifndef PUZZLE_H
#define PUZZLE_H

#include <stdbool.h>
#include <stdint.h>

#define PZ_PIECES 9
#define PZ_MAX_CELLS 5
#define PZ_BOARD 7 /* board rows and cols */
#define PZ_TABLE_W 18
#define PZ_TABLE_H 10

typedef struct {
    int r, c;
} PzCell;

/* orient: bits 0-1 rotate clockwise that many quarter turns, bit 2 mirrors
   first. Cell i of a piece is the same physical square in every orientation. */
typedef struct {
    int orient, r, c;
} PzPlace;

int pz_piece_size(int p);
/* Fills out[] with the piece's cells, normalized so the bounding box starts at
   (0,0). Returns the count. */
int pz_piece_cells(int p, int orient, PzCell out[PZ_MAX_CELLS]);
int pz_rotate(int orient); /* a quarter turn clockwise */
int pz_flip(int orient);   /* mirror left-right */

/* Board cells (in table coordinates). */
bool pz_board_cell(int r, int c);
/* Inside the board's 7x7 square but not part of the board. */
bool pz_board_gap(int r, int c);
/* "Jan".."Dec" or "1".."31"; NULL off the board. */
const char *pz_label(int r, int c);
/* The two cells left showing for a date (month 1-12, day 1-31). */
void pz_date_cells(int month, int day, PzCell out[2]);

/* Starting layout: every piece in the tray. */
void pz_tray(PzPlace out[PZ_PIECES]);
/* Whether piece p fits at `at`: inside the table, off the board's gaps, and
   not overlapping any other piece. */
bool pz_fits(const PzPlace pl[PZ_PIECES], int p, PzPlace at);
/* The piece covering a table cell, or -1. */
int pz_owner(const PzPlace pl[PZ_PIECES], int r, int c);
/* All board cells covered except the date's two. */
bool pz_solved(const PzPlace pl[PZ_PIECES], int month, int day);

/* Finds every solution for a date. Writes up to `max` of them to out (each
   PZ_PIECES places, in table coordinates) and returns the total found. */
int pz_solve(int month, int day, PzPlace *out, int max);

/* Calendar helpers. */
int pz_days_in_month(int year, int month);
int pz_weekday(int year, int month, int day); /* 0 = Sunday */
const char *pz_month_name(int month);         /* "January" .. */
const char *pz_weekday_name(int wd);          /* "Sunday" .. */

/* Solved dates, stored one YYYY-MM-DD per line. */
#define PZ_LOG_MAX 8192
typedef struct {
    int n;
    int32_t dates[PZ_LOG_MAX]; /* yyyymmdd */
} PzLog;

static inline int32_t pz_date(int y, int m, int d) { return y * 10000 + m * 100 + d; }
bool pz_log_has(const PzLog *l, int32_t date);
/* Returns true if the date was new. */
bool pz_log_add(PzLog *l, int32_t date);
int pz_log_count_year(const PzLog *l, int year);
/* A missing file loads as empty. Returns false only on a read error. */
bool pz_log_load(PzLog *l, const char *path);
bool pz_log_save(const PzLog *l, const char *path);

#endif
