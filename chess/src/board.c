#include "board.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Zobrist keys, derived on demand so there is no table to initialise. */

static uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

enum { Z_SIDE = 16 * 64, Z_CASTLE = Z_SIDE + 1, Z_EP = Z_CASTLE + 16 };

static uint64_t pos_compute_hash(const Position *p) {
    uint64_t h = 0;
    for (int s = 0; s < 64; s++)
        if (p->sq[s]) h ^= splitmix64((uint64_t)p->sq[s] * 64 + s);
    if (p->side) h ^= splitmix64(Z_SIDE);
    h ^= splitmix64(Z_CASTLE + p->castle);
    if (p->ep >= 0) h ^= splitmix64(Z_EP + FILE_OF(p->ep));
    return h;
}

/* ------------------------------------------------------------------ */
/* Geometry. */

static const int KNIGHT_D[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
static const int KING_D[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static const int ROOK_D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int BISHOP_D[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/* Square at (file, rank), or -1 off the board. */
static int at(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8 ? SQ(f, r) : -1; }

/* ------------------------------------------------------------------ */
/* Setup and FEN. */

void pos_start(Position *p) {
    pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

static const char PIECE_CHARS[] = ".PNBRQK";

static uint8_t piece_from_char(char c) {
    const char *k = strchr(PIECE_CHARS + 1, c >= 'a' ? c - 32 : c);
    if (!k || !c) return 0;
    return PIECE(c >= 'a' ? BLACK : WHITE, (int)(k - PIECE_CHARS));
}

/* An en passant square only counts if an enemy pawn could capture onto it. */
static bool ep_capturable(const Position *p, int ep) {
    int pawn_rank = RANK_OF(ep) + (p->side == WHITE ? -1 : 1);
    uint8_t pawn = PIECE(p->side, PAWN);
    int a = at(FILE_OF(ep) - 1, pawn_rank), b = at(FILE_OF(ep) + 1, pawn_rank);
    return (a >= 0 && p->sq[a] == pawn) || (b >= 0 && p->sq[b] == pawn);
}

bool pos_from_fen(Position *p, const char *fen) {
    memset(p, 0, sizeof *p);
    p->ep = -1;
    p->fullmove = 1;
    int f = 0, r = 7;
    const char *s = fen;
    for (; *s && *s != ' '; s++) {
        if (*s == '/') {
            if (f != 8 || r == 0) return false;
            f = 0;
            r--;
        } else if (*s >= '1' && *s <= '8') {
            f += *s - '0';
        } else {
            uint8_t pc = piece_from_char(*s);
            if (!pc || f > 7) return false;
            p->sq[SQ(f, r)] = pc;
            f++;
        }
        if (f > 8) return false;
    }
    if (r != 0 || f != 8 || *s != ' ') return false;
    s++;
    if (*s != 'w' && *s != 'b') return false;
    p->side = *s == 'b' ? BLACK : WHITE;
    s++;
    while (*s == ' ') s++;
    for (; *s && *s != ' '; s++) {
        switch (*s) {
        case 'K': p->castle |= CASTLE_WK; break;
        case 'Q': p->castle |= CASTLE_WQ; break;
        case 'k': p->castle |= CASTLE_BK; break;
        case 'q': p->castle |= CASTLE_BQ; break;
        case '-': break;
        default: return false;
        }
    }
    while (*s == ' ') s++;
    if (*s >= 'a' && *s <= 'h' && (s[1] == '3' || s[1] == '6')) {
        int ep = SQ(s[0] - 'a', s[1] - '1');
        if (ep_capturable(p, ep)) p->ep = (int8_t)ep;
        s += 2;
    } else if (*s == '-') {
        s++;
    }
    int half = 0, full = 1;
    if (sscanf(s, "%d %d", &half, &full) >= 1) {
        p->halfmove = (uint16_t)(half < 0 ? 0 : half);
        p->fullmove = (uint16_t)(full < 1 ? 1 : full);
    }
    int kings[2] = {0, 0};
    for (int i = 0; i < 64; i++)
        if (PTYPE(p->sq[i]) == KING) {
            p->king[PCOLOR(p->sq[i])] = (uint8_t)i;
            kings[PCOLOR(p->sq[i])]++;
        }
    if (kings[0] != 1 || kings[1] != 1) return false;
    p->hash = pos_compute_hash(p);
    return true;
}

void pos_to_fen(const Position *p, char *out, size_t n) {
    char buf[100];
    int k = 0;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            uint8_t pc = p->sq[SQ(f, r)];
            if (!pc) {
                empty++;
                continue;
            }
            if (empty) buf[k++] = (char)('0' + empty);
            empty = 0;
            char c = PIECE_CHARS[PTYPE(pc)];
            buf[k++] = PCOLOR(pc) == BLACK ? (char)(c + 32) : c;
        }
        if (empty) buf[k++] = (char)('0' + empty);
        if (r) buf[k++] = '/';
    }
    buf[k++] = ' ';
    buf[k++] = p->side == WHITE ? 'w' : 'b';
    buf[k++] = ' ';
    if (!p->castle) buf[k++] = '-';
    if (p->castle & CASTLE_WK) buf[k++] = 'K';
    if (p->castle & CASTLE_WQ) buf[k++] = 'Q';
    if (p->castle & CASTLE_BK) buf[k++] = 'k';
    if (p->castle & CASTLE_BQ) buf[k++] = 'q';
    buf[k++] = ' ';
    if (p->ep >= 0) {
        square_name(p->ep, buf + k);
        k += 2;
    } else {
        buf[k++] = '-';
    }
    snprintf(buf + k, sizeof buf - k, " %d %d", p->halfmove, p->fullmove);
    snprintf(out, n, "%s", buf);
}

/* ------------------------------------------------------------------ */
/* Attacks. */

bool pos_attacked(const Position *p, int sq, int by) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    int pr = by == WHITE ? r - 1 : r + 1; /* rank the attacking pawns stand on */
    for (int df = -1; df <= 1; df += 2) {
        int s = at(f + df, pr);
        if (s >= 0 && p->sq[s] == PIECE(by, PAWN)) return true;
    }
    for (int i = 0; i < 8; i++) {
        int s = at(f + KNIGHT_D[i][0], r + KNIGHT_D[i][1]);
        if (s >= 0 && p->sq[s] == PIECE(by, KNIGHT)) return true;
        s = at(f + KING_D[i][0], r + KING_D[i][1]);
        if (s >= 0 && p->sq[s] == PIECE(by, KING)) return true;
    }
    for (int i = 0; i < 4; i++) {
        for (int k = 1;; k++) {
            int s = at(f + ROOK_D[i][0] * k, r + ROOK_D[i][1] * k);
            if (s < 0) break;
            uint8_t pc = p->sq[s];
            if (!pc) continue;
            if (PCOLOR(pc) == by && (PTYPE(pc) == ROOK || PTYPE(pc) == QUEEN)) return true;
            break;
        }
        for (int k = 1;; k++) {
            int s = at(f + BISHOP_D[i][0] * k, r + BISHOP_D[i][1] * k);
            if (s < 0) break;
            uint8_t pc = p->sq[s];
            if (!pc) continue;
            if (PCOLOR(pc) == by && (PTYPE(pc) == BISHOP || PTYPE(pc) == QUEEN)) return true;
            break;
        }
    }
    return false;
}

bool pos_in_check(const Position *p) { return pos_attacked(p, p->king[p->side], p->side ^ 1); }

/* ------------------------------------------------------------------ */
/* Move generation. */

static int add(Move *out, int n, int from, int to, int promo, int flags) {
    out[n] = (Move){(uint8_t)from, (uint8_t)to, (uint8_t)promo, (uint8_t)flags};
    return n + 1;
}

static int add_pawn(Move *out, int n, int from, int to, int flags, bool captures_only) {
    int r = RANK_OF(to);
    if (r == 0 || r == 7) {
        n = add(out, n, from, to, QUEEN, flags);
        if (captures_only && !(flags & MOVE_CAPTURE)) return n;
        n = add(out, n, from, to, KNIGHT, flags);
        n = add(out, n, from, to, ROOK, flags);
        return add(out, n, from, to, BISHOP, flags);
    }
    return add(out, n, from, to, 0, flags);
}

int pos_pseudo_moves(const Position *p, Move *out, bool captures_only) {
    int n = 0, us = p->side, them = us ^ 1;
    int fwd = us == WHITE ? 1 : -1, home = us == WHITE ? 1 : 6;
    for (int from = 0; from < 64; from++) {
        uint8_t pc = p->sq[from];
        if (!pc || PCOLOR(pc) != us) continue;
        int f = FILE_OF(from), r = RANK_OF(from), type = PTYPE(pc);
        switch (type) {
        case PAWN: {
            int one = at(f, r + fwd);
            bool promo_rank = RANK_OF(one) == 0 || RANK_OF(one) == 7;
            if (one >= 0 && !p->sq[one] && (!captures_only || promo_rank)) {
                n = add_pawn(out, n, from, one, 0, captures_only);
                int two = at(f, r + 2 * fwd);
                if (r == home && !captures_only && !p->sq[two]) n = add(out, n, from, two, 0, MOVE_DOUBLE);
            }
            for (int df = -1; df <= 1; df += 2) {
                int to = at(f + df, r + fwd);
                if (to < 0) continue;
                if (p->sq[to] && PCOLOR(p->sq[to]) == them)
                    n = add_pawn(out, n, from, to, MOVE_CAPTURE, captures_only);
                else if (to == p->ep)
                    n = add(out, n, from, to, 0, MOVE_CAPTURE | MOVE_EP);
            }
            break;
        }
        case KNIGHT:
        case KING: {
            const int(*d)[2] = type == KNIGHT ? KNIGHT_D : KING_D;
            for (int i = 0; i < 8; i++) {
                int to = at(f + d[i][0], r + d[i][1]);
                if (to < 0) continue;
                uint8_t t = p->sq[to];
                if (!t && !captures_only) n = add(out, n, from, to, 0, 0);
                else if (t && PCOLOR(t) == them) n = add(out, n, from, to, 0, MOVE_CAPTURE);
            }
            break;
        }
        default: {
            for (int i = 0; i < 8; i++) {
                const int *d = i < 4 ? ROOK_D[i] : BISHOP_D[i - 4];
                if ((i < 4 && type == BISHOP) || (i >= 4 && type == ROOK)) continue;
                for (int k = 1;; k++) {
                    int to = at(f + d[0] * k, r + d[1] * k);
                    if (to < 0) break;
                    uint8_t t = p->sq[to];
                    if (!t) {
                        if (!captures_only) n = add(out, n, from, to, 0, 0);
                        continue;
                    }
                    if (PCOLOR(t) == them) n = add(out, n, from, to, 0, MOVE_CAPTURE);
                    break;
                }
            }
            break;
        }
        }
    }
    if (captures_only) return n;

    /* Castling: rights, empty path, rook in place, and no attacked square on the king's way. */
    int base = us == WHITE ? 0 : 56, k = base + 4;
    int ks = us == WHITE ? CASTLE_WK : CASTLE_BK, qs = us == WHITE ? CASTLE_WQ : CASTLE_BQ;
    if ((p->castle & (ks | qs)) && p->sq[k] == PIECE(us, KING) && !pos_attacked(p, k, them)) {
        if ((p->castle & ks) && p->sq[base + 7] == PIECE(us, ROOK) && !p->sq[base + 5] && !p->sq[base + 6] &&
            !pos_attacked(p, base + 5, them) && !pos_attacked(p, base + 6, them))
            n = add(out, n, k, base + 6, 0, MOVE_CASTLE);
        if ((p->castle & qs) && p->sq[base] == PIECE(us, ROOK) && !p->sq[base + 1] && !p->sq[base + 2] &&
            !p->sq[base + 3] && !pos_attacked(p, base + 3, them) && !pos_attacked(p, base + 2, them))
            n = add(out, n, k, base + 2, 0, MOVE_CASTLE);
    }
    return n;
}

/* Castling rights that survive a move touching each square. */
static uint8_t castle_keep(int sq) {
    switch (sq) {
    case 0: return (uint8_t)~CASTLE_WQ;
    case 7: return (uint8_t)~CASTLE_WK;
    case 4: return (uint8_t)~(CASTLE_WK | CASTLE_WQ);
    case 56: return (uint8_t)~CASTLE_BQ;
    case 63: return (uint8_t)~CASTLE_BK;
    case 60: return (uint8_t)~(CASTLE_BK | CASTLE_BQ);
    default: return 0xFF;
    }
}

void pos_make(Position *p, Move m) {
    int us = p->side;
    uint8_t pc = p->sq[m.from];
    bool reset = PTYPE(pc) == PAWN || p->sq[m.to] || (m.flags & MOVE_EP);
    if (m.flags & MOVE_EP) p->sq[m.to + (us == WHITE ? -8 : 8)] = EMPTY;
    if (m.flags & MOVE_CASTLE) {
        int base = us == WHITE ? 0 : 56;
        bool king_side = FILE_OF(m.to) == 6;
        int rf = base + (king_side ? 7 : 0), rt = base + (king_side ? 5 : 3);
        p->sq[rt] = p->sq[rf];
        p->sq[rf] = EMPTY;
    }
    p->sq[m.to] = m.promo ? PIECE(us, m.promo) : pc;
    p->sq[m.from] = EMPTY;
    if (PTYPE(pc) == KING) p->king[us] = m.to;
    p->castle &= castle_keep(m.from) & castle_keep(m.to);
    p->side = (uint8_t)(us ^ 1);
    p->ep = -1;
    if (m.flags & MOVE_DOUBLE) {
        int ep = (m.from + m.to) / 2;
        if (ep_capturable(p, ep)) p->ep = (int8_t)ep;
    }
    p->halfmove = reset ? 0 : (uint16_t)(p->halfmove + 1);
    if (us == BLACK) p->fullmove++;
    p->hash = pos_compute_hash(p);
}

bool pos_make_legal(const Position *p, Move m, Position *out) {
    *out = *p;
    pos_make(out, m);
    return !pos_attacked(out, out->king[p->side], out->side);
}

int pos_legal_moves(const Position *p, Move *out) {
    Move pseudo[MAX_MOVES];
    int n = pos_pseudo_moves(p, pseudo, false), k = 0;
    Position next;
    for (int i = 0; i < n; i++)
        if (pos_make_legal(p, pseudo[i], &next)) out[k++] = pseudo[i];
    return k;
}

Move pos_find_move(const Position *p, int from, int to, int promo) {
    Move moves[MAX_MOVES];
    int n = pos_legal_moves(p, moves);
    for (int i = 0; i < n; i++)
        if (moves[i].from == from && moves[i].to == to && (!moves[i].promo || moves[i].promo == (promo ? promo : QUEEN)))
            return moves[i];
    return MOVE_NONE;
}

bool pos_insufficient_material(const Position *p) {
    int minors = 0, bishops = 0, bishop_colors = 0;
    for (int s = 0; s < 64; s++) {
        int t = PTYPE(p->sq[s]);
        if (t == PAWN || t == ROOK || t == QUEEN) return false;
        if (t == KNIGHT) minors++;
        if (t == BISHOP) {
            minors++;
            bishops++;
            bishop_colors |= 1 << ((FILE_OF(s) + RANK_OF(s)) & 1);
        }
    }
    return minors <= 1 || (minors == bishops && bishop_colors != 3);
}

/* ------------------------------------------------------------------ */
/* Static exchange evaluation on a scratch board. */

static const int SEE_VALUE[7] = {0, 100, 320, 330, 500, 900, 20000};

/* The least valuable piece of color `c` attacking `sq` on board `b`, or -1. */
static int least_attacker(const uint8_t *b, int sq, int c) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    int pr = r + (c == WHITE ? -1 : 1);
    for (int df = -1; df <= 1; df += 2) {
        int s = at(f + df, pr);
        if (s >= 0 && b[s] == PIECE(c, PAWN)) return s;
    }
    for (int i = 0; i < 8; i++) {
        int s = at(f + KNIGHT_D[i][0], r + KNIGHT_D[i][1]);
        if (s >= 0 && b[s] == PIECE(c, KNIGHT)) return s;
    }
    int best = -1, best_v = 1 << 30;
    for (int i = 0; i < 8; i++) {
        bool diag = i < 4;
        const int *d = diag ? BISHOP_D[i] : ROOK_D[i - 4];
        for (int k = 1;; k++) {
            int s = at(f + d[0] * k, r + d[1] * k);
            if (s < 0) break;
            if (!b[s]) continue;
            int t = PTYPE(b[s]);
            if (PCOLOR(b[s]) == c && (t == QUEEN || t == (diag ? BISHOP : ROOK)) && SEE_VALUE[t] < best_v) {
                best = s;
                best_v = SEE_VALUE[t];
            }
            break;
        }
    }
    if (best >= 0) return best;
    for (int i = 0; i < 8; i++) {
        int s = at(f + KING_D[i][0], r + KING_D[i][1]);
        if (s >= 0 && b[s] == PIECE(c, KING)) return s;
    }
    return -1;
}

int pos_see(const Position *p, Move m) {
    uint8_t b[64];
    memcpy(b, p->sq, 64);
    int to = m.to, gain[40], d = 0;
    if (m.flags & MOVE_EP) {
        b[to + (p->side == WHITE ? -8 : 8)] = EMPTY;
        gain[0] = SEE_VALUE[PAWN];
    } else {
        gain[0] = SEE_VALUE[PTYPE(b[to])];
    }
    int piece = m.promo ? m.promo : PTYPE(b[m.from]);
    if (m.promo) gain[0] += SEE_VALUE[m.promo] - SEE_VALUE[PAWN];
    b[to] = b[m.from];
    b[m.from] = EMPTY;
    for (int side = p->side ^ 1; d < 38; side ^= 1) {
        int from = least_attacker(b, to, side);
        if (from < 0) break;
        d++;
        gain[d] = SEE_VALUE[piece] - gain[d - 1];
        piece = PTYPE(b[from]);
        b[to] = b[from];
        b[from] = EMPTY;
        if (piece == KING && least_attacker(b, to, side ^ 1) >= 0) { d--; break; } /* illegal recapture */
    }
    while (d > 0) { /* each side may stop trading when going on would lose */
        if (gain[d] > -gain[d - 1]) gain[d - 1] = -gain[d];
        d--;
    }
    return gain[0];
}

/* ------------------------------------------------------------------ */
/* Notation. */

void square_name(int sq, char *out) {
    out[0] = (char)('a' + FILE_OF(sq));
    out[1] = (char)('1' + RANK_OF(sq));
    out[2] = 0;
}

void move_uci(Move m, char *out) {
    square_name(m.from, out);
    square_name(m.to, out + 2);
    out[4] = m.promo ? "..nbrq"[m.promo] : 0;
    out[5] = 0;
}

void move_san(const Position *p, Move m, char *out) {
    int k = 0, type = PTYPE(p->sq[m.from]);
    if (m.flags & MOVE_CASTLE) {
        k += snprintf(out, 8, "%s", FILE_OF(m.to) == 6 ? "O-O" : "O-O-O");
    } else {
        if (type == PAWN) {
            if (m.flags & MOVE_CAPTURE) out[k++] = (char)('a' + FILE_OF(m.from));
        } else {
            out[k++] = PIECE_CHARS[type];
            /* Disambiguate against other pieces of the same type reaching the same square. */
            Move moves[MAX_MOVES];
            int n = pos_legal_moves(p, moves);
            bool clash = false, same_file = false, same_rank = false;
            for (int i = 0; i < n; i++) {
                Move o = moves[i];
                if (o.to != m.to || o.from == m.from || PTYPE(p->sq[o.from]) != type) continue;
                clash = true;
                if (FILE_OF(o.from) == FILE_OF(m.from)) same_file = true;
                if (RANK_OF(o.from) == RANK_OF(m.from)) same_rank = true;
            }
            if (clash && (!same_file || same_rank)) out[k++] = (char)('a' + FILE_OF(m.from));
            if (clash && same_file) out[k++] = (char)('1' + RANK_OF(m.from));
        }
        if (m.flags & MOVE_CAPTURE) out[k++] = 'x';
        square_name(m.to, out + k);
        k += 2;
        if (m.promo) {
            out[k++] = '=';
            out[k++] = PIECE_CHARS[m.promo];
        }
    }
    Position next = *p;
    pos_make(&next, m);
    if (pos_in_check(&next)) {
        Move replies[MAX_MOVES];
        out[k++] = pos_legal_moves(&next, replies) ? '+' : '#';
    }
    out[k] = 0;
}

/* ------------------------------------------------------------------ */
/* Game record. */

void game_new(Game *g, const Position *start) {
    g->ply = 0;
    g->pos[0] = *start;
    g->result = RESULT_NONE;
    g->end = END_NONE;
}

int game_repetitions(const Game *g, int ply) {
    const Position *cur = &g->pos[ply];
    int n = 0;
    for (int i = ply - 2; i >= 0 && i >= ply - cur->halfmove; i -= 2)
        if (g->pos[i].hash == cur->hash) n++;
    return n;
}

static void game_check_end(Game *g) {
    const Position *p = game_pos(g);
    Move moves[MAX_MOVES];
    if (!pos_legal_moves(p, moves)) {
        if (pos_in_check(p)) {
            g->result = p->side == WHITE ? RESULT_BLACK : RESULT_WHITE;
            g->end = END_CHECKMATE;
        } else {
            g->result = RESULT_DRAW;
            g->end = END_STALEMATE;
        }
        return;
    }
    GameEnd end = pos_insufficient_material(p) ? END_MATERIAL
                  : game_repetitions(g, g->ply) >= 2 ? END_REPETITION
                  : p->halfmove >= 100 ? END_FIFTY
                  : g->ply >= GAME_MAX_PLY ? END_LENGTH
                                           : END_NONE;
    if (end != END_NONE) {
        g->result = RESULT_DRAW;
        g->end = end;
    }
}

bool game_play(Game *g, Move m) {
    if (game_over(g) || g->ply >= GAME_MAX_PLY) return false;
    const Position *p = game_pos(g);
    Move legal = pos_find_move(p, m.from, m.to, m.promo);
    if (move_is_none(legal) || (m.promo && legal.promo != m.promo)) return false;
    move_san(p, legal, g->san[g->ply]);
    g->moves[g->ply] = legal;
    g->pos[g->ply + 1] = *p;
    pos_make(&g->pos[g->ply + 1], legal);
    g->ply++;
    game_check_end(g);
    return true;
}

void game_undo(Game *g, int n) {
    g->ply = n > g->ply ? 0 : g->ply - n;
    g->result = RESULT_NONE;
    g->end = END_NONE;
}

void game_resign(Game *g, int color) {
    if (game_over(g)) return;
    g->result = color == WHITE ? RESULT_BLACK : RESULT_WHITE;
    g->end = END_RESIGN;
}

void game_timeout(Game *g, int color) {
    if (game_over(g)) return;
    const Position *p = game_pos(g);
    bool bare = true; /* a lone king can't mate, so the flag only draws */
    for (int sq = 0; sq < 64; sq++)
        if (p->sq[sq] && PCOLOR(p->sq[sq]) != color && PTYPE(p->sq[sq]) != KING) bare = false;
    g->result = bare ? RESULT_DRAW : color == WHITE ? RESULT_BLACK : RESULT_WHITE;
    g->end = END_TIME;
}

static bool uci_square(const char *s) { return s[0] >= 'a' && s[0] <= 'h' && s[1] >= '1' && s[1] <= '8'; }

int game_play_uci(Game *g, const char *uci) {
    int played = 0;
    for (const char *s = uci;; played++) {
        while (*s == ' ' || *s == '\n') s++;
        if (!*s || !uci_square(s) || !uci_square(s + 2)) return played;
        bool has_promo = s[4] && s[4] != ' ' && s[4] != '\n';
        const char *pr = has_promo ? strchr("nbrq", s[4]) : NULL;
        if (has_promo && !pr) return played;
        int promo = pr ? (int)(pr - "nbrq") + KNIGHT : 0;
        if (!game_play(g, pos_find_move(game_pos(g), SQ(s[0] - 'a', s[1] - '1'), SQ(s[2] - 'a', s[3] - '1'), promo)))
            return played;
        s += has_promo ? 5 : 4;
    }
}

void game_uci(const Game *g, char *out, size_t n) {
    size_t k = 0;
    if (n) out[0] = 0;
    for (int i = 0; i < g->ply && k + 7 <= n; i++) {
        char m[6];
        move_uci(g->moves[i], m);
        k += (size_t)snprintf(out + k, n - k, "%s%s", i ? " " : "", m);
    }
}

void game_pgn(const Game *g, char *out, size_t n) {
    size_t len = 0, line = 0;
    if (n) out[0] = 0;
    for (int i = 0; i <= g->ply; i++) {
        char tok[24];
        if (i == g->ply) snprintf(tok, sizeof tok, "%s", game_result_string(g->result));
        else if (i % 2 == 0) snprintf(tok, sizeof tok, "%d. %s", i / 2 + 1, g->san[i]);
        else snprintf(tok, sizeof tok, "%s", g->san[i]);
        size_t t = strlen(tok);
        const char *sep = line == 0 ? "" : line + 1 + t > 79 ? "\n" : " ";
        if (len + strlen(sep) + t + 1 > n) return;
        len += (size_t)snprintf(out + len, n - len, "%s%s", sep, tok);
        line = *sep == '\n' ? t : line + strlen(sep) + t;
    }
}

int game_history(const Game *g, uint64_t *out) {
    for (int i = 0; i < g->ply; i++) out[i] = g->pos[i].hash;
    return g->ply;
}

const char *game_result_string(GameResult r) {
    switch (r) {
    case RESULT_WHITE: return "1-0";
    case RESULT_BLACK: return "0-1";
    case RESULT_DRAW: return "1/2-1/2";
    default: return "*";
    }
}

const char *game_end_string(GameEnd e) {
    switch (e) {
    case END_CHECKMATE: return "Checkmate";
    case END_RESIGN: return "Resignation";
    case END_TIME: return "Out of time";
    case END_STALEMATE: return "Stalemate";
    case END_REPETITION: return "Threefold repetition";
    case END_FIFTY: return "Fifty-move rule";
    case END_MATERIAL: return "Insufficient material";
    case END_LENGTH: return "Move limit";
    default: return "";
    }
}
