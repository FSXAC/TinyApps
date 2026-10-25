/*
 * Student: plays at your level. The app remembers how often you make each kind
 * of mistake (trainer.h) and passes the Student a slightly lower rate as
 * `req->slips`. Each move it rolls against those chances: usually it plays
 * one of the best moves; on a slip it plays a move that loses as much as that
 * grade does in the review (an Inaccuracy, a Mistake, a Blunder).
 *
 * Its slips are meant to be human ones: of the moves in the band, it plays the
 * one that looks about best at first sight (one move deep plus captures), so it
 * falls for tactics rather than giving pieces away for nothing, until the band
 * has nothing better to offer.
 */
#include "analysis.h"
#include "bot.h"

#define RANK_BUDGET 250000 /* nodes per move, about as long as Tiny's Normal */

static uint64_t mix(uint64_t x) { /* splitmix64 finalizer */
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

/* Loss bands (expected points) by grade: best, inaccuracy, mistake, blunder. */
static const double BAND_LO[4] = {0, ANALYSIS_LOSS_GOOD, ANALYSIS_LOSS_INACCURACY, ANALYSIS_LOSS_MISTAKE};
static const double BAND_HI[4] = {ANALYSIS_LOSS_EXCELLENT, ANALYSIS_LOSS_INACCURACY, ANALYSIS_LOSS_MISTAKE, 2};

static Move student_choose(const BotRequest *req) {
    RankedMove r[MAX_MOVES];
    int n = analysis_rank_moves(&req->pos, req->history, req->history_len, RANK_BUDGET, r, req->stop);
    if (n == 1) return r[0].move;
    double best = 0;
    for (int i = 0; i < n; i++)
        if (r[i].points > best) best = r[i].points;

    const BotSlips *s = &req->slips;
    double roll = (double)(mix(req->seed ^ 0x5374756465ull) >> 11) / 9007199254740992.0; /* [0, 1) */
    int grade = roll < s->blunder                                ? 3
                : roll < s->blunder + s->mistake                 ? 2
                : roll < s->blunder + s->mistake + s->inaccuracy ? 1
                                                                 : 0;
    /* Without a move in the band, slip a grade less (the best move is always in band 0). */
    for (; grade >= 0; grade--) {
        Move moves[MAX_MOVES];
        int scores[MAX_MOVES], k = 0;
        for (int i = 0; i < n; i++) {
            double loss = best - r[i].points;
            if (loss < BAND_LO[grade] || loss >= BAND_HI[grade]) continue;
            moves[k] = r[i].move;
            /* Among the best: any. A slip: what looks best at a glance, give or take a little. */
            int jitter = (int)(mix(req->seed ^ (uint64_t)(r[i].move.from << 8 | r[i].move.to)) % 3000);
            scores[k++] = grade ? (int)(r[i].glance * 100000) + jitter : 0;
        }
        if (k) return bot_pick(req, moves, scores, k);
    }
    return r[0].move;
}

const ChessBot bot_student = {"Student", "Plays at your level as you grow", student_choose, NULL, 0, 0};
