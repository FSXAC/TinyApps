/*
 * Greedy: takes the most valuable thing it can, right now, and never asks
 * whether it will get it back. It mates when it can; with nothing to take it
 * moves at random. Good practice for spotting bait and punishing grabs.
 */
#include "bot.h"

static Move greedy_choose(const BotRequest *req) {
    Move moves[MAX_MOVES];
    Position next[MAX_MOVES];
    int scores[MAX_MOVES];
    int n = bot_moves(&req->pos, moves, next);
    for (int i = 0; i < n; i++) {
        /* Biggest prize first; between equal prizes, the cheapest piece takes. */
        scores[i] = 16 * bot_gain(&req->pos, moves[i]) - bot_value[PTYPE(req->pos.sq[moves[i].from])] / 100;
        if (bot_mated(&next[i])) scores[i] = 1 << 20;
    }
    return bot_pick(req, moves, scores, n);
}

const ChessBot bot_greedy = {"Greedy", "Grabs whatever it can, never looks back", greedy_choose, NULL, 0, 0};
