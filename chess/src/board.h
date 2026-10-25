/*
 * Chess rules: positions, legal move generation, SAN and FEN, and a game
 * record with end-of-game detection. SDL-free (built as `chesscore`).
 *
 * Squares are 0..63 with a1 = 0, b1 = 1, ..., h8 = 63 (square = rank * 8 + file).
 * Positions are small value types: making a move copies, there is no unmake.
 */
#ifndef CHESS_BOARD_H
#define CHESS_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WHITE = 0, BLACK = 1 };
enum { EMPTY = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

/* A piece is color << 3 | type; 0 is an empty square. */
#define PIECE(color, type) ((uint8_t)(((color) << 3) | (type)))
#define PTYPE(p) ((p) & 7)
#define PCOLOR(p) ((p) >> 3)

#define SQ(file, rank) ((rank) * 8 + (file))
#define FILE_OF(sq) ((sq) & 7)
#define RANK_OF(sq) ((sq) >> 3)

enum { CASTLE_WK = 1, CASTLE_WQ = 2, CASTLE_BK = 4, CASTLE_BQ = 8 };

typedef struct {
    uint8_t sq[64];
    uint8_t side;      /* WHITE or BLACK to move */
    uint8_t castle;    /* CASTLE_* bits still available */
    int8_t ep;         /* en passant target square, or -1 (set only when a capture is possible) */
    uint8_t king[2];   /* king squares by color */
    uint16_t halfmove; /* plies since the last capture or pawn move */
    uint16_t fullmove; /* starts at 1, incremented after Black moves */
    uint64_t hash;     /* Zobrist key of everything that matters for repetition */
} Position;

enum { MOVE_CAPTURE = 1, MOVE_EP = 2, MOVE_CASTLE = 4, MOVE_DOUBLE = 8 };

typedef struct {
    uint8_t from, to;
    uint8_t promo; /* piece type promoted to, or 0 */
    uint8_t flags; /* MOVE_* */
} Move;

#define MOVE_NONE ((Move){0, 0, 0, 0})
#define MAX_MOVES 256

static inline bool move_is_none(Move m) { return m.from == m.to; }
static inline bool move_eq(Move a, Move b) { return a.from == b.from && a.to == b.to && a.promo == b.promo; }

void pos_start(Position *p);
/* Parses the board, side, castling and en passant fields (the clocks are optional). */
bool pos_from_fen(Position *p, const char *fen);
void pos_to_fen(const Position *p, char *out, size_t n);

/* True if square `sq` is attacked by any piece of color `by`. */
bool pos_attacked(const Position *p, int sq, int by);
bool pos_in_check(const Position *p);

/* Fills `out` (room for MAX_MOVES) with the legal moves; returns the count. */
int pos_legal_moves(const Position *p, Move *out);
/* Pseudo-legal moves (may leave the king in check), for engines that filter
   with pos_make_legal(). Captures only when `captures_only`. */
int pos_pseudo_moves(const Position *p, Move *out, bool captures_only);
/* Applies a move that is at least pseudo-legal. */
void pos_make(Position *p, Move m);
/* Applies a pseudo-legal move into `out`; false if it leaves the mover in check. */
bool pos_make_legal(const Position *p, Move m, Position *out);
/* Finds the legal move from -> to (promotion type `promo`, 0 = queen when one
   is needed). Returns MOVE_NONE if there is none. */
Move pos_find_move(const Position *p, int from, int to, int promo);

/* Static exchange evaluation: the material (centipawns: P 100, N 320, B 330,
   R 500, Q 900) the mover nets from `m` if both sides then trade off on its
   square, each stopping when it pays to. Negative = the move loses material. */
int pos_see(const Position *p, Move m);

/* Neither side can possibly mate (K v K, K+minor v K, K+B v K+B same colors). */
bool pos_insufficient_material(const Position *p);

/* Standard algebraic notation ("Nbd2", "exd6", "e8=Q+", "O-O-O#"), computed in
   the position *before* the move. `out` needs 8 bytes. */
void move_san(const Position *p, Move m, char *out);
/* Long algebraic "e2e4" / "e7e8q". `out` needs 6 bytes. */
void move_uci(Move m, char *out);
void square_name(int sq, char *out); /* "e4", 3 bytes */

/* ------------------------------------------------------------------ */
/* A game record: every position, move and SAN, plus the result. */

#define GAME_MAX_PLY 1024

typedef enum { RESULT_NONE, RESULT_WHITE, RESULT_BLACK, RESULT_DRAW } GameResult;
typedef enum {
    END_NONE,
    END_CHECKMATE,
    END_RESIGN,
    END_STALEMATE,
    END_REPETITION, /* threefold, applied automatically */
    END_FIFTY,      /* fifty-move rule, applied automatically */
    END_MATERIAL,   /* insufficient material */
    END_LENGTH,     /* GAME_MAX_PLY reached */
    END_TIME,       /* a clock ran out (game_timeout) */
} GameEnd;

typedef struct {
    Position pos[GAME_MAX_PLY + 1]; /* pos[0] is the start, pos[ply] the current position */
    Move moves[GAME_MAX_PLY];       /* moves[i] leads from pos[i] to pos[i + 1] */
    char san[GAME_MAX_PLY][8];
    int ply;
    GameResult result;
    GameEnd end;
} Game;

void game_new(Game *g, const Position *start);
static inline const Position *game_pos(const Game *g) { return &g->pos[g->ply]; }
static inline bool game_over(const Game *g) { return g->result != RESULT_NONE; }
/* Plays a legal move and checks for the end of the game. False if the game is
   over or the move is illegal. */
bool game_play(Game *g, Move m);
/* Takes back `n` plies (clamped) and clears any result. */
void game_undo(Game *g, int n);
void game_resign(Game *g, int color);
/* `color` ran out of time: a loss, or a draw if the other side has only its king. */
void game_timeout(Game *g, int color);
/* How many earlier positions equal pos[ply] (for repetition). */
int game_repetitions(const Game *g, int ply);
/* Plays space-separated UCI moves ("e2e4 e7e5 e7e8q"), stopping at the first
   bad one. Returns how many were played. */
int game_play_uci(Game *g, const char *uci);
/* The game's moves as space-separated UCI (what game_play_uci reads). `out`
   needs 6 bytes per move; it's cut short, at a move boundary, if `n` is less. */
void game_uci(const Game *g, char *out, size_t n);
/* The game's moves as PGN movetext, ending with the result ("1. e4 e5 2. Nf3
   ... 1-0"), in lines of at most 79 characters. Needs about 16 bytes per ply;
   cut short at a move boundary if `n` is less. Assumes White moved first. */
void game_pgn(const Game *g, char *out, size_t n);
/* Hashes of pos[0..ply-1] into `out` (room for GAME_MAX_PLY); returns ply. */
int game_history(const Game *g, uint64_t *out);
/* "1-0", "0-1", "1/2-1/2" or "*". */
const char *game_result_string(GameResult r);
/* "Checkmate", "Stalemate", "Threefold repetition", ... ("" when not over). */
const char *game_end_string(GameEnd e);

#endif
