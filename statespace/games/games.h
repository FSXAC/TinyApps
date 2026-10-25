/* The games that plug into the state-space core (SDL-free). */
#ifndef GAMES_H
#define GAMES_H

#include "sv.h"

/* Tic-tac-toe. State: 9 bytes, row-major from the top-left, 0 empty, 1 X,
   2 O. A move is a cell index 0-8. X moves first. */
extern const SvGame sv_ttt;

/* The Whole Year Puzzle (on puzzlecore) for one date. A state holds, per
   piece, 0 if unplaced or 1 + its placement, in 2 bytes; a move is a
   placement. sv_puzzle places any piece anywhere, and its space is explored
   backwards from the solutions. sv_puzzle_lowest must cover the lowest empty
   cell each move (the solver's rule), which is small enough to explore
   forwards, dead ends included. */
extern const SvGame sv_puzzle, sv_puzzle_lowest;
/* Picks the date (month 1-12, day 1-31). False if there's no such date. */
bool sv_puzzle_set_date(int month, int day);
/* The placement of piece p in a state, or -1. */
int sv_puzzle_placed(const void *s, int p);
/* Board cells as bits r * 7 + c. */
uint64_t sv_puzzle_mask(SvMove m);
/* How many placements (moves) there are; each is an index below this. */
int sv_puzzle_placements(void);
uint64_t sv_puzzle_date_mask(void);
uint64_t sv_puzzle_board_mask(void);

#endif
