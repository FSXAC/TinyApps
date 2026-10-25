/*
 * Queenie: loves her queen. She opens lines for it, brings it out early,
 * steers it toward your king, and gives check with it whenever she safely
 * can (though not round in circles: repeating a position puts her off).
 * Otherwise she trades sensibly: every move is scored by the static exchange
 * on its square, so she takes free material and won't leave the piece she
 * just moved hanging, but she looks no deeper than that.
 */
#include "bot.h"

#include <stdlib.h>

#define CHECK_BONUS 350 /* worth more than a pawn or two, never the queen itself */

static int king_distance(int a, int b) {
    int df = abs(FILE_OF(a) - FILE_OF(b)), dr = abs(RANK_OF(a) - RANK_OF(b));
    return df > dr ? df : dr;
}

/* Squares the queens of `color` reach. */
static int queen_reach(const Position *p, int color) {
    static const int DIR[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int n = 0;
    for (int sq = 0; sq < 64; sq++) {
        if (p->sq[sq] != PIECE(color, QUEEN)) continue;
        for (int d = 0; d < 8; d++)
            for (int f = FILE_OF(sq) + DIR[d][0], r = RANK_OF(sq) + DIR[d][1]; f >= 0 && f < 8 && r >= 0 && r < 8;
                 f += DIR[d][0], r += DIR[d][1]) {
                uint8_t pc = p->sq[SQ(f, r)];
                if (!pc || PCOLOR(pc) != color) n++;
                if (pc) break;
            }
    }
    return n;
}

static Move queenie_choose(const BotRequest *req) {
    const Position *p = &req->pos;
    Move moves[MAX_MOVES];
    Position next[MAX_MOVES];
    int scores[MAX_MOVES];
    int n = bot_moves(p, moves, next), reach = queen_reach(p, p->side);
    for (int i = 0; i < n; i++) {
        Move m = moves[i];
        if (bot_mated(&next[i])) {
            scores[i] = 1 << 20;
            continue;
        }
        int s = pos_see(p, m);
        if (PTYPE(p->sq[m.from]) == QUEEN) {
            if (pos_in_check(&next[i])) s += CHECK_BONUS;
            else s += 70 - 8 * king_distance(m.to, p->king[p->side ^ 1]); /* out, and toward the king */
        } else {
            s += 3 * (queen_reach(&next[i], p->side) - reach); /* open lines for her */
        }
        if (bot_seen(req, next[i].hash)) s -= CHECK_BONUS + 50; /* no endless checks */
        scores[i] = s;
    }
    return bot_pick(req, moves, scores, n);
}

const ChessBot bot_queenie = {"Queenie", "Brings her queen out to give check", queenie_choose, NULL, 0, 0};
