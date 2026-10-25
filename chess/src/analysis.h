/*
 * Post-game analysis: engine evaluation of positions, move classification
 * ("Best", "Blunder", ...) and accuracy, like a chess.com game review.
 * SDL-free (part of `chesscore`) and thread safe; the app runs it on a worker.
 */
#ifndef CHESS_ANALYSIS_H
#define CHESS_ANALYSIS_H

#include "board.h"

/* |score| for a decided game; forced mates are reported as +/- this too. */
#define EVAL_MATE_SCORE 10000

/* Classification thresholds on expected-points loss (0..1), like chess.com's:
   below GOOD a move is Good or better, then Inaccuracy, Mistake, and Blunder
   from MISTAKE up. */
#define ANALYSIS_LOSS_EXCELLENT 0.02
#define ANALYSIS_LOSS_GOOD 0.05
#define ANALYSIS_LOSS_INACCURACY 0.10
#define ANALYSIS_LOSS_MISTAKE 0.20

typedef struct {
    int score; /* centipawns from White's point of view, clamped to +/-EVAL_MATE_SCORE */
    int mate;  /* forced mate in |mate| moves: > 0 White mates, < 0 Black mates, 0 none */
    Move best; /* engine's best move here, or MOVE_NONE when the game is over */
} PositionEval;

typedef enum {
    CLASS_NONE,
    CLASS_BRILLIANT,
    CLASS_BEST,
    CLASS_EXCELLENT,
    CLASS_GOOD,
    CLASS_BOOK,
    CLASS_INACCURACY,
    CLASS_MISTAKE,
    CLASS_MISS,
    CLASS_BLUNDER,
    CLASS_FORCED,
    CLASS_COUNT
} MoveClass;

/* Evaluates one position. `history` holds the hashes of the earlier game
   positions (for repetition), as game_history() fills them. Deterministic.
   `stop` (may be NULL) aborts early with the best result so far. */
PositionEval analysis_evaluate(const Position *p, const uint64_t *history, int history_len,
                               const volatile int *stop);

/* Classifies `played` from `before`, given the evaluations of the position
   before and after the move. */
MoveClass analysis_classify(const Position *before, Move played, const PositionEval *before_eval,
                            const PositionEval *after_eval);

/* Accuracy (0-100) of `color`'s moves, from evals[0..ply] of one game. */
double analysis_accuracy(const PositionEval *evals, int ply, int color);

const char *analysis_class_name(MoveClass c);   /* "Blunder" */
const char *analysis_class_symbol(MoveClass c); /* "??", or "" when it has none */

/* Short eval label: "+1.3", "-0.4", "M3", "-M2", "1-0", "0-1", "1/2". */
void analysis_format(const PositionEval *e, char *out, size_t n);

/* ------------------------------------------------------------------ */
/* Extras. */

/* analysis_accuracy() for a game whose first move is `first_side`'s
   (analysis_accuracy() assumes White, as from the standard start). */
double analysis_accuracy_from(const PositionEval *evals, int ply, int color, int first_side);

/* A short, never-empty badge for a class, for move-list chips: "!!" "*" "!"
   "+" "=" "?!" "?" "x" "??" and "→" (an arrow) for Forced. */
const char *analysis_class_glyph(MoveClass c);

/* True for a game already over: mated (score +/-EVAL_MATE_SCORE, mate 0) or
   drawn by rule (score 0), both with best == MOVE_NONE. Note that a
   zero-initialized PositionEval also reads as a draw, so track "not yet
   evaluated" separately. */
bool analysis_is_decided(const PositionEval *e);

/* Expected points (0..1) for `color` from an evaluation, the scale move
   losses are measured on. */
double analysis_points(const PositionEval *e, int color);

/* One legal move, scored as expected points (0..1) for the side that plays it. */
typedef struct {
    Move move;
    double points; /* after the deepest search that finished */
    double glance; /* after a one-move look plus captures: how good it seems at first sight */
} RankedMove;

/* Scores every legal move of `p` (for bots that choose how well to play).
   Fills `out` (room for MAX_MOVES) in move-generation order and returns the
   count. Bounded by `budget` nodes; `stop` (may be NULL) ends it early. */
int analysis_rank_moves(const Position *p, const uint64_t *history, int history_len, long budget,
                        RankedMove *out, const volatile int *stop);

#endif
