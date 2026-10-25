/*
 * Opening book: well-known opening lines, used by the review to grade moves
 * as "Book" and to name the opening. Positions are matched by hash, so a line
 * reached in a different move order still counts. SDL-free and thread safe.
 */
#ifndef CHESS_BOOK_H
#define CHESS_BOOK_H

#include "board.h"

/* True if `p` is the start position or lies on a book line. If a named line
   ends exactly at `p`, `*name` (may be NULL) gets its name, else NULL. */
bool book_lookup(const Position *p, const char **name);

/* True if playing `m` in `before` stays in the book (both positions are book positions). */
bool book_move(const Position *before, Move m);

/* The lines themselves, for tests: `moves` is space-separated UCI from the start. */
typedef struct {
    const char *name;
    const char *moves;
} BookLine;

extern const BookLine book_lines[];
extern const int book_line_count;

#endif
