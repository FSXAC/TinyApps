/*
 * Mirror: answers each move with its reflection across the middle of the
 * board (e2e4 -> e7e5, Ng1f3 -> Ng8f6), so the position stays symmetric.
 * It doesn't remember moves: it plays whatever leaves the board most
 * symmetric, which is the reflection whenever that is legal. When it can't
 * copy (check, a blocked square, or as White on move one) it takes the
 * nearest thing, preferring captures, else a seeded random move.
 */
#include "bot.h"

/* Squares that match their reflection: same type, opposite color, or both empty. */
static int symmetry(const Position *p) {
    int n = 0;
    for (int sq = 0; sq < 64; sq++) {
        uint8_t a = p->sq[sq], b = p->sq[sq ^ 56];
        n += a ? b && PTYPE(a) == PTYPE(b) && PCOLOR(a) != PCOLOR(b) : !b;
    }
    return n;
}

static Move mirror_choose(const BotRequest *req) {
    Move moves[MAX_MOVES];
    Position next[MAX_MOVES];
    int scores[MAX_MOVES];
    int n = bot_moves(&req->pos, moves, next);
    for (int i = 0; i < n; i++) scores[i] = 10000 * symmetry(&next[i]) + bot_gain(&req->pos, moves[i]);
    return bot_pick(req, moves, scores, n);
}

const ChessBot bot_mirror = {"Mirror", "Copies every move you make", mirror_choose, NULL, 0, 0};
