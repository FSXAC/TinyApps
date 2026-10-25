#include "bot.h"

const ChessBot *const chess_bots[] = {&bot_student, &bot_tiny, &bot_greedy, &bot_mirror, &bot_queenie};
const int chess_bot_count = (int)(sizeof chess_bots / sizeof chess_bots[0]);

const int bot_value[7] = {0, 100, 320, 330, 500, 900, 0};

bool bot_seen(const BotRequest *req, uint64_t hash) {
    for (int i = 0; i < req->history_len; i++)
        if (req->history[i] == hash) return true;
    return false;
}

int bot_gain(const Position *p, Move m) {
    int v = (m.flags & MOVE_EP) ? bot_value[PAWN] : bot_value[PTYPE(p->sq[m.to])];
    if (m.promo) v += bot_value[m.promo] - bot_value[PAWN];
    return v;
}

int bot_moves(const Position *p, Move *out, Position *next) {
    int n = pos_legal_moves(p, out);
    for (int i = 0; i < n; i++) {
        next[i] = *p;
        pos_make(&next[i], out[i]);
    }
    return n;
}

bool bot_mated(const Position *next) {
    Move replies[MAX_MOVES];
    return pos_in_check(next) && pos_legal_moves(next, replies) == 0;
}

static uint64_t mix(uint64_t x) { /* splitmix64 finalizer */
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

Move bot_pick(const BotRequest *req, const Move *moves, const int *scores, int n) {
    int best = 0;
    uint64_t best_tie = 0;
    for (int i = 0; i < n; i++) {
        uint64_t tie = mix(req->seed ^ ((uint64_t)moves[i].from << 8 | moves[i].to) * 0x9e3779b97f4a7c15ull);
        if (i == 0 || scores[i] > scores[best] || (scores[i] == scores[best] && tie > best_tie)) {
            best = i;
            best_tie = tie;
        }
    }
    return moves[best];
}
