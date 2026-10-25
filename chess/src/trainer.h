/*
 * The Student's memory of you: how often you make each kind of mistake, kept
 * as a moving average over your recent games against it, plus a correction
 * from results. From it come the Student's own slip chances, a little lower
 * than yours, so it plays just above your level and gets harder as you improve.
 * SDL-free; the app stores it as text (trainer_format / trainer_parse).
 */
#ifndef CHESS_TRAINER_H
#define CHESS_TRAINER_H

#include <stddef.h>

#include "analysis.h"
#include "bot.h"

typedef struct {
    int games;          /* games recorded */
    int graded;         /* of those, analysed ones it learned your rates from */
    double rate[3];     /* your slips per move: inaccuracies, mistakes (and misses), blunders */
    double edge;        /* from results; > 0 after you've been winning, which sharpens the Student */
    int wins, draws, losses;
    unsigned long long last_seed; /* the last game recorded, so none counts twice */
} Trainer;

/* One finished game, as the memory sees it. */
typedef struct {
    unsigned long long seed; /* the game's id */
    double score;            /* yours: 1 win, 0.5 draw, 0 loss */
    int moves;               /* your graded moves (not book, forced or lost already); 0 = not analysed */
    int slips[3];            /* of them: inaccuracies, mistakes (and misses), blunders */
} TrainerGame;

/* A fresh memory: a complete beginner. */
void trainer_init(Trainer *t);

/* Counts `color`'s graded moves and slips in a game's classes[0..ply), with
   evals[0..ply] (White moved first). Fills moves and slips only. Moves from
   positions already lost don't count: no move there can be a real blunder,
   and the Student's own slips can't land there either. */
void trainer_tally(TrainerGame *g, const MoveClass *classes, const PositionEval *evals, int ply, int color);

/* Learns from a game: its result always, your rates only if it was analysed
   and long enough. False (and nothing changed) if `g->seed` was the last one
   recorded. */
bool trainer_record(Trainer *t, const TrainerGame *g);

/* The Student's slip chances per move. */
BotSlips trainer_slips(const Trainer *t);

/* Text form, a few lines. trainer_parse leaves `t` as trainer_init would
   for a missing or unreadable memory, and returns false then. */
void trainer_format(const Trainer *t, char *out, size_t n);
bool trainer_parse(Trainer *t, const char *text);

#endif
