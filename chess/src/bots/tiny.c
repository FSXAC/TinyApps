/*
 * Tiny: a simple, honest bot. Iterative-deepening alpha-beta with a capture
 * search, on material plus piece-square tables. A small per-game noise term
 * varies its play; mate scores make it finish games quickly rather than toy
 * with the opponent, and a mop-up term lets it convert won endgames.
 * A node budget (not a clock) bounds each move, so it is deterministic.
 *
 * Levels change the depth, the budget and the noise. Advanced also keeps a
 * transposition table and prunes with null moves and late-move reductions,
 * which lets it see several plies deeper in the same time.
 */
#include "bot.h"

#include <stdlib.h>

#define INF 32000
#define MATE 30000
#define MAX_PLY 64
#define TT_BITS 18 /* 2^18 entries of 16 bytes: 4 MB, allocated per move */

typedef struct {
    int max_depth;
    long budget; /* nodes per move */
    int noise;   /* evaluation noise, +/- centipawns */
    bool strong; /* transposition table, null-move pruning, late-move reductions */
} Level;

static const Level LEVELS[] = {
    {2, 60000, 40, false},   /* Reduced: two plies, loose judgement */
    {6, 1600000, 6, false},  /* Normal */
    {32, 3000000, 3, true},  /* Advanced */
};

static const BotLevel TINY_LEVELS[] = {
    {"Reduced", "Looks two moves ahead"},
    {"Normal", "Plays solid, honest chess"},
    {"Advanced", "Searches deep, punishes mistakes"},
};

static const int VALUE[7] = {0, 100, 320, 330, 500, 900, 0};

/* Piece-square tables from White's side, rank 8 first (as printed). */
static const int PST[7][64] = {
    {0},
    {0,  0,  0,  0,  0,  0,  0,  0,  50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10,
     5,  5,  10, 25, 25, 10, 5,  5,  0,  0,  0,  20, 20, 0,  0,  0,  5,  -5, -10, 0, 0,  -10, -5, 5,
     5,  10, 10, -20, -20, 10, 10, 5, 0, 0,  0,  0,  0,  0,  0,  0},
    {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,   0,   -20, -40,
     -30, 0,   10,  15,  15,  10,  0,   -30, -30, 5,   15,  20,  20,  15,  5,   -30,
     -30, 0,   15,  20,  20,  15,  0,   -30, -30, 5,   10,  15,  15,  10,  5,   -30,
     -40, -20, 0,   5,   5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50},
    {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,   0,   0,   -10,
     -10, 0,   5,   10,  10,  5,   0,   -10, -10, 5,   5,   10,  10,  5,   5,   -10,
     -10, 0,   10,  10,  10,  10,  0,   -10, -10, 10,  10,  10,  10,  10,  10,  -10,
     -10, 5,   0,   0,   0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20},
    {0,  0, 0, 0, 0, 0, 0, 0,  5,  10, 10, 10, 10, 10, 10, 5,  -5, 0, 0, 0, 0, 0, 0, -5,
     -5, 0, 0, 0, 0, 0, 0, -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0, 0, 0, 0, -5,
     -5, 0, 0, 0, 0, 0, 0, -5, 0,  0,  0,  5,  5,  0,  0,  0},
    {-20, -10, -10, -5, -5, -10, -10, -20, -10, 0,   0,   0,  0,  0,   0,   -10,
     -10, 0,   5,   5,  5,  5,   0,   -10, -5,  0,   5,   5,  5,  5,   0,   -5,
     0,   0,   5,   5,  5,  5,   0,   -5,  -10, 5,   5,   5,  5,  5,   0,   -10,
     -10, 0,   5,   0,  0,  0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20},
    /* King, middlegame: stay castled. */
    {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
     -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
     -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20},
};

/* King, endgame: head for the centre. */
static const int KING_END[64] = {
    -50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0,   0,   -10, -20, -30,
    -30, -10, 20,  30,  30,  20,  -10, -30, -30, -10, 30,  40,  40,  30,  -10, -30,
    -30, -10, 30,  40,  40,  30,  -10, -30, -30, -10, 20,  30,  30,  20,  -10, -30,
    -30, -30, 0,   0,   0,   0,   -30, -30, -50, -30, -30, -30, -30, -30, -30, -50,
};

enum { TT_EXACT, TT_LOWER, TT_UPPER };

typedef struct {
    uint64_t key;
    Move move;
    int16_t score;
    int8_t depth;
    uint8_t bound;
} TTEntry;

typedef struct {
    const BotRequest *req;
    const Level *lv;
    TTEntry *tt; /* NULL unless the level is strong */
    long nodes;
    bool aborted;
    uint64_t path[MAX_PLY + 8]; /* hashes from the root down, for repetition */
    Move killers[MAX_PLY][2];
} Search;

static int center_distance(int sq) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    return abs(2 * f - 7) / 2 + abs(2 * r - 7) / 2;
}

/* Static evaluation from the side to move's point of view. */
static int evaluate(const Search *s, const Position *p) {
    int score[2] = {0, 0}, material[2] = {0, 0}, bishops[2] = {0, 0}, phase = 0;
    for (int sq = 0; sq < 64; sq++) {
        uint8_t pc = p->sq[sq];
        if (!pc) continue;
        int c = PCOLOR(pc), t = PTYPE(pc);
        int idx = c == WHITE ? (7 - RANK_OF(sq)) * 8 + FILE_OF(sq) : sq;
        material[c] += VALUE[t];
        if (t != KING) score[c] += PST[t][idx];
        if (t == BISHOP) bishops[c]++;
        if (t >= KNIGHT && t <= QUEEN) phase += t == QUEEN ? 4 : t == ROOK ? 2 : 1;
    }
    if (phase > 24) phase = 24;
    for (int c = 0; c < 2; c++) {
        int k = p->king[c];
        int idx = c == WHITE ? (7 - RANK_OF(k)) * 8 + FILE_OF(k) : k;
        score[c] += material[c] + (PST[KING][idx] * phase + KING_END[idx] * (24 - phase)) / 24;
        if (bishops[c] >= 2) score[c] += 30;
    }
    /* Mop-up: when clearly winning late, drive the lone king to the edge and close in. */
    int diff = material[WHITE] - material[BLACK];
    if (phase <= 8 && abs(diff) >= 300) {
        int win = diff > 0 ? WHITE : BLACK;
        int lk = p->king[win ^ 1], wk = p->king[win];
        int dist = abs(FILE_OF(lk) - FILE_OF(wk)) + abs(RANK_OF(lk) - RANK_OF(wk));
        score[win] += 10 * center_distance(lk) + 4 * (14 - dist);
    }
    /* A few centipawns of per-game noise so games differ. */
    int noise = (int)((p->hash ^ s->req->seed) % (uint64_t)(2 * s->lv->noise + 1)) - s->lv->noise;
    int v = score[WHITE] - score[BLACK] + noise;
    return p->side == WHITE ? v : -v;
}

static int victim_value(const Position *p, Move m) {
    return (m.flags & MOVE_EP) ? VALUE[PAWN] : VALUE[PTYPE(p->sq[m.to])];
}

/* Higher first: the previous best move, captures by MVV-LVA, promotions, killers. */
static void order(const Search *s, const Position *p, Move *moves, int n, Move first, int ply) {
    int keys[MAX_MOVES];
    for (int i = 0; i < n; i++) {
        Move m = moves[i];
        int k = 0;
        if (move_eq(m, first)) k = 1000000;
        else if (m.flags & MOVE_CAPTURE) k = 100000 + 10 * victim_value(p, m) - VALUE[PTYPE(p->sq[m.from])] / 10;
        else if (m.promo == QUEEN) k = 90000;
        else if (ply < MAX_PLY && move_eq(m, s->killers[ply][0])) k = 80000;
        else if (ply < MAX_PLY && move_eq(m, s->killers[ply][1])) k = 79000;
        keys[i] = k;
    }
    for (int i = 1; i < n; i++) { /* insertion sort, descending */
        Move m = moves[i];
        int k = keys[i], j = i - 1;
        for (; j >= 0 && keys[j] < k; j--) {
            moves[j + 1] = moves[j];
            keys[j + 1] = keys[j];
        }
        moves[j + 1] = m;
        keys[j + 1] = k;
    }
}

static bool tick(Search *s) {
    if ((++s->nodes & 1023) == 0 && ((s->req->stop && *s->req->stop) || s->nodes > s->lv->budget))
        s->aborted = true;
    return !s->aborted;
}

static int quiesce(Search *s, const Position *p, int alpha, int beta, int ply) {
    if (!tick(s)) return 0;
    int stand = evaluate(s, p);
    if (stand >= beta || ply >= MAX_PLY) return stand;
    if (stand > alpha) alpha = stand;
    Move moves[MAX_MOVES];
    int n = pos_pseudo_moves(p, moves, true);
    order(s, p, moves, n, MOVE_NONE, MAX_PLY);
    for (int i = 0; i < n; i++) {
        Position next;
        if (!pos_make_legal(p, moves[i], &next)) continue;
        int v = -quiesce(s, &next, -beta, -alpha, ply + 1);
        if (s->aborted) return 0;
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

static bool repeated(const Search *s, const Position *p, int ply) {
    for (int i = ply - 2; i >= 0 && i >= ply - p->halfmove; i -= 2)
        if (s->path[i] == p->hash) return true;
    return bot_seen(s->req, p->hash);
}

static bool has_pieces(const Position *p) {
    for (int sq = 0; sq < 64; sq++) {
        uint8_t pc = p->sq[sq];
        if (pc && PCOLOR(pc) == p->side && PTYPE(pc) != PAWN && PTYPE(pc) != KING) return true;
    }
    return false;
}

/* Mate scores are stored relative to the node, so they stay right at another ply. */
static int to_tt(int v, int ply) { return v > MATE - MAX_PLY ? v + ply : v < -MATE + MAX_PLY ? v - ply : v; }
static int from_tt(int v, int ply) { return v > MATE - MAX_PLY ? v - ply : v < -MATE + MAX_PLY ? v + ply : v; }

static int search(Search *s, const Position *p, int depth, int alpha, int beta, int ply, Move first,
                  Move *best_out, bool null_ok) {
    if (!tick(s)) return 0;
    s->path[ply] = p->hash;
    if (ply > 0 && (p->halfmove >= 100 || repeated(s, p, ply))) return 0;
    bool check = pos_in_check(p);
    if (check && ply < MAX_PLY / 2) depth++;
    if (depth <= 0 || ply >= MAX_PLY) return quiesce(s, p, alpha, beta, ply);

    TTEntry *e = s->tt ? &s->tt[p->hash & ((1u << TT_BITS) - 1)] : NULL;
    if (e && e->key == p->hash) {
        if (move_is_none(first)) first = e->move;
        int v = from_tt(e->score, ply);
        if (ply > 0 && e->depth >= depth &&
            (e->bound == TT_EXACT || (e->bound == TT_LOWER && v >= beta) || (e->bound == TT_UPPER && v <= alpha)))
            return v;
    }

    /* Null move: if passing still holds beta, a real move will too. */
    if (s->lv->strong && null_ok && ply > 0 && !check && depth >= 3 && beta < MATE - MAX_PLY && has_pieces(p) &&
        evaluate(s, p) >= beta) {
        Position n = *p;
        n.side ^= 1;
        n.ep = -1;
        n.halfmove = 0; /* no repetitions across a null move */
        n.hash = p->hash ^ 0x9e3779b97f4a7c15ull;
        int v = -search(s, &n, depth - 3 - depth / 4, -beta, -beta + 1, ply + 1, MOVE_NONE, NULL, false);
        if (s->aborted) return 0;
        if (v >= beta) return v > MATE - MAX_PLY ? beta : v;
    }

    Move moves[MAX_MOVES], best_move = MOVE_NONE;
    int n = pos_pseudo_moves(p, moves, false), legal = 0, best = -INF, alpha0 = alpha;
    order(s, p, moves, n, first, ply);
    for (int i = 0; i < n; i++) {
        Position next;
        if (!pos_make_legal(p, moves[i], &next)) continue;
        legal++;
        int v;
        bool quiet = !(moves[i].flags & MOVE_CAPTURE) && !moves[i].promo;
        if (s->lv->strong && legal > 4 && depth >= 3 && !check && quiet && !pos_in_check(&next)) {
            /* Late quiet moves are rarely best: look at them shallower first. */
            v = -search(s, &next, depth - 2, -alpha - 1, -alpha, ply + 1, MOVE_NONE, NULL, true);
            if (!s->aborted && v > alpha) v = -search(s, &next, depth - 1, -beta, -alpha, ply + 1, MOVE_NONE, NULL, true);
        } else {
            v = -search(s, &next, depth - 1, -beta, -alpha, ply + 1, MOVE_NONE, NULL, true);
        }
        if (s->aborted) return 0;
        if (v > best) {
            best = v;
            best_move = moves[i];
        }
        if (v > alpha) alpha = v;
        if (alpha >= beta) {
            if (quiet && ply < MAX_PLY) {
                s->killers[ply][1] = s->killers[ply][0];
                s->killers[ply][0] = moves[i];
            }
            break;
        }
    }
    if (!legal) return check ? -MATE + ply : 0;
    if (best_out) *best_out = best_move;
    if (e) *e = (TTEntry){p->hash, best_move, (int16_t)to_tt(best, ply), (int8_t)(depth > 127 ? 127 : depth),
                          (uint8_t)(best <= alpha0 ? TT_UPPER : best >= beta ? TT_LOWER : TT_EXACT)};
    return best;
}

static Move tiny_choose(const BotRequest *req) {
    Search s = {0};
    s.req = req;
    int lv = req->level < 0 ? 0 : req->level >= 3 ? 2 : req->level;
    s.lv = &LEVELS[lv];
    Move legal[MAX_MOVES];
    int n = pos_legal_moves(&req->pos, legal);
    if (n == 0) return MOVE_NONE;
    Move best = legal[req->seed % (uint64_t)n];
    if (n == 1) return best;
    if (s.lv->strong) s.tt = calloc((size_t)1 << TT_BITS, sizeof(TTEntry)); /* runs without it on failure */
    for (int depth = 1; depth <= s.lv->max_depth; depth++) {
        Move found = MOVE_NONE;
        int v = search(&s, &req->pos, depth, -INF, INF, 0, best, &found, false);
        if (s.aborted) break;
        if (!move_is_none(found)) best = found;
        if (v > MATE - MAX_PLY) break; /* found a mate: play it */
    }
    free(s.tt);
    return best;
}

const ChessBot bot_tiny = {
    "Tiny", "A small, honest engine", tiny_choose, TINY_LEVELS, 3, 1,
};
