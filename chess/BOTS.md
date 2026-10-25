# Writing a chess bot

A bot is one C99 file in `chess/src/bots/` that exports a `ChessBot`. It is pure logic with no SDL, so it builds into `chesscore` and can be unit tested.

## The contract (`src/bots/bot.h`)

```c
typedef struct {
    Position pos;              /* position to move in; pos.side is your color */
    const uint64_t *history;   /* hashes of earlier game positions, oldest first */
    int history_len;
    uint64_t seed;             /* differs per game and per move: use it for variety */
    const volatile int *stop;  /* may be NULL; nonzero = answer now */
    int level;                 /* index into your levels (0 if you have none) */
    BotSlips slips;            /* Student only: chances per move of an inaccuracy, mistake, blunder */
} BotRequest;

typedef struct {
    const char *name;   /* "Normal" */
    const char *about;  /* one line for the side panel */
} BotLevel;

typedef struct {
    const char *name;   /* shown over the board, e.g. "Tiny" */
    const char *about;  /* one short line for the side panel */
    Move (*choose)(const BotRequest *req);
    const BotLevel *levels;  /* weakest first, or NULL for a single strength */
    int level_count;
    int default_level;
} ChessBot;
```

`choose` must:
- **Return a legal move.** The app only calls it when at least one legal move exists. Pick from `pos_legal_moves`, or check pseudo-legal moves with `pos_make_legal`.
- **Be thread safe.** It runs on a worker thread, so use no mutable globals. Keep search state on the stack or allocate and free it inside the call.
- **Honor `stop`.** Poll `if (req->stop && *req->stop)` every ~1000 nodes and return your best move so far.
- **Bound its work.** The UI waits at least 400 ms anyway, so aim for 50–500 ms per move. Prefer a node or depth budget over the clock, so results are deterministic per seed and testable.

## Useful rules API (`src/board.h`)

| Call | Use |
|---|---|
| `pos_legal_moves(p, out)` | all legal moves (`out` has room for `MAX_MOVES`) |
| `pos_pseudo_moves(p, out, captures_only)` + `pos_make_legal(p, m, &next)` | fast search loop: generate, then skip moves where it returns false |
| `pos_make(&p, m)` | apply a move in place (copy-make; positions are small values, there's no unmake) |
| `pos_in_check(p)`, `pos_attacked(p, sq, by)` | tactics, king safety |
| `p->sq[64]`, `PTYPE(pc)`, `PCOLOR(pc)`, `SQ(f, r)`, `FILE_OF`, `RANK_OF` | board access; a1 = 0, h8 = 63 |
| `p->hash`, `p->halfmove` | repetition and fifty-move checks |
| `pos_see(p, m)` | static exchange: what `m` nets if both sides trade off on its square (negative = it hangs material) |
| `bot_seen(req, hash)` | true if `hash` already occurred in the game, so treat it as a draw |
| `move_eq(a, b)`, `move_is_none(m)`, `move_san`, `move_uci` | comparing and printing moves |
| `book_lookup(p, &name)`, `book_move(p, m)` (`book.h`) | is this a known opening position / move; handy for an opening book |

Mates are scored as `MATE - ply` so the shortest mate wins, and stalemate and repetition score 0. `tiny.c` is a complete reference of about 300 lines: alpha-beta, quiescence, piece-square tables, a node budget, and levels.

## Scoring bots (no search)

A personality is often easiest as "score every legal move, play the best". `bot.h` has helpers for that:

| Call | Use |
|---|---|
| `bot_moves(p, moves, next)` | legal moves plus the position after each |
| `bot_gain(p, m)` | material won on the spot (capture plus promotion) |
| `bot_mated(&next[i])` | the move mates |
| `bot_pick(req, moves, scores, n)` | the top score, ties broken by the seed, so equal moves vary per game |
| `bot_value[type]` | piece values in centipawns |

`greedy.c` (about 20 lines), `mirror.c` and `queenie.c` are examples.

## Adding one

1. Create `src/bots/<name>.c` ending with `const ChessBot bot_<name> = {"Name", "One-line blurb", <name>_choose, NULL, 0, 0};`, or pass `LEVELS, count, default_index` to offer strengths in the new-game picker (up to 5; see `tiny.c`).
2. Declare `extern const ChessBot bot_<name>;` in `bot.h` and add `&bot_<name>` to `chess_bots[]` in `bot.c`. The new-game picker lists bots in this order and shows at most 5 (`MAX_PICK` in `main.c`; all five slots are taken, so a sixth bot needs the picker to grow or another bot to go), and `--demo` pairs them at random.
3. Add the file to `chesscore` in `chess/CMakeLists.txt`.
4. Add a `test_<name>` to `tests/bot_test.c` for its personality (positions where it must do its thing; mate in one if it should find it). `test_roster` already plays every bot against every other and checks each move with `game_play`. Run `cmake --build build -j && ./build/bin/bot_tests`.

## Ideas for personalities

Student, Greedy, Mirror and Queenie are taken. Others that would fit:

- **Random:** a uniform choice from the legal moves (`bot_pick` with all-zero scores).
- **Pacifist:** never captures unless forced.
- **Pawn storm:** pushes pawns toward your king, scored like Queenie's queen.
- **Style versions of Tiny:** give its eval a bias, e.g. favor trades or king attacks.
