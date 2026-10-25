/*
 * Analysis engine for game review: iterative-deepening PVS alpha-beta with a
 * transposition table, null-move pruning, late-move reductions and a capture
 * search that also resolves checks (so short mates are found reliably), on a
 * tapered PeSTO evaluation plus pawn structure and a few piece terms.
 *
 * Everything mutable lives in a per-call Search (heap allocated, TT included),
 * so calls are thread safe. A node budget, not a clock, bounds the work, so
 * results are deterministic.
 */
#include "analysis.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INF 32000
#define MATE 30000
#define MAX_PLY 64
#define MAX_DEPTH 40
#define IS_MATE(v) ((v) > MATE - 2 * MAX_PLY || (v) < -(MATE - 2 * MAX_PLY))
#define TT_BITS 18 /* 256k entries x 16 bytes = 4 MB per call */

/* ------------------------------------------------------------------ */
/* Evaluation: PeSTO piece values and tables (from White's side, rank 8 first),
   tapered between middlegame and endgame by the remaining material. */

static const int MG_VAL[7] = {0, 82, 337, 365, 477, 1025, 0};
static const int EG_VAL[7] = {0, 94, 281, 297, 512, 936, 0};
static const int PHASE_INC[7] = {0, 0, 1, 1, 2, 4, 0};
/* Classic values for SEE, draw detection and move ordering. */
static const int VALUE[7] = {0, 100, 320, 330, 500, 900, 20000};

static const int MG_PST[7][64] = {
    {0},
    {0,   0,   0,   0,   0,   0,  0,   0,   98,  134, 61,  95,  68,  126, 34, -11,
     -6,  7,   26,  31,  65,  56, 25,  -20, -14, 13,  6,   21,  23,  12,  17, -23,
     -27, -2,  -5,  12,  17,  6,  10,  -25, -26, -4,  -4,  -10, 3,   3,   33, -12,
     -35, -1,  -20, -23, -15, 24, 38,  -22, 0,   0,   0,   0,   0,   0,   0,  0},
    {-167, -89, -34, -49, 61,  -97, -15, -107, -73, -41, 72,  36,  23,  62,  7,   -17,
     -47,  60,  37,  65,  84,  129, 73,  44,   -9,  17,  19,  53,  37,  69,  18,  22,
     -13,  4,   16,  13,  28,  19,  21,  -8,   -23, -9,  12,  10,  19,  17,  25,  -16,
     -29,  -53, -12, -3,  -1,  18,  -14, -19,  -105, -21, -58, -33, -17, -28, -19, -23},
    {-29, 4,  -82, -37, -25, -42, 7,  -8,  -26, 16, -18, -13, 30,  59,  18,  -47,
     -16, 37, 43,  40,  35,  50,  37, -2,  -4,  5,  19,  50,  37,  37,  7,   -2,
     -6,  13, 13,  26,  34,  12,  10, 4,   0,   15, 15,  15,  14,  27,  18,  10,
     4,   15, 16,  0,   7,   21,  33, 1,   -33, -3, -14, -21, -13, -12, -39, -21},
    {32,  42,  32,  51,  63, 9,  31,  43,  27,  32,  58,  62,  80, 67, 26,  44,
     -5,  19,  26,  36,  17, 45, 61,  16,  -24, -11, 7,   26,  24, 35, -8,  -20,
     -36, -26, -12, -1,  9,  -7, 6,   -23, -45, -25, -16, -17, 3,  0,  -5,  -33,
     -44, -16, -20, -9,  -1, 11, -6,  -71, -19, -13, 1,   17,  16, 7,  -37, -26},
    {-28, 0,   29,  12,  59,  44,  43,  45,  -24, -39, -5,  1,   -16, 57,  28,  54,
     -13, -17, 7,   8,   29,  56,  47,  57,  -27, -27, -16, -16, -1,  17,  -2,  1,
     -9,  -26, -9,  -10, -2,  -4,  3,   -3,  -14, 2,   -11, -2,  -5,  2,   14,  5,
     -35, -8,  11,  2,   8,   15,  -3,  1,   -1,  -18, -9,  10,  -15, -25, -31, -50},
    {-65, 23,  16,  -15, -56, -34, 2,   13,  29,  -1,  -20, -7,  -8,  -4,  -38, -29,
     -9,  24,  2,   -16, -20, 6,   22,  -22, -17, -20, -12, -27, -30, -25, -14, -36,
     -49, -1,  -27, -39, -46, -44, -33, -51, -14, -14, -22, -46, -44, -30, -15, -27,
     1,   7,   -8,  -64, -43, -16, 9,   8,   -15, 36,  12,  -54, 8,   -28, 24,  14},
};

static const int EG_PST[7][64] = {
    {0},
    {0,  0,   0,   0,   0,   0,   0,   0,   178, 173, 158, 134, 147, 132, 165, 187,
     94, 100, 85,  67,  56,  53,  82,  84,  32,  24,  13,  5,   -2,  4,   17,  17,
     13, 9,   -3,  -7,  -7,  -8,  3,   -1,  4,   7,   -6,  1,   0,   -5,  -1,  -8,
     13, 8,   8,   10,  13,  0,   2,   -7,  0,   0,   0,   0,   0,   0,   0,   0},
    {-58, -38, -13, -28, -31, -27, -63, -99, -25, -8,  -25, -2,  -9,  -25, -24, -52,
     -24, -20, 10,  9,   -1,  -9,  -19, -41, -17, 3,   22,  22,  22,  11,  8,   -18,
     -18, -6,  16,  25,  16,  17,  4,   -18, -23, -3,  -1,  15,  10,  -3,  -20, -22,
     -42, -20, -10, -5,  -2,  -20, -23, -44, -29, -51, -23, -15, -22, -18, -50, -64},
    {-14, -21, -11, -8,  -7, -9,  -17, -24, -8,  -4,  7,   -12, -3, -13, -4,  -14,
     2,   -8,  0,   -1,  -2, 6,   0,   4,   -3,  9,   12,  9,   14, 10,  3,   2,
     -6,  3,   13,  19,  7,  10,  -3,  -9,  -12, -3,  8,   10,  13, 3,   -7,  -15,
     -14, -18, -7,  -1,  4,  -9,  -15, -27, -23, -9,  -23, -5,  -9, -16, -5,  -17},
    {13, 10, 18, 15, 12, 12,  8,   5,   11, 13, 13, 11, -3, 3,   8,   3,
     7,  7,  7,  5,  4,  -3,  -5,  -3,  4,  3,  13, 1,  2,  1,   -1,  2,
     3,  5,  8,  4,  -5, -6,  -8,  -11, -4, 0,  -5, -1, -7, -12, -8,  -16,
     -6, -6, 0,  2,  -9, -9,  -11, -3,  -9, 2,  3,  -1, -5, -13, 4,   -20},
    {-9,  22,  22,  27,  27,  19,  10,  20,  -17, 20,  32,  41,  58,  25,  30,  0,
     -20, 6,   9,   49,  47,  35,  19,  9,   3,   22,  24,  45,  57,  40,  57,  36,
     -18, 28,  19,  47,  31,  34,  39,  23,  -16, -27, 15,  6,   9,   17,  10,  5,
     -22, -23, -30, -16, -16, -23, -36, -32, -33, -28, -22, -43, -5,  -32, -20, -41},
    {-74, -35, -18, -18, -11, 15,  4,   -17, -12, 17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,  -8,  22,  24,  27,  26,  33,  26,  3,
     -18, -4,  21,  24,  27,  23,  9,   -11, -19, -3,  11,  21,  23,  16,  7,   -9,
     -27, -11, 4,   13,  14,  4,   -5,  -17, -53, -34, -21, -11, -28, -14, -24, -43},
};

/* Passed pawn bonus by relative rank (0 = own back rank). */
static const int PASSED_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
static const int PASSED_EG[8] = {0, 10, 15, 25, 45, 75, 120, 0};

static int iabs(int x) { return x < 0 ? -x : x; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int edge_distance(int sq) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    return imin(imin(f, 7 - f), imin(r, 7 - r));
}

/* Static evaluation in centipawns from the side to move's point of view. */
static int evaluate(const Position *p) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    int npm[2] = {0, 0}, pawns[2] = {0, 0}, bishops[2] = {0, 0}, knights[2] = {0, 0}, minors_majors[2] = {0, 0};
    /* Pawn counts per file, and the most / least advanced pawn rank per file and color. */
    int pfile[2][8] = {{0}};
    for (int sq = 0; sq < 64; sq++) {
        uint8_t pc = p->sq[sq];
        if (!pc) continue;
        int c = PCOLOR(pc), t = PTYPE(pc);
        int idx = c == WHITE ? sq ^ 56 : sq;
        mg[c] += MG_VAL[t] + MG_PST[t][idx];
        eg[c] += EG_VAL[t] + EG_PST[t][idx];
        phase += PHASE_INC[t];
        if (t == PAWN) {
            pawns[c]++;
            pfile[c][FILE_OF(sq)]++;
        } else if (t != KING) {
            npm[c] += VALUE[t];
            minors_majors[c]++;
            if (t == BISHOP) bishops[c]++;
            if (t == KNIGHT) knights[c]++;
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        uint8_t pc = p->sq[sq];
        if (!pc) continue;
        int c = PCOLOR(pc), t = PTYPE(pc), f = FILE_OF(sq), r = RANK_OF(sq);
        if (t == PAWN) {
            int rel = c == WHITE ? r : 7 - r, dir = c == WHITE ? 1 : -1;
            bool isolated = (f == 0 || !pfile[c][f - 1]) && (f == 7 || !pfile[c][f + 1]);
            if (isolated) mg[c] -= 10, eg[c] -= 15;
            /* Passed: no enemy pawn ahead on this or an adjacent file. */
            bool passed = true;
            for (int rr = r + dir; passed && rr >= 0 && rr < 8; rr += dir)
                for (int ff = imax(0, f - 1); ff <= imin(7, f + 1); ff++)
                    if (p->sq[SQ(ff, rr)] == PIECE(c ^ 1, PAWN)) passed = false;
            if (passed) {
                mg[c] += PASSED_MG[rel];
                eg[c] += PASSED_EG[rel];
                /* Blocked passers are worth less. */
                int ahead = sq + 8 * dir;
                if (p->sq[ahead]) eg[c] -= PASSED_EG[rel] / 3;
            }
        } else if (t == ROOK) {
            if (!pfile[c][f]) {
                int b = pfile[c ^ 1][f] ? 10 : 20;
                mg[c] += b;
                eg[c] += b / 2;
            }
        }
    }
    for (int c = 0; c < 2; c++) {
        for (int f = 0; f < 8; f++)
            if (pfile[c][f] > 1) mg[c] -= 10 * (pfile[c][f] - 1), eg[c] -= 20 * (pfile[c][f] - 1);
        if (bishops[c] >= 2) mg[c] += 30, eg[c] += 50;
    }
    if (phase > 24) phase = 24;
    int v = ((mg[WHITE] - mg[BLACK]) * phase + (eg[WHITE] - eg[BLACK]) * (24 - phase)) / 24;

    /* Endgame knowledge: a pawnless side needs more than a minor piece's edge to
       win, and two knights cannot force mate. */
    int strong = v >= 0 ? WHITE : BLACK, weak = strong ^ 1;
    if (!pawns[strong]) {
        bool two_knights = knights[strong] == 2 && minors_majors[strong] == 2 && !minors_majors[weak];
        if (npm[strong] - npm[weak] < 400 || two_knights) v /= 8;
    }
    /* Mop-up: with a lone enemy king, drive it to the edge and close in, so the
       search can see the mate. */
    if (minors_majors[weak] == 0 && pawns[weak] == 0 && npm[strong] >= 500) {
        int lk = p->king[weak], wk = p->king[strong];
        int dist = iabs(FILE_OF(lk) - FILE_OF(wk)) + iabs(RANK_OF(lk) - RANK_OF(wk));
        int bonus = 300 + 20 * (3 - edge_distance(lk)) + 6 * (14 - dist);
        v += strong == WHITE ? bonus : -bonus;
    }
    v += p->side == WHITE ? 10 : -10; /* tempo */
    return p->side == WHITE ? v : -v;
}

/* ------------------------------------------------------------------ */
/* Search. */

enum { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };

typedef struct {
    uint64_t key;
    int16_t score;
    int8_t depth;
    uint8_t flag;
    Move move;
} TTEntry;

typedef struct {
    TTEntry *tt; /* NULL when allocation failed: search without it */
    size_t tt_mask;
    const uint64_t *hist;
    int hist_len;
    const volatile int *stop;
    long nodes, budget;
    bool aborted;
    Move root_best;
    uint64_t path[MAX_PLY + 2]; /* hashes from the root down, for repetition */
    Move killers[MAX_PLY + 2][2];
    int history[2][64][64];
} Search;

/* Work bound per position. ~150k nodes is about 100 ms on an M-series Mac. */
#define NODE_BUDGET 150000

static bool tick(Search *s) {
    if ((++s->nodes & 1023) == 0 && ((s->stop && *s->stop) || s->nodes > s->budget)) s->aborted = true;
    return !s->aborted;
}

/* Mate scores are stored relative to the node, not the root. */
static int to_tt(int v, int ply) { return v > MATE - 2 * MAX_PLY ? v + ply : v < -(MATE - 2 * MAX_PLY) ? v - ply : v; }
static int from_tt(int v, int ply) { return v > MATE - 2 * MAX_PLY ? v - ply : v < -(MATE - 2 * MAX_PLY) ? v + ply : v; }

static TTEntry *tt_probe(Search *s, uint64_t key) {
    if (!s->tt) return NULL;
    TTEntry *e = &s->tt[key & s->tt_mask];
    return e->key == key ? e : NULL;
}

static void tt_store(Search *s, uint64_t key, int depth, int flag, int score, int ply, Move m) {
    if (!s->tt) return;
    TTEntry *e = &s->tt[key & s->tt_mask];
    if (e->key == key && e->depth > depth && flag != TT_EXACT) return; /* keep deeper info */
    e->key = key;
    e->depth = (int8_t)imin(depth, 127);
    e->flag = (uint8_t)flag;
    e->score = (int16_t)to_tt(score, ply);
    e->move = m;
}

static bool is_draw(const Search *s, const Position *p, int ply) {
    if (p->halfmove >= 100 || pos_insufficient_material(p)) return true;
    /* Repetition within the reversible stretch: the search path, then the game. */
    int i = ply - 2;
    for (; i >= 0 && i >= ply - p->halfmove; i -= 2)
        if (s->path[i] == p->hash) return true;
    int back = p->halfmove - ply; /* game positions still in the reversible stretch */
    for (int k = 1; k <= back && k <= s->hist_len; k++)
        if (s->hist[s->hist_len - k] == p->hash) return true;
    return false;
}

static bool has_pieces(const Position *p, int c) {
    for (int sq = 0; sq < 64; sq++) {
        uint8_t pc = p->sq[sq];
        if (pc && PCOLOR(pc) == c && PTYPE(pc) != PAWN && PTYPE(pc) != KING) return true;
    }
    return false;
}

static int victim(const Position *p, Move m) {
    return (m.flags & MOVE_EP) ? PAWN : PTYPE(p->sq[m.to]);
}

/* Ordering keys: TT move, good captures (MVV-LVA), promotions, killers, history, bad captures. */
static void score_moves(const Search *s, const Position *p, const Move *moves, int *keys, int n, Move tt_move,
                        int ply) {
    for (int i = 0; i < n; i++) {
        Move m = moves[i];
        int k;
        if (move_eq(m, tt_move)) k = 1 << 30;
        else if (m.flags & MOVE_CAPTURE) {
            k = 10 * VALUE[victim(p, m)] - VALUE[PTYPE(p->sq[m.from])] / 100;
            k += pos_see(p, m) >= 0 ? 1 << 26 : -(1 << 20);
        } else if (m.promo == QUEEN) k = 1 << 25;
        else if (ply <= MAX_PLY && move_eq(m, s->killers[ply][0])) k = 1 << 24;
        else if (ply <= MAX_PLY && move_eq(m, s->killers[ply][1])) k = (1 << 24) - 1;
        else k = s->history[p->side][m.from][m.to];
        keys[i] = k;
    }
}

/* Selection step: swaps the best remaining move into slot i. */
static Move pick(Move *moves, int *keys, int n, int i) {
    int b = i;
    for (int j = i + 1; j < n; j++)
        if (keys[j] > keys[b]) b = j;
    Move m = moves[b];
    int k = keys[b];
    moves[b] = moves[i];
    keys[b] = keys[i];
    moves[i] = m;
    keys[i] = k;
    return m;
}

/* Capture search. In check it tries every evasion, so mates at the horizon count. */
static int quiesce(Search *s, const Position *p, int alpha, int beta, int ply) {
    if (!tick(s)) return 0;
    if (ply >= MAX_PLY) return evaluate(p);
    if (pos_insufficient_material(p)) return 0;
    bool check = pos_in_check(p);
    int best, stand = 0;
    Move moves[MAX_MOVES];
    int keys[MAX_MOVES];
    int n;
    if (check) {
        best = -MATE + ply;
        n = pos_pseudo_moves(p, moves, false);
    } else {
        stand = evaluate(p);
        if (stand >= beta) return stand;
        if (stand > alpha) alpha = stand;
        best = stand;
        n = pos_pseudo_moves(p, moves, true);
    }
    score_moves(s, p, moves, keys, n, MOVE_NONE, MAX_PLY + 1);
    for (int i = 0; i < n; i++) {
        Move m = pick(moves, keys, n, i);
        if (!check && !m.promo) {
            /* Delta and SEE pruning: skip captures that can't raise alpha or lose material. */
            if (stand + VALUE[victim(p, m)] + 200 < alpha) continue;
            if (keys[i] < 0) continue;
        }
        Position next;
        if (!pos_make_legal(p, m, &next)) continue;
        int v = -quiesce(s, &next, -beta, -alpha, ply + 1);
        if (s->aborted) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

static int search(Search *s, const Position *p, int depth, int alpha, int beta, int ply, bool null_ok) {
    bool root = ply == 0, pv = beta - alpha > 1;
    if (!root) {
        if (is_draw(s, p, ply)) return 0;
        /* Mate distance pruning. */
        alpha = imax(alpha, -MATE + ply);
        beta = imin(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }
    s->path[ply] = p->hash;
    bool check = pos_in_check(p);
    if (check) depth++;
    if (depth <= 0 || ply >= MAX_PLY) return quiesce(s, p, alpha, beta, ply);
    if (!tick(s)) return 0;

    Move tt_move = MOVE_NONE;
    TTEntry *e = tt_probe(s, p->hash);
    if (e) {
        tt_move = e->move;
        int v = from_tt(e->score, ply);
        if (!pv && e->depth >= depth &&
            (e->flag == TT_EXACT || (e->flag == TT_LOWER && v >= beta) || (e->flag == TT_UPPER && v <= alpha)))
            return v;
    }

    int static_eval = check ? -INF : evaluate(p);
    if (!pv && !check && !IS_MATE(beta)) {
        /* Reverse futility: far above beta near the leaves. */
        if (depth <= 3 && static_eval - 120 * depth >= beta) return static_eval;
        /* Null move, but not without pieces (zugzwang). */
        if (null_ok && depth >= 3 && static_eval >= beta && has_pieces(p, p->side)) {
            Position n = *p;
            n.side ^= 1;
            n.ep = -1;
            n.halfmove = 0; /* no repetitions across a null move */
            n.hash = p->hash ^ 0x5bd1e9955bd1e995ull;
            int v = -search(s, &n, depth - 1 - (2 + depth / 4), -beta, -beta + 1, ply + 1, false);
            if (s->aborted) return 0;
            if (v >= beta) return IS_MATE(v) ? beta : v;
        }
    }

    Move moves[MAX_MOVES];
    int keys[MAX_MOVES];
    int n = pos_pseudo_moves(p, moves, false);
    score_moves(s, p, moves, keys, n, tt_move, ply);
    int legal = 0, best = -INF, orig_alpha = alpha;
    Move best_move = MOVE_NONE;
    for (int i = 0; i < n; i++) {
        Move m = pick(moves, keys, n, i);
        Position next;
        if (!pos_make_legal(p, m, &next)) continue;
        legal++;
        bool quiet = !(m.flags & MOVE_CAPTURE) && !m.promo;
        bool gives_check = pos_in_check(&next);
        /* Futility: quiet moves can't lift a hopeless node near the leaves. */
        if (!pv && !check && !gives_check && quiet && legal > 1 && depth <= 2 && !IS_MATE(alpha) &&
            static_eval + 100 + 120 * depth <= alpha)
            continue;
        int v;
        if (legal == 1) {
            v = -search(s, &next, depth - 1, -beta, -alpha, ply + 1, true);
        } else {
            int r = 0;
            if (depth >= 3 && quiet && !check && !gives_check && legal > 3) {
                r = 1 + (legal > 10) + (depth >= 7) - pv;
                r = imax(0, imin(r, depth - 2));
            }
            v = -search(s, &next, depth - 1 - r, -alpha - 1, -alpha, ply + 1, true);
            if (v > alpha && r) v = -search(s, &next, depth - 1, -alpha - 1, -alpha, ply + 1, true);
            if (v > alpha && v < beta) v = -search(s, &next, depth - 1, -beta, -alpha, ply + 1, true);
        }
        if (s->aborted) return 0;
        if (v > best) {
            best = v;
            best_move = m;
            if (root) s->root_best = m;
        }
        if (v > alpha) alpha = v;
        if (alpha >= beta) {
            if (quiet) {
                if (!move_eq(m, s->killers[ply][0])) {
                    s->killers[ply][1] = s->killers[ply][0];
                    s->killers[ply][0] = m;
                }
                int *h = &s->history[p->side][m.from][m.to];
                *h += depth * depth;
                if (*h > (1 << 22)) /* keep below the killer keys */
                    for (int c = 0; c < 2; c++)
                        for (int a = 0; a < 64 * 64; a++) s->history[c][a / 64][a % 64] /= 2;
            }
            break;
        }
    }
    if (!legal) return check ? -MATE + ply : 0;
    if (best == -INF) best = alpha; /* every move was pruned by futility */
    tt_store(s, p->hash, depth, best >= beta ? TT_LOWER : best > orig_alpha ? TT_EXACT : TT_UPPER, best, ply,
             best_move);
    return best;
}

static int count_hash(const uint64_t *h, int n, uint64_t key) {
    int c = 0;
    for (int i = 0; i < n; i++) c += h[i] == key;
    return c;
}

PositionEval analysis_evaluate(const Position *p, const uint64_t *history, int history_len,
                               const volatile int *stop) {
    PositionEval r = {0, 0, MOVE_NONE};
    Move legal[MAX_MOVES];
    int n = pos_legal_moves(p, legal);
    if (n == 0) {
        if (pos_in_check(p)) r.score = p->side == WHITE ? -EVAL_MATE_SCORE : EVAL_MATE_SCORE;
        return r;
    }
    if (!history) history_len = 0;
    /* Drawn by rule: the game is over, so no best move. */
    if (pos_insufficient_material(p) || p->halfmove >= 100 || count_hash(history, history_len, p->hash) >= 2)
        return r;

    Search *s = calloc(1, sizeof *s);
    if (!s) {
        r.best = legal[0];
        return r;
    }
    s->tt = calloc((size_t)1 << TT_BITS, sizeof(TTEntry));
    s->tt_mask = s->tt ? ((size_t)1 << TT_BITS) - 1 : 0;
    s->hist = history;
    s->hist_len = history_len;
    s->stop = stop;
    s->budget = NODE_BUDGET;

    Move best = legal[0];
    int score = 0;
    bool done = false;
    for (int depth = 1; depth <= MAX_DEPTH; depth++) {
        s->root_best = MOVE_NONE;
        int v = search(s, p, depth, -INF, INF, 0, false);
        if (s->aborted) break;
        done = true;
        score = v;
        if (!move_is_none(s->root_best)) best = s->root_best;
        /* A mate found at this depth is the shortest one once the depth covers it. */
        if (IS_MATE(v) && depth >= MATE - iabs(v) + 2) break;
    }
    if (!done) { /* stopped before depth 1 finished: fall back to a static guess */
        s->aborted = false;
        s->stop = NULL;
        s->budget = s->nodes + 20000;
        score = quiesce(s, p, -INF, INF, 0);
        if (s->aborted) score = evaluate(p);
    }
    free(s->tt);
    free(s);

    r.best = best;
    if (IS_MATE(score)) {
        int plies = MATE - iabs(score), moves = (plies + 1) / 2;
        int winner = score > 0 ? p->side : p->side ^ 1;
        r.mate = winner == WHITE ? moves : -moves;
        r.score = winner == WHITE ? EVAL_MATE_SCORE : -EVAL_MATE_SCORE;
    } else {
        int w = p->side == WHITE ? score : -score;
        r.score = imax(-(EVAL_MATE_SCORE - 1), imin(EVAL_MATE_SCORE - 1, w));
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Classification and accuracy. */

/* Expected points (0..1) for `color`, from a White-relative evaluation; the
   logistic curve lichess fits to game outcomes. */
static double cp_points(double cp) { return 1.0 / (1.0 + exp(-0.00368208 * cp)); }

static double expected_points(const PositionEval *e, int color) {
    double cp = color == WHITE ? e->score : -e->score;
    if (e->mate) cp = (e->mate > 0) == (color == WHITE) ? 1e5 : -1e5;
    return cp_points(cp);
}

double analysis_points(const PositionEval *e, int color) { return expected_points(e, color); }

/* True if `e` says `color` can force mate (or already has). */
static bool has_mate(const PositionEval *e, int color) {
    int sign = color == WHITE ? 1 : -1;
    if (e->mate) return e->mate * sign > 0;
    return e->score * sign >= EVAL_MATE_SCORE;
}

static bool is_forcing(const Position *p, Move m) {
    if (move_is_none(m)) return false;
    if ((m.flags & MOVE_CAPTURE) || m.promo) return true;
    Position next;
    return pos_make_legal(p, m, &next) && pos_in_check(&next);
}

MoveClass analysis_classify(const Position *before, Move played, const PositionEval *before_eval,
                            const PositionEval *after_eval) {
    Move legal[MAX_MOVES];
    int n = pos_legal_moves(before, legal), mover = before->side;
    if (n == 0 || !before_eval || !after_eval) return CLASS_NONE;
    if (n == 1) return CLASS_FORCED;

    Position after;
    if (!pos_make_legal(before, played, &after)) return CLASS_NONE;
    Move reply[MAX_MOVES];
    bool mates = pos_legal_moves(&after, reply) == 0 && pos_in_check(&after);

    double wb = expected_points(before_eval, mover), wa = expected_points(after_eval, mover);
    double loss = wb - wa;
    if (loss < 0) loss = 0;

    bool best = mates || (!move_is_none(before_eval->best) && move_eq(played, before_eval->best));
    if (best || loss < ANALYSIS_LOSS_EXCELLENT) {
        /* Brilliant: a sound piece sacrifice in a game that isn't already won,
           that leaves the mover at least equal. */
        int pt = PTYPE(before->sq[played.from]);
        if (!mates && !played.promo && pt != PAWN && pt != KING && wb < 0.90 && wa >= 0.50 &&
            pos_see(before, played) <= -200)
            return CLASS_BRILLIANT;
        return best ? CLASS_BEST : CLASS_EXCELLENT;
    }
    if (loss >= ANALYSIS_LOSS_MISTAKE) return CLASS_BLUNDER;

    /* Miss: a forced mate, or a clearly winning tactic, was on the board and is gone. */
    bool missed_mate = has_mate(before_eval, mover) && !has_mate(after_eval, mover);
    bool missed_tactic = wb >= 0.75 && wa < 0.65 && is_forcing(before, before_eval->best);
    if ((missed_mate || missed_tactic) && loss >= ANALYSIS_LOSS_GOOD) return CLASS_MISS;

    if (loss < ANALYSIS_LOSS_GOOD) return CLASS_GOOD;
    if (loss < ANALYSIS_LOSS_INACCURACY) return CLASS_INACCURACY;
    return CLASS_MISTAKE;
}

double analysis_accuracy_from(const PositionEval *evals, int ply, int color, int first_side) {
    double sum = 0, inv = 0;
    int n = 0;
    for (int i = 0; i < ply; i++) {
        int mover = (first_side + i) & 1;
        if (mover != color) continue;
        double loss = 100.0 * (expected_points(&evals[i], mover) - expected_points(&evals[i + 1], mover));
        if (loss < 0) loss = 0;
        /* Lichess's per-move accuracy curve, shifted so a lossless move scores exactly 100. */
        double acc = 103.1668 * exp(-0.04354 * loss) - 3.1668;
        acc = acc < 0 ? 0 : acc > 100 ? 100 : acc;
        sum += acc;
        inv += 1.0 / (acc < 1 ? 1 : acc);
        n++;
    }
    if (!n) return 100;
    /* Blend the mean with the harmonic mean, so a few blunders weigh more than
       many routine moves can hide. */
    double a = (sum / n + n / inv) / 2;
    return a < 0 ? 0 : a > 100 ? 100 : a;
}

double analysis_accuracy(const PositionEval *evals, int ply, int color) {
    return analysis_accuracy_from(evals, ply, color, WHITE);
}

const char *analysis_class_name(MoveClass c) {
    static const char *names[CLASS_COUNT] = {"",           "Brilliant", "Best", "Excellent", "Good",  "Book",
                                             "Inaccuracy", "Mistake",   "Miss", "Blunder",   "Forced"};
    return c >= 0 && c < CLASS_COUNT ? names[c] : "";
}

const char *analysis_class_symbol(MoveClass c) {
    switch (c) {
    case CLASS_BRILLIANT: return "!!";
    case CLASS_INACCURACY: return "?!";
    case CLASS_MISTAKE: return "?";
    case CLASS_BLUNDER: return "??";
    default: return "";
    }
}

const char *analysis_class_glyph(MoveClass c) {
    static const char *glyphs[CLASS_COUNT] = {"", "!!", "*", "!", "+", "=", "?!", "?", "x", "??", "\xe2\x86\x92"};
    return c >= 0 && c < CLASS_COUNT ? glyphs[c] : "";
}

bool analysis_is_decided(const PositionEval *e) {
    return move_is_none(e->best) && e->mate == 0 && (e->score == 0 || iabs(e->score) >= EVAL_MATE_SCORE);
}

void analysis_format(const PositionEval *e, char *out, size_t n) {
    if (e->mate) {
        snprintf(out, n, "%sM%d", e->mate < 0 ? "-" : "", iabs(e->mate));
    } else if (analysis_is_decided(e)) {
        snprintf(out, n, "%s", e->score > 0 ? "1-0" : e->score < 0 ? "0-1" : "1/2");
    } else {
        int t = (iabs(e->score) + 5) / 10; /* tenths of a pawn */
        if (t == 0) snprintf(out, n, "0.0");
        else snprintf(out, n, "%c%d.%d", e->score > 0 ? '+' : '-', t / 10, t % 10);
    }
}

/* ------------------------------------------------------------------ */
/* Move ranking. */

/* Expected points for the side a search score belongs to. */
static double search_points(int v) { return IS_MATE(v) ? (v > 0 ? 1.0 : 0.0) : cp_points(v); }

int analysis_rank_moves(const Position *p, const uint64_t *history, int history_len, long budget,
                        RankedMove *out, const volatile int *stop) {
    Move legal[MAX_MOVES];
    Position next[MAX_MOVES];
    int n = pos_legal_moves(p, legal);
    for (int i = 0; i < n; i++) {
        next[i] = *p;
        pos_make(&next[i], legal[i]);
        /* Until a search finishes: the static look, from the mover's side. */
        double v = search_points(-evaluate(&next[i]));
        out[i] = (RankedMove){legal[i], v, v};
    }
    if (n < 2) return n;
    Search *s = calloc(1, sizeof *s);
    if (!s) return n;
    s->tt = calloc((size_t)1 << TT_BITS, sizeof(TTEntry));
    s->tt_mask = s->tt ? ((size_t)1 << TT_BITS) - 1 : 0;
    s->hist = history;
    s->hist_len = history ? history_len : 0;
    s->stop = stop;
    s->budget = budget;
    s->path[0] = p->hash;
    /* Every root move gets a full window, so each score is exact, not just a bound. */
    int scores[MAX_MOVES];
    for (int depth = 1; depth <= MAX_DEPTH; depth++) {
        for (int i = 0; i < n && !s->aborted; i++) scores[i] = -search(s, &next[i], depth - 1, -INF, INF, 1, true);
        if (s->aborted) break;
        bool mated = false;
        for (int i = 0; i < n; i++) {
            out[i].points = search_points(scores[i]);
            if (depth == 1) out[i].glance = out[i].points;
            mated = mated || scores[i] >= MATE - depth;
        }
        if (mated) break; /* a mate within reach is found; deeper won't change the pick */
    }
    free(s->tt);
    free(s);
    return n;
}
