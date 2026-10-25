/*
 * Bot API. A bot is a name plus a `choose` function that picks a move. SDL-free.
 *
 * Each bot lives in its own file in this directory. To add one: write
 * `<name>.c` defining a `const ChessBot bot_<name>`, declare it below, list it
 * in `chess_bots` (bot.c) and add the file to `chesscore` in CMakeLists.txt.
 * The app runs `choose` on a worker thread with its own copy of the request,
 * so bots must not keep shared mutable state. See ../../BOTS.md.
 */
#ifndef CHESS_BOT_H
#define CHESS_BOT_H

#include "board.h"

/* Chances per move of a deliberate slip of each grade, for the Student, which
   plays at your level. The app fills them from its memory (trainer.h). */
typedef struct {
    double inaccuracy, mistake, blunder;
} BotSlips;

typedef struct {
    Position pos;              /* the position to move in; pos.side is the bot's color */
    const uint64_t *history;   /* hashes of the earlier game positions, oldest first */
    int history_len;
    uint64_t seed;             /* differs per game and per move, for variety */
    const volatile int *stop;  /* becomes nonzero when the answer is no longer wanted */
    int level;                 /* index into the bot's levels (0 if it has none) */
    BotSlips slips;            /* Student only: how often it slips (zeros = never) */
} BotRequest;

/* A strength setting, picked before the game. */
typedef struct {
    const char *name;  /* "Normal" */
    const char *about; /* one line for the side panel */
} BotLevel;

typedef struct {
    const char *name;  /* shown over the board */
    const char *about; /* one line for the side panel */
    /* Returns a legal move (any legal move if `stop` is raised early). Only
       called when the position has at least one legal move. */
    Move (*choose)(const BotRequest *req);
    const BotLevel *levels; /* weakest first, or NULL for a single strength */
    int level_count;
    int default_level;
} ChessBot;

/* The roster, in the new-game picker's order. */
extern const ChessBot bot_student; /* student.c: plays at your level, from memory */
extern const ChessBot bot_tiny;   /* tiny.c: alpha-beta search, three levels */
extern const ChessBot bot_greedy; /* greedy.c: takes the biggest capture */
extern const ChessBot bot_mirror; /* mirror.c: copies your moves */
extern const ChessBot bot_queenie; /* queenie.c: checks with its queen */

extern const ChessBot *const chess_bots[];
extern const int chess_bot_count;

/* ------------------------------------------------------------------ */
/* Helpers for bots that score moves one by one rather than search. */

/* True if `hash` appears in the request's history (a repeat of a game position). */
bool bot_seen(const BotRequest *req, uint64_t hash);

/* Piece values in centipawns by type (P 100 ... Q 900, K 0). */
extern const int bot_value[7];

/* Material `m` wins on the spot: the captured piece plus any promotion gain. */
int bot_gain(const Position *p, Move m);

/* Fills `out` with the legal moves and `next` with the position after each;
   returns the count. */
int bot_moves(const Position *p, Move *out, Position *next);

/* True if the side to move in `next` is checkmated. */
bool bot_mated(const Position *next);

/* The move with the highest score, ties broken by `req->seed`. n > 0. */
Move bot_pick(const BotRequest *req, const Move *moves, const int *scores, int n);

#endif
