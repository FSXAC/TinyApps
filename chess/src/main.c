/*
 * Chess - play a bot offline, then review the game with an eval bar.
 * Click-click or drag to move; right-click marks squares, right-drag draws arrows.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "analysis.h"
#include "board.h"
#include "book.h"
#include "bot.h"
#include "evalbar.h"
#include "pieces.h"
#include "tiny.h"
#include "trainer.h"

#define BOT_MIN_NS (400 * SDL_NS_PER_MS) /* the bot never answers faster than this */
#define DEMO_MOVE_NS (1500 * (uint64_t)SDL_NS_PER_MS) /* --demo: slow enough to follow */
#define DEMO_RESTART_NS (7000 * (uint64_t)SDL_NS_PER_MS) /* --demo: pause on the result before the next game */
#define MAX_ARROWS 64
#define PANEL_W 220  /* units */
#define MAX_PICK 5   /* bots and levels the new-game picker can show */
#define MAX_TIMER_MIN 180 /* --timer limit, minutes */

typedef struct {
    int x, y, w, h;
} Rect;

static bool in_rect(Rect r, int x, int y) { return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

typedef enum {
    BTN_PREV,
    BTN_NEXT,
    BTN_NEW,
    BTN_FLIP,
    BTN_UNDO,
    BTN_END, /* Resign while playing, Review once the game is over */
    BTN_BEST, /* review: reveal what the engine would have played instead of the shown move */
    BTN_DLG_REVIEW,
    BTN_DLG_NEW,
    BTN_PICK_BOT,                         /* new-game picker: one per bot... */
    BTN_PICK_LEVEL = BTN_PICK_BOT + MAX_PICK, /* ...one per level of the picked bot */
    BTN_PLAY = BTN_PICK_LEVEL + MAX_PICK,
    BTN_CANCEL,
    BTN_COUNT
} Button;

typedef struct {
    int u, w, h;
    Rect board;
    int sq; /* square size */
    Rect evalbar, top, bottom, panel;
    Rect list; /* move list */
    int row_h;
    Rect info; /* review line under the move list */
    Rect btn[BTN_COUNT];
    Rect dialog;
    Rect setup; /* new-game picker */
    int setup_label[3]; /* y of its "Opponent", "Strength" and blurb lines */
} Layout;

typedef struct {
    int from, to;
} Arrow;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TinyFont font;
    PieceSet pieces;
    Layout lay;
    int mx, my; /* mouse, renderer pixels */

    Game game;
    const ChessBot *bots[2]; /* by color; only the bot side's entry matters unless in demo */
    int levels[2];           /* index into bots[c]->levels */
    bool demo;               /* --demo: bots play each other, forever */
    uint64_t demo_next;      /* when the next demo game starts, or 0 */
    bool started; /* a game has been set up (false until the first Play) */
    bool setup;   /* new-game picker showing */
    int pick_bot, pick_level;
    int user; /* the user's color */
    bool flipped;
    int view;    /* ply shown; game.ply when live */
    bool review; /* game review mode */
    bool dialog; /* game-over dialog showing */
    int scroll;  /* first visible move-list row */
    bool show_best;     /* review: best move revealed */
    float wheel_list;   /* fractional wheel travel not yet turned into steps */
    float wheel_board;

    /* Left button: click-click or drag. */
    int selected;  /* square or -1 */
    int press_sq;  /* square the left button went down on, or -1 */
    bool press_reselect;
    bool dragging;
    int press_x, press_y;
    int promo_from, promo_to; /* promotion picker, or -1 */
    int pressed_btn;          /* button under a left press, or -1 */

    /* Right button annotations. */
    uint64_t marks;
    Arrow arrows[MAX_ARROWS];
    int n_arrows;
    int rpress_sq; /* square the right button went down on, or -1 */

    /* Bot worker: owns bot_req and bot_hist while running. */
    Uint32 ev_bot;
    SDL_Thread *bot_thread;
    volatile int bot_stop;
    int bot_gen;
    BotRequest bot_req;
    uint64_t bot_hist[GAME_MAX_PLY];
    uint64_t seed;

    /* Analysis worker: reads the game, writes evals/classes; an_done counts evaluated positions. */
    Uint32 ev_an;
    SDL_Thread *an_thread;
    volatile int an_stop;
    int an_gen;
    SDL_AtomicInt an_done;
    SDL_AtomicInt an_complete;
    uint64_t an_hist[GAME_MAX_PLY];
    PositionEval evals[GAME_MAX_PLY + 1];
    MoveClass classes[GAME_MAX_PLY];
    const char *opening[GAME_MAX_PLY + 1]; /* opening name at each ply, NULL until one is known */
    double accuracy[2];

    /* Review: a line the user plays out from the shown move, on a copy of the game
       (var.pos[0..var_base] are the game's). Its own worker evaluates var_view. */
    bool exploring;
    Game var;
    int var_base, var_view;
    PositionEval var_evals[GAME_MAX_PLY + 1];
    bool var_known[GAME_MAX_PLY + 1];
    Uint32 ev_var;
    SDL_Thread *var_thread;
    volatile int var_stop;
    int var_gen;
    int var_ply; /* what the worker evaluates: var_req, with var_hist[0..var_ply) */
    Position var_req;
    uint64_t var_hist[GAME_MAX_PLY];
    PositionEval var_result;

    /* Your clock (--timer); the bot has none. It runs only on your turn in a live game. */
    int64_t timer_ms;     /* --timer, for new games; 0 = untimed */
    int64_t clock_ms;     /* this game's starting time; 0 = untimed */
    int64_t clock_left;   /* ns */
    uint64_t clock_since; /* when it was last charged, while running; else 0 */

    char save_path[1024]; /* the saved game; empty = don't load or save */
    char archive_path[1024]; /* every game, appended as PGN when a new one replaces it; empty = keep none */
    SDL_Time started_at;     /* when this game began, for the archive; 0 = unknown */

    /* The Student's memory of you, and a log of each game it learned from;
       empty paths = it plays from memory but learns nothing (dev runs). */
    Trainer trainer;
    char trainer_path[1024];
    char log_path[1024];
} App;

static const char *color_name(int c) { return c == WHITE ? "White" : "Black"; }
static bool is_bot(const App *a, int c) { return a->demo || c != a->user; }
static const char *player_name(const App *a, int c) { return is_bot(a, c) ? a->bots[c]->name : "You"; }
static const BotLevel *bot_level(const App *a, int c) {
    return a->bots[c]->levels ? &a->bots[c]->levels[a->levels[c]] : NULL;
}
static const char *bot_about(const App *a, int c) { return bot_level(a, c) ? bot_level(a, c)->about : a->bots[c]->about; }
static bool bot_turn(const App *a) { return a->started && !game_over(&a->game) && is_bot(a, game_pos(&a->game)->side); }
static bool live(const App *a) { return a->view == a->game.ply; }
/* Against the Student there's no undo, and every game counts. */
static bool vs_student(const App *a) { return a->started && !a->demo && a->bots[a->user ^ 1] == &bot_student; }

/* ------------------------------------------------------------------ */
/* Workers. */

static uint32_t pack_move(Move m) { return m.from | m.to << 8 | m.promo << 16 | (uint32_t)m.flags << 24; }
static Move unpack_move(uint32_t v) {
    return (Move){(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
}

static int SDLCALL bot_main(void *data) {
    App *a = data;
    uint64_t t0 = SDL_GetTicksNS();
    Move m = a->bots[a->bot_req.pos.side]->choose(&a->bot_req);
    uint64_t min_ns = a->demo ? DEMO_MOVE_NS : BOT_MIN_NS;
    while (!a->bot_stop && SDL_GetTicksNS() - t0 < min_ns) SDL_Delay(10);
    SDL_Event e = {0};
    e.type = a->ev_bot;
    e.user.code = a->bot_gen;
    e.user.data1 = (void *)(uintptr_t)pack_move(m);
    SDL_PushEvent(&e);
    return 0;
}

static void cancel_bot(App *a) {
    if (!a->bot_thread) return;
    a->bot_stop = 1;
    SDL_WaitThread(a->bot_thread, NULL);
    a->bot_thread = NULL;
    a->bot_gen++; /* its pending event, if any, is now stale */
}

static void start_bot(App *a) {
    cancel_bot(a);
    if (!bot_turn(a)) return;
    a->bot_req = (BotRequest){*game_pos(&a->game), a->bot_hist, game_history(&a->game, a->bot_hist),
                              a->seed + (uint64_t)a->game.ply, &a->bot_stop, a->levels[game_pos(&a->game)->side],
                              trainer_slips(&a->trainer)};
    a->bot_stop = 0;
    a->bot_thread = SDL_CreateThread(bot_main, "bot", a);
    if (!a->bot_thread) SDL_Log("bot thread: %s", SDL_GetError());
}

static void push_analysis_event(App *a) {
    SDL_Event e = {0};
    e.type = a->ev_an;
    e.user.code = a->an_gen;
    SDL_PushEvent(&e);
}

static int SDLCALL analysis_main(void *data) {
    App *a = data;
    const Game *g = &a->game;
    bool in_book = true; /* every move so far was book */
    const char *opening = NULL;
    for (int i = 0; i <= g->ply; i++) {
        PositionEval e = analysis_evaluate(&g->pos[i], a->an_hist, i, &a->an_stop);
        if (a->an_stop) return 0;
        a->evals[i] = e;
        const char *name = NULL;
        bool was_book = in_book;
        in_book = in_book && book_lookup(&g->pos[i], &name);
        if (name) opening = name;
        a->opening[i] = opening;
        if (i > 0)
            a->classes[i - 1] = was_book && in_book ? CLASS_BOOK
                                                    : analysis_classify(&g->pos[i - 1], g->moves[i - 1], &a->evals[i - 1], &e);
        SDL_SetAtomicInt(&a->an_done, i + 1);
        push_analysis_event(a);
    }
    a->accuracy[WHITE] = analysis_accuracy(a->evals, g->ply, WHITE);
    a->accuracy[BLACK] = analysis_accuracy(a->evals, g->ply, BLACK);
    SDL_SetAtomicInt(&a->an_complete, 1);
    push_analysis_event(a);
    return 0;
}

static void cancel_analysis(App *a) {
    if (a->an_thread) {
        a->an_stop = 1;
        SDL_WaitThread(a->an_thread, NULL);
        a->an_thread = NULL;
    }
    a->an_gen++;
    SDL_SetAtomicInt(&a->an_done, 0);
    SDL_SetAtomicInt(&a->an_complete, 0);
}

/* Analyses the finished game in the background, once. */
static void start_analysis(App *a) {
    if (a->an_thread) return;
    game_history(&a->game, a->an_hist);
    a->an_stop = 0;
    a->an_thread = SDL_CreateThread(analysis_main, "analysis", a);
    if (!a->an_thread) SDL_Log("analysis thread: %s", SDL_GetError());
}

static int SDLCALL var_main(void *data) {
    App *a = data;
    a->var_result = analysis_evaluate(&a->var_req, a->var_hist, a->var_ply, &a->var_stop);
    SDL_Event e = {0};
    e.type = a->ev_var;
    e.user.code = a->var_gen;
    SDL_PushEvent(&e);
    return 0;
}

static void cancel_var(App *a) {
    if (!a->var_thread) return;
    a->var_stop = 1;
    SDL_WaitThread(a->var_thread, NULL);
    a->var_thread = NULL;
    a->var_gen++;
}

/* Evaluates the shown position of the explored line, unless it's known. If the
   worker is busy with another one, its event starts this one when it's done. */
static void start_var(App *a) {
    if (!a->exploring || a->var_thread || a->var_known[a->var_view]) return;
    a->var_ply = a->var_view;
    a->var_req = a->var.pos[a->var_ply];
    game_history(&a->var, a->var_hist);
    a->var_stop = 0;
    a->var_thread = SDL_CreateThread(var_main, "explore", a);
    if (!a->var_thread) SDL_Log("explore thread: %s", SDL_GetError());
}

static int analysed(const App *a) { return SDL_GetAtomicInt((SDL_AtomicInt *)&a->an_done); }
static bool analysis_finished(const App *a) { return SDL_GetAtomicInt((SDL_AtomicInt *)&a->an_complete) != 0; }

/* ------------------------------------------------------------------ */
/* Persistence: the game in progress (or the last one) survives quitting, as text. */

static void save_game(const App *a) {
    if (a->demo || !a->started || !a->save_path[0]) return;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", a->save_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    static char moves[GAME_MAX_PLY * 6];
    game_uci(&a->game, moves, sizeof moves);
    const Game *g = &a->game;
    int resigned = g->end == END_RESIGN ? (g->result == RESULT_WHITE ? BLACK : WHITE) : -1;
    int timeout = g->end == END_TIME ? a->user : -1;
    fprintf(f, "chess 1\n");
    for (int c = 0; c < 2; c++) fprintf(f, "bot %d %d %s\n", c, a->levels[c], a->bots[c]->name);
    fprintf(f, "user %d\nflipped %d\nreview %d\nview %d\nresigned %d\ntimeout %d\nseed %llu\n", a->user, a->flipped,
            a->review, a->view, resigned, timeout, (unsigned long long)a->seed);
    fprintf(f, "started %lld\n", (long long)a->started_at);
    if (a->clock_ms)
        fprintf(f, "clock %lld %lld\n", (long long)a->clock_ms, (long long)SDL_max(a->clock_left / 1000000, 0));
    fprintf(f, "moves %s\n", moves);
    bool ok = !ferror(f);
    ok = (fclose(f) == 0) && ok;
    if (ok) rename(tmp, a->save_path);
    else remove(tmp);
}

static int find_bot(const char *name) {
    for (int i = 0; i < chess_bot_count; i++)
        if (strcmp(chess_bots[i]->name, name) == 0) return i;
    return -1;
}

static void enter_review(App *a);
static void ensure_visible(App *a);
static void start_bot(App *a);

/* Restores the saved game; false (and nothing changed) if there's none. */
static bool load_game(App *a) {
    FILE *f = a->save_path[0] ? fopen(a->save_path, "r") : NULL;
    if (!f) return false;
    static char line[GAME_MAX_PLY * 6 + 16];
    static char moves[GAME_MAX_PLY * 6 + 16];
    int version = 0, user = WHITE, flipped = 0, review = 0, view = -1, resigned = -1, timeout = -1;
    long long clock_ms = 0, clock_left = 0, started_at = 0;
    int bots[2] = {0, 0}, levels[2] = {chess_bots[0]->default_level, chess_bots[0]->default_level};
    unsigned long long seed = 0;
    bool have_moves = false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        int c, lv, n = 0;
        if (sscanf(line, "bot %d %d %n", &c, &lv, &n) == 2 && n > 0 && (c == WHITE || c == BLACK)) {
            int b = find_bot(line + n);
            if (b >= 0) {
                bots[c] = b;
                levels[c] = chess_bots[b]->levels ? SDL_clamp(lv, 0, chess_bots[b]->level_count - 1) : 0;
            }
        } else if (strncmp(line, "moves", 5) == 0) {
            snprintf(moves, sizeof moves, "%s", line + 5);
            have_moves = true;
        } else if (!sscanf(line, "chess %d", &version) && !sscanf(line, "user %d", &user) &&
                   !sscanf(line, "flipped %d", &flipped) && !sscanf(line, "review %d", &review) &&
                   !sscanf(line, "view %d", &view) && !sscanf(line, "resigned %d", &resigned) &&
                   !sscanf(line, "timeout %d", &timeout) && !sscanf(line, "started %lld", &started_at) &&
                   sscanf(line, "clock %lld %lld", &clock_ms, &clock_left) != 2) {
            sscanf(line, "seed %llu", &seed);
        }
    }
    fclose(f);
    if (version != 1 || !have_moves) return false;

    Position start;
    pos_start(&start);
    game_new(&a->game, &start);
    game_play_uci(&a->game, moves);
    if (resigned == WHITE || resigned == BLACK) game_resign(&a->game, resigned);
    if (timeout == WHITE || timeout == BLACK) game_timeout(&a->game, timeout);
    a->clock_ms = clock_ms > 0 ? clock_ms : 0; /* the game keeps the clock it started with */
    a->clock_left = a->clock_ms ? SDL_clamp(clock_left, 0, clock_ms) * 1000000 : 0;
    a->clock_since = 0;
    for (int c = 0; c < 2; c++) {
        a->bots[c] = chess_bots[bots[c]];
        a->levels[c] = levels[c];
    }
    a->user = user == BLACK ? BLACK : WHITE;
    a->flipped = flipped != 0;
    a->seed = seed;
    a->started_at = (SDL_Time)started_at;
    a->pick_bot = bots[a->user ^ 1]; /* New offers the same opponent */
    a->pick_level = levels[a->user ^ 1];
    a->started = true;
    a->setup = false;
    a->view = a->game.ply;
    if (review && game_over(&a->game)) { /* back to the move being reviewed */
        enter_review(a);
        a->view = view >= 0 && view <= a->game.ply ? view : a->game.ply;
    } else if (game_over(&a->game) && vs_student(a)) {
        start_analysis(a); /* in case it ended before the Student could learn from it */
    }
    ensure_visible(a);
    start_bot(a);
    return true;
}

/* ------------------------------------------------------------------ */
/* The Student's memory (trainer.h), in trainer.txt next to the saved game. */

static void load_trainer(App *a) {
    trainer_init(&a->trainer);
    FILE *f = a->trainer_path[0] ? fopen(a->trainer_path, "r") : NULL;
    if (!f) return;
    char text[1024];
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    trainer_parse(&a->trainer, text);
}

static void save_trainer(const App *a) {
    char tmp[1100], text[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", a->trainer_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    trainer_format(&a->trainer, text, sizeof text);
    bool ok = fputs(text, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    if (ok) rename(tmp, a->trainer_path);
    else remove(tmp);
}

/* Learns from the finished game against the Student, once: its result, and
   the review's grades of your moves as far as the analysis has got (all of
   them unless a new game cut it short). Also appends a line to the log, for
   stats later. */
static void learn(App *a) {
    const Game *g = &a->game;
    if (!vs_student(a) || !game_over(g) || !a->trainer_path[0]) return;
    GameResult won = a->user == WHITE ? RESULT_WHITE : RESULT_BLACK;
    TrainerGame t = {a->seed, g->result == RESULT_DRAW ? 0.5 : g->result == won ? 1 : 0, 0, {0, 0, 0}};
    bool finished = analysis_finished(a);
    trainer_tally(&t, a->classes, a->evals, finished ? g->ply : SDL_max(analysed(a) - 1, 0), a->user);
    BotSlips played = trainer_slips(&a->trainer); /* what the Student had this game */
    if (!trainer_record(&a->trainer, &t)) return;
    save_trainer(a);

    FILE *f = a->log_path[0] ? fopen(a->log_path, "a") : NULL;
    if (!f) return;
    SDL_Time now = 0;
    SDL_DateTime dt = {0};
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &dt, true);
    fprintf(f, "%04d-%02d-%02d %02d:%02d %s %s %s plies %d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
            color_name(a->user), t.score == 1 ? "win" : t.score == 0 ? "loss" : "draw", game_end_string(g->end), g->ply);
    fprintf(f, " moves %d inaccuracies %d mistakes %d blunders %d", t.moves, t.slips[0], t.slips[1], t.slips[2]);
    if (finished) fprintf(f, " accuracy %.1f", a->accuracy[a->user]);
    fprintf(f, " student %.3f %.3f %.3f\n", played.inaccuracy, played.mistake, played.blunder);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Game flow. */

static void clear_annotations(App *a) {
    a->marks = 0;
    a->n_arrows = 0;
}

static void reset_input(App *a) {
    a->selected = a->press_sq = a->promo_from = a->promo_to = a->rpress_sq = a->pressed_btn = -1;
    a->dragging = false;
}

static void game_ended(App *a) {
    if (a->demo) { /* no dialog or analysis: show the result, then play on */
        a->demo_next = SDL_GetTicksNS() + DEMO_RESTART_NS;
        return;
    }
    a->dialog = true;
    reset_input(a);
    start_analysis(a);
}

/* Opens the new-game picker; the current game carries on underneath until Play. */
static void open_setup(App *a) {
    a->setup = true;
    a->dialog = false;
    a->promo_from = a->promo_to = -1;
    a->dragging = false;
    a->press_sq = -1;
}

static void new_game(App *a) {
    cancel_bot(a);
    cancel_analysis(a);
    cancel_var(a);
    a->exploring = false;
    Position start;
    pos_start(&start);
    game_new(&a->game, &start);
    a->user = SDL_rand(2) ? BLACK : WHITE;
    a->flipped = a->user == BLACK && !a->demo;
    a->demo_next = 0;
    a->seed = (uint64_t)SDL_rand_bits() << 32 | SDL_rand_bits();
    if (!SDL_GetCurrentTime(&a->started_at)) a->started_at = 0;
    a->clock_ms = a->demo ? 0 : a->timer_ms;
    a->clock_left = a->clock_ms * 1000000;
    a->clock_since = 0;
    a->view = 0;
    a->review = a->dialog = a->show_best = false;
    a->scroll = 0;
    reset_input(a);
    clear_annotations(a);
    start_bot(a);
}

/* PGN name for a side: "You", or the bot with its level, "Tiny (Normal)". */
static void pgn_player(const App *a, int c, char *out, size_t n) {
    if (bot_level(a, c) && is_bot(a, c)) snprintf(out, n, "%s (%s)", player_name(a, c), bot_level(a, c)->name);
    else snprintf(out, n, "%s", player_name(a, c));
}

/* Appends the current game to the archive as PGN, if anything was played. An
   unfinished game is kept too, with result "*". Called only when a new game
   replaces it, so a game is never archived twice (Undo can reopen a finished one). */
static void archive_game(const App *a) {
    const Game *g = &a->game;
    if (!a->started || a->demo || !a->archive_path[0] || g->ply == 0) return;
    FILE *f = fopen(a->archive_path, "a");
    if (!f) return;
    SDL_DateTime dt = {0};
    bool dated = a->started_at && SDL_TimeToDateTime(a->started_at, &dt, true);
    char white[64], black[64];
    pgn_player(a, WHITE, white, sizeof white);
    pgn_player(a, BLACK, black, sizeof black);
    fprintf(f, "[Event \"Casual game\"]\n[Site \"TinyApps Chess\"]\n");
    if (dated)
        fprintf(f, "[Date \"%04d.%02d.%02d\"]\n[Time \"%02d:%02d:%02d\"]\n", dt.year, dt.month, dt.day, dt.hour,
                dt.minute, dt.second);
    else
        fprintf(f, "[Date \"????.??.??\"]\n");
    fprintf(f, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n", white, black, game_result_string(g->result));
    fprintf(f, "[Termination \"%s\"]\n", game_over(g) ? game_end_string(g->end) : "Unfinished");
    if (a->clock_ms) fprintf(f, "[TimeControl \"%lld\"]\n", (long long)(a->clock_ms / 1000)); /* your clock only */
    static char moves[GAME_MAX_PLY * 16];
    game_pgn(g, moves, sizeof moves);
    fprintf(f, "\n%s\n\n", moves);
    fclose(f);
}

/* True if starting a new game now resigns this one: you've moved in a live
   game against the Student. */
static bool play_resigns(const App *a) {
    return vs_student(a) && !game_over(&a->game) && a->game.ply > (a->user == BLACK);
}

/* Starts a game against the picked bot and level. A game against the Student
   is settled first: resigned if you'd moved, and remembered. */
static void play(App *a) {
    if (play_resigns(a)) {
        cancel_bot(a);
        game_resign(&a->game, a->user);
    }
    learn(a);
    archive_game(a);
    a->bots[0] = a->bots[1] = chess_bots[a->pick_bot];
    a->levels[0] = a->levels[1] = a->bots[0]->levels ? a->pick_level : 0;
    a->setup = false;
    a->started = true;
    new_game(a);
    save_game(a);
}

/* --demo: a new game between two randomly picked bots and levels. */
static void demo_game(App *a) {
    for (int c = 0; c < 2; c++) {
        a->bots[c] = chess_bots[SDL_rand(chess_bot_count)];
        a->levels[c] = a->bots[c]->levels ? SDL_rand(a->bots[c]->level_count) : 0;
    }
    a->started = true;
    a->setup = false;
    new_game(a);
}

static void pick_bot(App *a, int i) {
    if (i < 0 || i >= SDL_min(chess_bot_count, MAX_PICK)) return;
    if (i != a->pick_bot) a->pick_level = chess_bots[i]->default_level;
    a->pick_bot = i;
}

static void pick_level(App *a, int i) {
    const ChessBot *b = chess_bots[a->pick_bot];
    if (b->levels && i >= 0 && i < SDL_min(b->level_count, MAX_PICK)) a->pick_level = i;
}

static void after_move(App *a) {
    a->view = a->game.ply;
    ensure_visible(a);
    if (game_over(&a->game)) game_ended(a);
    else start_bot(a);
}

/* Leaves the explored line for the game, at the move it branched from. */
static void stop_exploring(App *a) {
    if (!a->exploring) return;
    cancel_var(a);
    a->exploring = false;
    a->selected = a->promo_from = a->promo_to = -1;
}

/* Review: plays `m` in the shown position, branching off the game the first time.
   Moves made after stepping back in the line replace the rest of it. */
static void explore_move(App *a, Move m) {
    cancel_var(a);
    if (!a->exploring) {
        a->var = a->game;
        a->var_base = a->var_view = a->view;
        memset(a->var_known, 0, sizeof a->var_known);
        a->exploring = true;
    }
    game_undo(&a->var, a->var.ply - a->var_view);
    for (int i = a->var_view + 1; i <= GAME_MAX_PLY; i++) a->var_known[i] = false;
    if (game_play(&a->var, m)) a->var_view = a->var.ply;
    else if (a->var_view == a->var_base) a->exploring = false;
    start_var(a);
}

static void user_move(App *a, Move m) {
    a->selected = -1;
    if (a->review) {
        explore_move(a, m);
    } else if (game_play(&a->game, m)) {
        after_move(a);
        save_game(a);
    }
}

/* Takes back the user's last move (and the bot's reply). */
static void step(App *a, int d);

static void undo(App *a) {
    if (a->exploring) {
        step(a, -1);
        return;
    }
    if (a->demo || a->review || a->game.ply == 0 || a->game.end == END_TIME || vs_student(a)) return;
    if (a->game.end == END_RESIGN) {
        game_undo(&a->game, 0);
    } else {
        int n = game_pos(&a->game)->side == a->user ? 2 : 1;
        if (a->game.ply < n) return; /* only the bot's opening move is on the board */
        cancel_bot(a);
        game_undo(&a->game, n);
    }
    cancel_analysis(a);
    a->dialog = false;
    reset_input(a);
    a->view = a->game.ply;
    ensure_visible(a);
    start_bot(a);
    save_game(a);
}

static void resign(App *a) {
    if (game_over(&a->game)) return;
    cancel_bot(a);
    game_resign(&a->game, a->user);
    game_ended(a);
    save_game(a);
}

static void enter_review(App *a) {
    if (a->demo || !game_over(&a->game)) return;
    start_analysis(a);
    a->review = true;
    a->dialog = false;
    reset_input(a);
    a->view = 0;
    ensure_visible(a);
    save_game(a);
}

static void go_to(App *a, int ply) {
    stop_exploring(a);
    a->view = SDL_clamp(ply, 0, a->game.ply);
    a->selected = -1;
    a->promo_from = a->promo_to = -1;
    ensure_visible(a);
}

/* One move back or forward: along the explored line, else through the game. */
static void step(App *a, int d) {
    if (!a->exploring) {
        go_to(a, a->view + d);
        return;
    }
    a->var_view = SDL_min(a->var_view + d, a->var.ply);
    a->selected = a->promo_from = a->promo_to = -1;
    if (a->var_view <= a->var_base) stop_exploring(a); /* back where it branched */
    else start_var(a);
}

/* ------------------------------------------------------------------ */
/* Your clock. */

static bool clock_running(const App *a) {
    return a->clock_ms && a->started && !a->demo && !a->setup && !game_over(&a->game) &&
           game_pos(&a->game)->side == a->user;
}

/* Charges the time since the last tick while the clock runs, and starts or
   stops it to match the game. Called around every batch of events, so each
   interval is charged by the state it was spent in. Running out ends the game. */
static void tick_clock(App *a) {
    uint64_t now = SDL_GetTicksNS();
    if (a->clock_since) a->clock_left -= (int64_t)(now - a->clock_since);
    a->clock_since = clock_running(a) ? now : 0;
    if (a->clock_since && a->clock_left <= 0) {
        a->clock_left = 0;
        a->clock_since = 0;
        game_timeout(&a->game, a->user);
        a->view = a->game.ply;
        game_ended(a);
        save_game(a);
    }
}

/* Tenths of a second left, rounded up (so it reads 0 only once it's out). */
static int64_t clock_tenths(const App *a) {
    int64_t tenth = 100 * (int64_t)SDL_NS_PER_MS;
    return a->clock_left > 0 ? (a->clock_left + tenth - 1) / tenth : 0;
}

/* "4:59", "1:02:03", or "9.4" under ten seconds. */
static void clock_text(const App *a, char *out, size_t n) {
    int64_t t = clock_tenths(a);
    if (t < 100) {
        snprintf(out, n, "%d.%d", (int)(t / 10), (int)(t % 10));
        return;
    }
    int s = (int)((t + 9) / 10);
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

/* Milliseconds until the shown time changes (the main loop wakes then). */
static Sint32 clock_wait_ms(const App *a) {
    int64_t step = (clock_tenths(a) <= 100 ? 100 : 1000) * (int64_t)SDL_NS_PER_MS;
    int64_t rem = a->clock_left % step;
    return (Sint32)((rem ? rem : step) / SDL_NS_PER_MS) + 1;
}

/* ------------------------------------------------------------------ */
/* Layout, in renderer pixels. */

static void layout(App *a) {
    Layout *L = &a->lay;
    SDL_GetRenderOutputSize(a->ren, &L->w, &L->h);
    int u = L->u = tiny_unit(a->win, a->ren);
    /* Player strips: a framed 32-unit avatar, then a gap to the board. */
    int avatar = 34 * u, strip_gap = 10 * u;
    int m = TINY_MARGIN * u, strip = avatar + strip_gap, eb = EVALBAR_WIDTH * u, gap = TINY_PAD * u, pw = PANEL_W * u;
    int avail_w = L->w - 2 * m - eb - gap - m - pw, avail_h = L->h - 2 * m - 2 * strip;
    int sq = SDL_max(SDL_min(avail_w, avail_h) / 8, 4 * u);
    int size = L->sq = sq;
    size = sq * 8;
    int block_w = eb + gap + size + m + pw, block_h = size + 2 * strip;
    int x0 = SDL_max((L->w - block_w) / 2, m), y0 = SDL_max((L->h - block_h) / 2, m);
    L->evalbar = (Rect){x0, y0 + strip, eb, size};
    L->board = (Rect){x0 + eb + gap, y0 + strip, size, size};
    L->top = (Rect){L->board.x, y0, size, avatar};
    L->bottom = (Rect){L->board.x, y0 + strip + size + strip_gap, size, avatar};
    L->panel = (Rect){L->board.x + size + m, y0, pw, block_h};

    /* Panel, bottom up: action buttons (2x2), nav buttons, review info, then the move list. */
    int pad = TINY_PAD * u, g = TINY_GAP * u, bh = 22 * u;
    int ix = L->panel.x + pad, iw = pw - 2 * pad, half = (iw - g) / 2;
    int y = L->panel.y + L->panel.h - pad - bh;
    L->btn[BTN_UNDO] = (Rect){ix, y, half, bh};
    L->btn[BTN_END] = (Rect){ix + half + g, y, iw - half - g, bh};
    y -= bh + g;
    L->btn[BTN_NEW] = (Rect){ix, y, half, bh};
    L->btn[BTN_FLIP] = (Rect){ix + half + g, y, iw - half - g, bh};
    y -= bh + pad;
    L->btn[BTN_PREV] = (Rect){ix, y, half, bh};
    L->btn[BTN_NEXT] = (Rect){ix + half + g, y, iw - half - g, bh};
    int info_h = a->review ? 2 * tiny_line_height(TINY_BODY * u) + bh + 2 * pad : 0;
    y -= info_h + pad;
    L->info = (Rect){ix, y, iw, info_h};
    L->btn[BTN_BEST] = a->review ? (Rect){ix, y + info_h - bh, iw, bh} : (Rect){0};
    /* Header: bot name and blurb, then a status line and a hint under it. */
    int list_top = L->panel.y + pad + 2 * tiny_line_height(TINY_BODY * u) + 2 * tiny_line_height(TINY_CAPTION * u) +
                   pad + TINY_GAP * u;
    L->row_h = tiny_line_height(TINY_BODY * u);
    L->list = (Rect){ix, list_top, iw, SDL_max(y - list_top - g, L->row_h)};

    /* Game-over dialog, centered on the board. */
    int dw = SDL_min(size - 2 * pad, 200 * u);
    int dh = tiny_line_height(TINY_TITLE * u) + tiny_line_height(TINY_CAPTION * u) + bh + 3 * pad;
    L->dialog = (Rect){L->board.x + (size - dw) / 2, L->board.y + (size - dh) / 2, dw, dh};
    int by = L->dialog.y + dh - pad - bh, bw = (dw - 2 * pad - g) / 2;
    L->btn[BTN_DLG_REVIEW] = a->dialog ? (Rect){L->dialog.x + pad, by, bw, bh} : (Rect){0};
    L->btn[BTN_DLG_NEW] = a->dialog ? (Rect){L->dialog.x + pad + bw + g, by, bw, bh} : (Rect){0};

    /* New-game picker: title, bots, levels, the level's blurb, then Cancel and Play. */
    for (int b = BTN_PICK_BOT; b <= BTN_CANCEL; b++) L->btn[b] = (Rect){0};
    L->setup = (Rect){0};
    if (!a->setup) return;
    const ChessBot *pb = chess_bots[a->pick_bot];
    int nb = SDL_min(chess_bot_count, MAX_PICK), nl = pb->levels ? SDL_min(pb->level_count, MAX_PICK) : 0;
    int cap = tiny_line_height(TINY_CAPTION * u);
    int sw = SDL_min(size - 2 * pad, 360 * u), sx = L->board.x + (size - sw) / 2, iw2 = sw - 2 * pad;
    int sh = pad + tiny_line_height(TINY_TITLE * u) + cap + nb * (bh + g) + (nl ? cap + bh + g : 0) + cap + pad + bh + pad;
    int sy = L->board.y + (size - sh) / 2;
    L->setup = (Rect){sx, sy, sw, sh};
    y = sy + pad + tiny_line_height(TINY_TITLE * u);
    L->setup_label[0] = y;
    y += cap;
    for (int i = 0; i < nb; i++, y += bh + g) L->btn[BTN_PICK_BOT + i] = (Rect){sx + pad, y, iw2, bh};
    L->setup_label[1] = y;
    if (nl) {
        y += cap;
        int lw = (iw2 - (nl - 1) * g) / nl;
        for (int i = 0; i < nl; i++)
            L->btn[BTN_PICK_LEVEL + i] = (Rect){sx + pad + i * (lw + g), y, i == nl - 1 ? iw2 - i * (lw + g) : lw, bh};
        y += bh + g;
    }
    L->setup_label[2] = y;
    y += cap + pad;
    int hw = (iw2 - g) / 2;
    L->btn[BTN_CANCEL] = (Rect){sx + pad, y, hw, bh};
    L->btn[BTN_PLAY] = (Rect){sx + pad + hw + g, y, iw2 - hw - g, bh};
}

static int list_rows(const App *a) { return SDL_max(a->lay.list.h / a->lay.row_h, 1); }

/* Scrolls the move list so the viewed move is visible. */
static void ensure_visible(App *a) {
    layout(a);
    int row = a->view > 0 ? (a->view - 1) / 2 : 0, rows = list_rows(a);
    if (row < a->scroll) a->scroll = row;
    if (row >= a->scroll + rows) a->scroll = row - rows + 1;
}

static void clamp_scroll(App *a) {
    int total = (a->game.ply + 1) / 2;
    a->scroll = SDL_clamp(a->scroll, 0, SDL_max(total - list_rows(a), 0));
}

/* Rect of the move-list cell for `ply`, or w == 0 if scrolled out. */
static Rect move_cell(const App *a, int ply) {
    const Layout *L = &a->lay;
    int row = ply / 2 - a->scroll;
    if (row < 0 || row >= list_rows(a)) return (Rect){0};
    int num_w = tiny_text_width("00.", TINY_BODY * L->u, TINY_PLAIN) + TINY_GAP * L->u;
    int cw = (L->list.w - num_w) / 2;
    return (Rect){L->list.x + num_w + (ply & 1) * cw, L->list.y + row * L->row_h, cw, L->row_h};
}

static Rect square_rect(const App *a, int sq) {
    int f = FILE_OF(sq), r = RANK_OF(sq);
    int col = a->flipped ? 7 - f : f, row = a->flipped ? r : 7 - r;
    int s = a->lay.sq;
    return (Rect){a->lay.board.x + col * s, a->lay.board.y + row * s, s, s};
}

static int square_at(const App *a, int x, int y) {
    if (!in_rect(a->lay.board, x, y)) return -1;
    int col = (x - a->lay.board.x) / a->lay.sq, row = (y - a->lay.board.y) / a->lay.sq;
    return a->flipped ? SQ(7 - col, row) : SQ(col, 7 - row);
}

/* Promotion picker cell i (queen, knight, rook, bishop), stacked from the target toward the middle. */
static const int PROMO_TYPES[4] = {QUEEN, KNIGHT, ROOK, BISHOP};
static Rect promo_cell(const App *a, int i) {
    Rect r = square_rect(a, a->promo_to);
    int dir = r.y == a->lay.board.y ? 1 : -1;
    r.y += dir * i * a->lay.sq;
    return r;
}

/* ------------------------------------------------------------------ */
/* Drawing. */

static int text(App *a, const char *s, int x, int y, int scale, TinyStyle st, TinyColor c) {
    tiny_set_color(a->ren, c);
    return tiny_text(&a->font, s, x, y, scale, st);
}

/* Fits `s` into `w` pixels, eliding with "…". */
static void fit_text(const char *s, int w, int scale, TinyStyle st, char *out, size_t n) {
    snprintf(out, n, "%s", s);
    if (tiny_text_width(out, scale, st) <= w) return;
    size_t len = strlen(out);
    while (len > 0) {
        do len--; /* drop one whole UTF-8 sequence */
        while (len > 0 && (out[len] & 0xC0) == 0x80);
        snprintf(out + len, n - len, "…");
        if (tiny_text_width(out, scale, st) <= w) return;
    }
}

/* A button; `on` shows it pressed, for toggles. */
static void button_on(App *a, Button b, const char *label, bool enabled, bool on) {
    Rect r = a->lay.btn[b];
    if (r.w <= 0) return;
    int u = a->lay.u;
    TinyState st = on && enabled ? TINY_PRESSED : TINY_NORMAL;
    if (enabled && in_rect(r, a->mx, a->my) && !on) st = a->pressed_btn == (int)b ? TINY_PRESSED : TINY_HOVER;
    TinyColor c = tiny_button(a->ren, r.x, r.y, r.w, r.h, u, st);
    int scale = TINY_BODY * u;
    if (tiny_text_width(label, scale, TINY_BOLD) > r.w - 4 * u) scale = TINY_CAPTION * u; /* narrow button */
    tiny_set_color(a->ren, enabled ? c : TINY_GRAY);
    tiny_text_centered(&a->font, label, r.x + r.w / 2, r.y + (r.h - tiny_text_height(scale)) / 2, scale, TINY_BOLD);
}

static void button(App *a, Button b, const char *label, bool enabled) { button_on(a, b, label, enabled, false); }

/* Rectangle frame of thickness t. */
static void frame(SDL_Renderer *r, Rect q, int t) {
    tiny_fill_rect(r, q.x, q.y, q.w, t);
    tiny_fill_rect(r, q.x, q.y + q.h - t, q.w, t);
    tiny_fill_rect(r, q.x, q.y + t, t, q.h - 2 * t);
    tiny_fill_rect(r, q.x + q.w - t, q.y + t, t, q.h - 2 * t);
}

/* The game shown on the board (the explored line while exploring) and its ply. */
static const Game *shown_game(const App *a) { return a->exploring ? &a->var : &a->game; }
static int shown_ply(const App *a) { return a->exploring ? a->var_view : a->view; }
/* The position on the board, which is also the one moves are made in. */
static const Position *viewed(const App *a) { return &shown_game(a)->pos[shown_ply(a)]; }
static Move shown_best(const App *a);
static Move better_move(const App *a);
static bool button_enabled(const App *a, Button b);

static void draw_square(App *a, int sq, bool highlight, bool check) {
    Rect r = square_rect(a, sq);
    bool light = (FILE_OF(sq) + RANK_OF(sq)) & 1;
    if (check) {
        tiny_set_color(a->ren, TINY_INK);
        tiny_fill_rect(a->ren, r.x, r.y, r.w, r.h);
    } else if (highlight) {
        tiny_set_color(a->ren, light ? TINY_SHADE : TINY_GRAY);
        tiny_fill_rect(a->ren, r.x, r.y, r.w, r.h);
    } else if (light) {
        tiny_set_color(a->ren, TINY_PAPER);
        tiny_fill_rect(a->ren, r.x, r.y, r.w, r.h);
    } else {
        tiny_fill_gray50(a->ren, r.x, r.y, r.w, r.h, a->lay.u);
    }
}

/* Coordinates on the edge squares, on small paper chips so they read on the dither. */
static void draw_coordinates(App *a) {
    int u = a->lay.u, s = TINY_CAPTION * u;
    int cw = tiny_text_width("a", s, TINY_PLAIN) + 2 * u, ch = tiny_text_height(s) + 2 * u;
    for (int i = 0; i < 8; i++) {
        char label[2] = {(char)('1' + i), 0};
        Rect r = square_rect(a, SQ(a->flipped ? 7 : 0, i));
        tiny_set_color(a->ren, TINY_PAPER);
        tiny_fill_rect(a->ren, r.x, r.y, cw, ch);
        text(a, label, r.x + u, r.y + u, s, TINY_PLAIN, TINY_DIM);
        label[0] = (char)('a' + i);
        r = square_rect(a, SQ(i, a->flipped ? 7 : 0));
        tiny_set_color(a->ren, TINY_PAPER);
        tiny_fill_rect(a->ren, r.x + r.w - cw, r.y + r.h - ch, cw, ch);
        text(a, label, r.x + r.w - cw + u, r.y + r.h - ch + u, s, TINY_PLAIN, TINY_DIM);
    }
}

/* Legal-move hints: a dot on empty squares, corner wedges on captures. */
static void draw_target(App *a, int sq, bool capture) {
    Rect r = square_rect(a, sq);
    int u = a->lay.u;
    SDL_Renderer *ren = a->ren;
    if (!capture) {
        int d = SDL_max(r.w / 4 / u * u, 4 * u);
        int x = r.x + (r.w - d) / 2, y = r.y + (r.h - d) / 2;
        tiny_set_color(ren, TINY_PAPER);
        tiny_fill_round(ren, x - u, y - u, d + 2 * u, d + 2 * u, d / u / 2 + 1, u, TINY_CORNERS);
        tiny_set_color(ren, TINY_INK);
        tiny_fill_round(ren, x, y, d, d, d / u / 2, u, TINY_CORNERS);
        return;
    }
    int n = SDL_max(r.w / 5 / u, 3); /* wedge legs, in units */
    tiny_set_color(ren, TINY_INK);
    for (int j = 0; j < n; j++) {
        int len = (n - j) * u;
        tiny_fill_rect(ren, r.x, r.y + j * u, len, u);
        tiny_fill_rect(ren, r.x + r.w - len, r.y + j * u, len, u);
        tiny_fill_rect(ren, r.x, r.y + r.h - (j + 1) * u, len, u);
        tiny_fill_rect(ren, r.x + r.w - len, r.y + r.h - (j + 1) * u, len, u);
    }
}

/* Right-click mark: an ink ring with a paper lining. */
static void draw_mark(App *a, int sq) {
    Rect r = square_rect(a, sq);
    int u = a->lay.u;
    tiny_set_color(a->ren, TINY_INK);
    frame(a->ren, (Rect){r.x + u, r.y + u, r.w - 2 * u, r.h - 2 * u}, 3 * u);
    tiny_set_color(a->ren, TINY_PAPER);
    frame(a->ren, (Rect){r.x + 4 * u, r.y + 4 * u, r.w - 8 * u, r.h - 8 * u}, u);
}

/* --- Arrows: flat translucent polygons, built so pieces never overlap (which would double the alpha). */

typedef struct {
    SDL_Vertex v[18];
    int n;
} Shape;

static void tri(Shape *s, SDL_FColor c, float x0, float y0, float x1, float y1, float x2, float y2) {
    float p[6] = {x0, y0, x1, y1, x2, y2};
    for (int i = 0; i < 3; i++) s->v[s->n++] = (SDL_Vertex){{p[2 * i], p[2 * i + 1]}, c, {0, 0}};
}

/* A shaft from (x0,y0) to (x1,y1), w wide. */
static void shaft(Shape *s, SDL_FColor c, float x0, float y0, float x1, float y1, float w) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) return;
    float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    tri(s, c, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny);
    tri(s, c, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny);
}

static void draw_arrow(App *a, int from, int to, TinyColor color, uint8_t alpha) {
    Rect rf = square_rect(a, from), rt = square_rect(a, to);
    float s = (float)a->lay.sq;
    float x0 = rf.x + s / 2, y0 = rf.y + s / 2, x1 = rt.x + s / 2, y1 = rt.y + s / 2;
    float w = s * 0.2f, head_w = s * 0.5f, head_l = s * 0.4f;
    SDL_Color k = tiny_color(color);
    SDL_FColor c = {k.r / 255.f, k.g / 255.f, k.b / 255.f, alpha / 255.f};
    Shape sh = {.n = 0};

    /* Knight moves bend once, going the long way first. */
    int df = abs(FILE_OF(to) - FILE_OF(from)), dr = abs(RANK_OF(to) - RANK_OF(from));
    if ((df == 1 && dr == 2) || (df == 2 && dr == 1)) {
        float cx = fabsf(x1 - x0) > fabsf(y1 - y0) ? x1 : x0, cy = cx == x1 ? y0 : y1;
        float ux = (cx - x0) / fmaxf(fabsf(cx - x0) + fabsf(cy - y0), 1);
        float uy = (cy - y0) / fmaxf(fabsf(cx - x0) + fabsf(cy - y0), 1);
        shaft(&sh, c, x0 + ux * s * 0.25f, y0 + uy * s * 0.25f, cx + ux * w / 2, cy + uy * w / 2, w);
        x0 = cx;
        y0 = cy;
        float vx = (x1 - x0) / fmaxf(fabsf(x1 - x0) + fabsf(y1 - y0), 1);
        float vy = (y1 - y0) / fmaxf(fabsf(x1 - x0) + fabsf(y1 - y0), 1);
        x0 += vx * w / 2;
        y0 += vy * w / 2;
    } else {
        float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
        x0 += dx / len * s * 0.25f;
        y0 += dy / len * s * 0.25f;
    }
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= head_l) return;
    float ux = dx / len, uy = dy / len, bx = x1 - ux * head_l, by = y1 - uy * head_l;
    shaft(&sh, c, x0, y0, bx, by, w);
    tri(&sh, c, bx - uy * head_w / 2, by + ux * head_w / 2, x1, y1, bx + uy * head_w / 2, by - ux * head_w / 2);
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(a->ren, NULL, sh.v, sh.n, NULL, 0);
}

static bool is_target(const App *a, int to, bool *capture) {
    if (a->selected < 0) return false;
    Move moves[MAX_MOVES];
    int n = pos_legal_moves(viewed(a), moves);
    for (int i = 0; i < n; i++)
        if (moves[i].from == a->selected && moves[i].to == to) {
            if (capture) *capture = moves[i].flags & MOVE_CAPTURE;
            return true;
        }
    return false;
}

/* What the engine would have played instead of the shown move, or MOVE_NONE if that
   was the move played (or it isn't analysed yet). */
static Move better_move(const App *a) {
    if (!a->review || a->exploring || a->view == 0 || analysed(a) <= a->view) return MOVE_NONE;
    Move best = a->evals[a->view - 1].best;
    return move_eq(best, a->game.moves[a->view - 1]) ? MOVE_NONE : best;
}

/* better_move(), once revealed with Show best. */
static Move shown_best(const App *a) { return a->show_best ? better_move(a) : MOVE_NONE; }

static void draw_board(App *a) {
    const Position *p = viewed(a);
    int u = a->lay.u;
    int ply = shown_ply(a);
    Move last = ply > 0 ? shown_game(a)->moves[ply - 1] : MOVE_NONE;
    bool check = pos_in_check(p);
    for (int sq = 0; sq < 64; sq++) {
        bool hl = (ply > 0 && (sq == last.from || sq == last.to)) || sq == a->selected;
        draw_square(a, sq, hl, check && sq == p->king[p->side]);
    }
    draw_coordinates(a);
    for (int sq = 0; sq < 64; sq++)
        if (a->marks >> sq & 1) draw_mark(a, sq);

    /* While dragging, the target under the cursor gets a frame. */
    int hover = a->dragging ? square_at(a, a->mx, a->my) : -1;
    if (hover >= 0 && hover != a->press_sq && is_target(a, hover, NULL)) {
        Rect r = square_rect(a, hover);
        tiny_set_color(a->ren, TINY_INK);
        frame(a->ren, r, 2 * u);
    }

    for (int sq = 0; sq < 64; sq++) {
        if (!p->sq[sq]) continue;
        Rect r = square_rect(a, sq);
        bool lifted = a->dragging && sq == a->press_sq;
        pieces_draw(&a->pieces, a->ren, p->sq[sq], r.x, r.y, r.w, lifted ? 90 : 255);
    }
    for (int sq = 0; sq < 64; sq++) {
        bool capture = false;
        if (sq != a->selected && is_target(a, sq, &capture)) draw_target(a, sq, capture);
    }

    /* Review: the played move's grade on its square. */
    if (a->review && !a->exploring && a->view > 0 && analysed(a) > a->view) {
        MoveClass c = a->classes[a->view - 1];
        const char *sym = analysis_class_glyph(c);
        if (c != CLASS_NONE) {
            Rect r = square_rect(a, last.to);
            int s = TINY_CAPTION * u * (r.w >= 48 * u ? 2 : 1);
            int w = tiny_text_width(sym, s, TINY_BOLD) + 4 * u, h = tiny_text_height(s) + 4 * u;
            int x = r.x + r.w - w - u, y = r.y + u;
            tiny_frame_round(a->ren, x, y, w, h, TINY_RADIUS_SMALL, u, TINY_CORNERS, TINY_INK, TINY_PAPER);
            text(a, sym, x + 2 * u, y + 2 * u, s, TINY_BOLD, TINY_PAPER);
        }
    }

    Move best = shown_best(a);
    if (!move_is_none(best)) draw_arrow(a, best.from, best.to, TINY_INK, 0xA0);
    for (int i = 0; i < a->n_arrows; i++) draw_arrow(a, a->arrows[i].from, a->arrows[i].to, TINY_DIM, 0xC0);
    int rhover = a->rpress_sq >= 0 ? square_at(a, a->mx, a->my) : -1;
    if (rhover >= 0 && rhover != a->rpress_sq) draw_arrow(a, a->rpress_sq, rhover, TINY_DIM, 0xC0);

    if (a->dragging) {
        int s = a->lay.sq;
        pieces_draw(&a->pieces, a->ren, p->sq[a->press_sq], a->mx - s / 2, a->my - s / 2, s, 255);
    }

    if (a->promo_to >= 0) {
        Rect first = promo_cell(a, 0), last_cell = promo_cell(a, 3);
        int y = SDL_min(first.y, last_cell.y);
        tiny_panel(a->ren, first.x, y, first.w, 4 * first.h, u);
        for (int i = 0; i < 4; i++) {
            Rect r = promo_cell(a, i);
            if (in_rect(r, a->mx, a->my)) {
                tiny_set_color(a->ren, TINY_SHADE);
                tiny_fill_rect(a->ren, r.x + u, r.y + u, r.w - 2 * u, r.h - 2 * u);
            }
            pieces_draw(&a->pieces, a->ren, PIECE(p->side, PROMO_TYPES[i]), r.x, r.y, r.w, 255);
        }
    }
}

/* Material each side has taken, as piece counts, and the material lead. */
static const int START_COUNT[7] = {0, 8, 2, 2, 2, 1, 1};
static const int POINTS[7] = {0, 1, 3, 3, 5, 9, 0};

static void draw_player(App *a, Rect strip, int color) {
    const Position *p = viewed(a);
    int u = a->lay.u, body = TINY_BODY * u, cap = TINY_CAPTION * u;
    int count[2][7] = {{0}};
    for (int sq = 0; sq < 64; sq++)
        if (p->sq[sq]) count[PCOLOR(p->sq[sq])][PTYPE(p->sq[sq])]++;
    int lead = 0;
    for (int t = PAWN; t <= QUEEN; t++) lead += POINTS[t] * (count[color][t] - count[color ^ 1][t]);

    /* Your clock, at the right end: inverted while it runs. */
    int right = strip.x + strip.w;
    if (color == a->user && a->clock_ms && !a->demo) {
        char t[16];
        clock_text(a, t, sizeof t);
        int title = TINY_TITLE * u, th = tiny_text_height(title) + 6 * u;
        int tw = SDL_max(tiny_text_width(t, title, TINY_BOLD), tiny_text_width("00:00", title, TINY_BOLD)) + 8 * u;
        int cx = right - tw, cy = strip.y + (strip.h - th) / 2;
        bool on = a->clock_since != 0;
        tiny_frame_round(a->ren, cx, cy, tw, th, TINY_RADIUS_SMALL, u, TINY_CORNERS, on ? TINY_INK : TINY_PAPER, TINY_INK);
        int w = tiny_text_width(t, title, TINY_BOLD);
        text(a, t, cx + tw - 4 * u - w, cy + 3 * u, title, TINY_BOLD, on ? TINY_PAPER : TINY_INK);
        right = cx - TINY_PAD * u;
    }

    /* Avatar: square pixel content in a 1-unit ink frame. */
    int box = strip.h, x = strip.x;
    tiny_set_color(a->ren, TINY_INK);
    tiny_fill_rect(a->ren, x, strip.y, box, box);
    pieces_draw_avatar(&a->pieces, a->ren, is_bot(a, color) ? AVATAR_BOT : AVATAR_YOU, x + u, strip.y + u, box - 2 * u);
    x += box + TINY_PAD * u;

    /* First line: the name (inverted while it's their move), with a BOT tag for the bot. */
    int ty = strip.y + 2 * u;
    bool to_move = a->started && !game_over(&a->game) && live(a) && p->side == color && !a->review;
    if (is_bot(a, color)) {
        int tw = tiny_text_width("BOT", cap, TINY_BOLD) + 4 * u, th = tiny_text_height(body);
        tiny_frame_round(a->ren, x, ty, tw, th, TINY_RADIUS_SMALL, u, TINY_CORNERS, TINY_PAPER, TINY_INK);
        text(a, "BOT", x + 2 * u, ty + (th - tiny_text_height(cap)) / 2, cap, TINY_BOLD, TINY_INK);
        x += tw + TINY_GAP * u + 2 * u;
    }
    const char *name = player_name(a, color);
    int nw = tiny_text_width(name, body, TINY_BOLD);
    if (to_move) {
        tiny_set_color(a->ren, TINY_INK);
        tiny_fill_round(a->ren, x - 2 * u, ty - 2 * u, nw + 4 * u, tiny_text_height(body) + 4 * u, TINY_RADIUS_SMALL,
                        u, TINY_CORNERS);
    }
    text(a, name, x, ty, body, TINY_BOLD, to_move ? TINY_PAPER : TINY_INK);
    if (is_bot(a, color) && bot_level(a, color))
        text(a, bot_level(a, color)->name, x + nw + TINY_PAD * u, ty, body, TINY_PLAIN, TINY_DIM);

    /* Second line: accuracy in review, else the pieces they've taken and their material lead. */
    x = strip.x + box + TINY_PAD * u;
    int mini = 16 * u, step = 8 * u, cy = strip.y + strip.h - mini / 2 - u; /* 16u: whole-pixel scale on 2x */
    if (a->review) {
        char acc[32];
        if (analysis_finished(a)) snprintf(acc, sizeof acc, "Accuracy %.1f", a->accuracy[color]);
        else snprintf(acc, sizeof acc, "Accuracy …");
        text(a, acc, x, cy - tiny_text_height(body) / 2, body, TINY_PLAIN, TINY_DIM);
        return;
    }
    for (int t = QUEEN; t >= PAWN; t--) {
        int taken = SDL_max(START_COUNT[t] - count[color ^ 1][t], 0);
        if (!taken) continue;
        for (int i = 0; i < taken && x + mini < right; i++, x += step)
            pieces_draw(&a->pieces, a->ren, PIECE(color ^ 1, t), x, cy - mini / 2, mini, 255);
        x += mini - step + u;
    }
    if (lead > 0) {
        char buf[8];
        snprintf(buf, sizeof buf, "+%d", lead);
        text(a, buf, x + TINY_GAP * u, cy - tiny_text_height(cap) / 2, cap, TINY_PLAIN, TINY_DIM);
    }
}

/* "You won", "Tiny won" or "Draw". */
static const char *result_title(const App *a) {
    const Game *g = &a->game;
    if (g->result == RESULT_DRAW) return "Draw";
    int winner = g->result == RESULT_WHITE ? WHITE : BLACK;
    if (a->demo) return winner == WHITE ? "White won" : "Black won";
    return winner == a->user ? "You won" : "You lost";
}

static void status_lines(const App *a, char *l1, char *l2, size_t n) {
    const Game *g = &a->game;
    *l2 = 0;
    if (!a->started) {
        snprintf(l1, n, "New game");
        snprintf(l2, n, "Pick an opponent to start");
        return;
    }
    if (a->review) {
        snprintf(l1, n, "Game review");
        int done = analysed(a);
        if (a->exploring) snprintf(l2, n, "Playing out a line · Esc returns");
        else if (!analysis_finished(a)) snprintf(l2, n, "Analyzing… %d/%d", done, g->ply + 1);
        else if (a->opening[g->ply]) snprintf(l2, n, "%s", a->opening[g->ply]);
        else snprintf(l2, n, "← → step through moves");
        return;
    }
    if (game_over(g) && a->demo) {
        snprintf(l1, n, "%s · %s", result_title(a), game_end_string(g->end));
        int secs = a->demo_next ? (int)((a->demo_next - SDL_min(SDL_GetTicksNS(), a->demo_next)) / SDL_NS_PER_SECOND) + 1 : 0;
        snprintf(l2, n, "Next game in %ds", secs);
        return;
    } else if (game_over(g)) {
        snprintf(l1, n, "%s", result_title(a));
        snprintf(l2, n, "%s · %s", game_end_string(g->end), game_result_string(g->result));
    } else if (a->demo) {
        snprintf(l1, n, "%s to move", color_name(game_pos(g)->side));
        snprintf(l2, n, "%s", live(a) ? "Bots play each other" : "Viewing an earlier move");
        return;
    } else if (bot_turn(a)) {
        snprintf(l1, n, "%s is thinking…", a->bots[a->user ^ 1]->name);
    } else {
        snprintf(l1, n, "Your move");
    }
    if (!live(a) && !game_over(g)) snprintf(l2, n, "Viewing an earlier move");
    else if (!game_over(g)) snprintf(l2, n, "You play %s", color_name(a->user));
}

/* The explored line up to the shown move, "12...Nf6 13.Bxf7+ Kxf7", dropping
   its first moves (for "…") until it fits in `w`. */
static void line_text(const App *a, int w, int scale, char *out, size_t n) {
    for (int first = a->var_base; first < a->var_view; first++) {
        size_t k = (size_t)snprintf(out, n, "%s", first > a->var_base ? "…" : "");
        for (int i = first; i < a->var_view && k < n; i++) {
            const Position *p = &a->var.pos[i];
            if (p->side == WHITE) k += (size_t)snprintf(out + k, n - k, "%s%d.%s", i > first ? " " : "", p->fullmove, a->var.san[i]);
            else if (i == first) k += (size_t)snprintf(out + k, n - k, "%d...%s", p->fullmove, a->var.san[i]);
            else k += (size_t)snprintf(out + k, n - k, " %s", a->var.san[i]);
        }
        if (tiny_text_width(out, scale, TINY_PLAIN) <= w) return;
    }
}

static void draw_panel(App *a) {
    const Layout *L = &a->lay;
    const Game *g = &a->game;
    int u = L->u, body = TINY_BODY * u, cap = TINY_CAPTION * u, pad = TINY_PAD * u;
    Rect P = L->panel;
    tiny_panel(a->ren, P.x, P.y, P.w, P.h, u);
    int x = P.x + pad, y = P.y + pad, iw = P.w - 2 * pad;

    char buf[96], l1[96], l2[96];
    int opp = a->user ^ 1;
    snprintf(buf, sizeof buf, a->demo ? "Demo" : "vs %s", a->bots[opp]->name);
    int hw = text(a, buf, x, y, body, TINY_BOLD, TINY_INK);
    if (!a->demo && bot_level(a, opp)) {
        fit_text(bot_level(a, opp)->name, iw - hw - TINY_PAD * u, body, TINY_PLAIN, buf, sizeof buf);
        text(a, buf, x + hw + TINY_PAD * u, y, body, TINY_PLAIN, TINY_DIM);
    }
    y += tiny_line_height(body);
    fit_text(a->demo ? "Random bots, a new game each time" : bot_about(a, opp), iw, cap, TINY_PLAIN, buf, sizeof buf);
    text(a, buf, x, y, cap, TINY_PLAIN, TINY_DIM);
    y += tiny_line_height(cap) + pad;
    status_lines(a, l1, l2, sizeof l1);
    fit_text(l1, iw, body, TINY_PLAIN, buf, sizeof buf);
    text(a, buf, x, y, body, TINY_PLAIN, TINY_INK);
    y += tiny_line_height(body);
    fit_text(l2, iw, cap, TINY_PLAIN, buf, sizeof buf);
    text(a, buf, x, y, cap, TINY_PLAIN, TINY_DIM);

    tiny_set_color(a->ren, TINY_INK);
    tiny_fill_rect(a->ren, P.x, L->list.y - TINY_GAP * u, P.w, u);

    /* Move list: "12.  Nf3  Qxd5??", current move inverted. */
    int rows = list_rows(a), ty_off = (L->row_h - tiny_text_height(body)) / 2 - u;
    for (int row = 0; row < rows; row++) {
        int ply = (a->scroll + row) * 2;
        if (ply >= g->ply) break;
        int ry = L->list.y + row * L->row_h;
        snprintf(buf, sizeof buf, "%d.", a->scroll + row + 1);
        text(a, buf, L->list.x, ry + ty_off, body, TINY_PLAIN, TINY_DIM);
        for (int k = 0; k < 2 && ply + k < g->ply; k++) {
            int i = ply + k;
            Rect c = move_cell(a, i);
            const char *sym = a->review && analysed(a) > i + 1 ? analysis_class_symbol(a->classes[i]) : "";
            snprintf(buf, sizeof buf, "%s%s", g->san[i], sym);
            bool current = a->view == i + 1;
            int tw = tiny_text_width(buf, body, TINY_PLAIN);
            if (current) {
                tiny_set_color(a->ren, TINY_INK);
                tiny_fill_round(a->ren, c.x - 2 * u, c.y + u, tw + 4 * u, c.h - 2 * u, TINY_RADIUS_SMALL, u, TINY_CORNERS);
            } else if (in_rect(c, a->mx, a->my)) {
                tiny_set_color(a->ren, TINY_SHADE);
                tiny_fill_round(a->ren, c.x - 2 * u, c.y + u, tw + 4 * u, c.h - 2 * u, TINY_RADIUS_SMALL, u, TINY_CORNERS);
            }
            text(a, buf, c.x, c.y + ty_off, body, TINY_PLAIN, current ? TINY_PAPER : TINY_INK);
        }
    }
    if (g->ply == 0) text(a, "No moves yet", L->list.x, L->list.y + ty_off, body, TINY_PLAIN, TINY_GRAY);

    /* Review: what the current move was, and what the engine wanted. */
    if (a->review && L->info.h > 0) {
        tiny_set_color(a->ren, TINY_INK);
        tiny_fill_rect(a->ren, P.x, L->info.y, P.w, u);
        int iy = L->info.y + pad;
        *l1 = *l2 = 0;
        /* Line 1: the move on the left, its grade in its own right-aligned column. */
        const char *grade = "";
        char eval[16];
        if (a->exploring) {
            snprintf(l1, sizeof l1, "Your line");
            if (a->var_known[a->var_view]) analysis_format(&a->var_evals[a->var_view], eval, sizeof eval);
            else snprintf(eval, sizeof eval, "…");
            grade = eval;
            line_text(a, iw, body, l2, sizeof l2);
        } else if (a->view == 0) {
            snprintf(l1, sizeof l1, "Starting position");
        } else if (analysed(a) > a->view) {
            int i = a->view - 1;
            snprintf(l1, sizeof l1, "%s", g->san[i]);
            grade = analysis_class_name(a->classes[i]);
            Move best = shown_best(a);
            if (a->classes[i] == CLASS_BOOK) {
                snprintf(l2, sizeof l2, "%s", a->opening[i + 1] ? a->opening[i + 1] : "A known opening move");
            } else if (!move_is_none(best)) {
                char san[8];
                move_san(&g->pos[i], best, san);
                snprintf(l2, sizeof l2, "Best was %s", san);
            } else if (move_is_none(better_move(a)) && !move_is_none(a->evals[i].best)) {
                snprintf(l2, sizeof l2, "The engine's choice too");
            }
        } else {
            snprintf(l1, sizeof l1, "Analyzing…");
        }
        int gw = *grade ? tiny_text_width(grade, body, TINY_BOLD) : 0;
        if (gw) text(a, grade, x + iw - gw, iy, body, TINY_BOLD, TINY_INK);
        fit_text(l1, iw - (gw ? gw + TINY_PAD * u : 0), body, TINY_BOLD, buf, sizeof buf);
        text(a, buf, x, iy, body, TINY_BOLD, TINY_INK);
        fit_text(l2, iw, body, TINY_PLAIN, buf, sizeof buf);
        text(a, buf, x, iy + tiny_line_height(body), body, TINY_PLAIN, TINY_DIM);

        if (a->exploring) button(a, BTN_BEST, "Back to game", true);
        else button_on(a, BTN_BEST, a->show_best ? "Hide best" : "Show best", button_enabled(a, BTN_BEST), a->show_best);
    }

    button(a, BTN_PREV, "‹", button_enabled(a, BTN_PREV));
    button(a, BTN_NEXT, "›", button_enabled(a, BTN_NEXT));
    button(a, BTN_NEW, "New", true);
    button(a, BTN_FLIP, "Flip", true);
    button(a, BTN_UNDO, "Undo", button_enabled(a, BTN_UNDO));
    if (game_over(g)) button(a, BTN_END, "Review", button_enabled(a, BTN_END));
    else button(a, BTN_END, "Resign", button_enabled(a, BTN_END));
}

static void draw_dialog(App *a) {
    const Layout *L = &a->lay;
    Rect d = L->dialog;
    int u = L->u, pad = TINY_PAD * u;
    tiny_panel(a->ren, d.x, d.y, d.w, d.h, u);
    const Game *g = &a->game;
    const char *title = result_title(a);
    int cx = d.x + d.w / 2, y = d.y + pad;
    tiny_set_color(a->ren, TINY_INK);
    tiny_text_centered(&a->font, title, cx, y, TINY_TITLE * u, TINY_BOLD);
    y += tiny_line_height(TINY_TITLE * u);
    char buf[64];
    snprintf(buf, sizeof buf, "%s · %s", game_end_string(g->end), game_result_string(g->result));
    tiny_set_color(a->ren, TINY_DIM);
    tiny_text_centered(&a->font, buf, cx, y, TINY_CAPTION * u, TINY_PLAIN);
    button(a, BTN_DLG_REVIEW, "Review", true);
    button(a, BTN_DLG_NEW, "New", true);
}

static void draw_setup(App *a) {
    const Layout *L = &a->lay;
    Rect d = L->setup;
    int u = L->u, pad = TINY_PAD * u, cap = TINY_CAPTION * u;
    tiny_panel(a->ren, d.x, d.y, d.w, d.h, u);
    tiny_set_color(a->ren, TINY_INK);
    tiny_text_centered(&a->font, "New game", d.x + d.w / 2, d.y + pad, TINY_TITLE * u, TINY_BOLD);
    text(a, "Opponent", d.x + pad, L->setup_label[0], cap, TINY_PLAIN, TINY_DIM);
    for (int i = 0; i < SDL_min(chess_bot_count, MAX_PICK); i++)
        button_on(a, BTN_PICK_BOT + i, chess_bots[i]->name, true, i == a->pick_bot);
    const ChessBot *b = chess_bots[a->pick_bot];
    const char *about = b->about;
    if (b->levels) {
        text(a, "Strength", d.x + pad, L->setup_label[1], cap, TINY_PLAIN, TINY_DIM);
        for (int i = 0; i < SDL_min(b->level_count, MAX_PICK); i++)
            button_on(a, BTN_PICK_LEVEL + i, b->levels[i].name, true, i == a->pick_level);
        about = b->levels[a->pick_level].about;
    }
    if (play_resigns(a)) about = "Play resigns your game with the Student";
    char buf[96];
    fit_text(about, d.w - 2 * pad, cap, TINY_PLAIN, buf, sizeof buf);
    text(a, buf, d.x + pad, L->setup_label[2], cap, TINY_PLAIN, TINY_DIM);
    button(a, BTN_CANCEL, "Cancel", a->started);
    button(a, BTN_PLAY, "Play", true);
}

static void render(App *a) {
    layout(a);
    clamp_scroll(a);
    SDL_Renderer *r = a->ren;
    tiny_set_color(r, TINY_PAPER);
    SDL_RenderClear(r);
    const Layout *L = &a->lay;
    int bottom = a->flipped ? BLACK : WHITE;

    if (a->review) {
        const PositionEval *e = analysed(a) > a->view ? &a->evals[a->view] : NULL; /* even until analysed */
        if (a->exploring) e = a->var_known[a->var_view] ? &a->var_evals[a->var_view] : NULL;
        evalbar_draw(r, &a->font, L->evalbar.x, L->evalbar.y, L->evalbar.w, L->evalbar.h, L->u, e, !a->flipped);
    }
    draw_player(a, L->top, bottom ^ 1);
    draw_player(a, L->bottom, bottom);
    draw_board(a);
    tiny_set_color(r, TINY_INK);
    frame(r, (Rect){L->board.x - L->u, L->board.y - L->u, L->board.w + 2 * L->u, L->board.h + 2 * L->u}, L->u);
    draw_panel(a);
    if (a->dialog) draw_dialog(a);
    if (a->setup) draw_setup(a);
    tiny_present(r);
}

/* ------------------------------------------------------------------ */
/* Input. */

static bool can_move(const App *a) {
    if (!a->started || a->setup) return false;
    if (a->review) return true; /* plays out a line from the shown position, either side */
    return !game_over(&a->game) && live(a) && !bot_turn(a);
}

/* Plays from -> to, asking for the promotion piece when needed. */
static void try_move(App *a, int from, int to) {
    const Position *p = viewed(a);
    Move m = pos_find_move(p, from, to, 0);
    if (move_is_none(m)) return;
    if (m.promo) {
        a->promo_from = from;
        a->promo_to = to;
        a->selected = -1;
        return;
    }
    user_move(a, m);
}

static void toggle_arrow(App *a, int from, int to) {
    for (int i = 0; i < a->n_arrows; i++)
        if (a->arrows[i].from == from && a->arrows[i].to == to) {
            a->arrows[i] = a->arrows[--a->n_arrows];
            return;
        }
    if (a->n_arrows < MAX_ARROWS) a->arrows[a->n_arrows++] = (Arrow){from, to};
}

static void press_button(App *a, Button b) {
    switch (b) {
    case BTN_PREV: step(a, -1); break;
    case BTN_NEXT: step(a, 1); break;
    case BTN_NEW:
    case BTN_DLG_NEW:
        if (a->demo) demo_game(a);
        else open_setup(a);
        break;
    case BTN_PLAY: play(a); break;
    case BTN_CANCEL: a->setup = false; break;
    case BTN_FLIP:
        a->flipped = !a->flipped;
        save_game(a);
        break;
    case BTN_UNDO: undo(a); break;
    case BTN_END:
        if (game_over(&a->game)) enter_review(a);
        else resign(a);
        break;
    case BTN_DLG_REVIEW: enter_review(a); break;
    case BTN_BEST:
        if (a->exploring) stop_exploring(a);
        else a->show_best = !a->show_best;
        break;
    default:
        if (b >= BTN_PICK_BOT && b < BTN_PICK_BOT + MAX_PICK) pick_bot(a, b - BTN_PICK_BOT);
        else if (b >= BTN_PICK_LEVEL && b < BTN_PICK_LEVEL + MAX_PICK) pick_level(a, b - BTN_PICK_LEVEL);
        break;
    }
}

static bool button_enabled(const App *a, Button b) {
    bool picker = b >= BTN_PICK_BOT && b <= BTN_CANCEL;
    if (a->setup != picker) return false; /* the picker is modal */
    if (b == BTN_CANCEL) return a->started;
    switch (b) {
    case BTN_PREV: return a->exploring || a->view > 0;
    case BTN_NEXT: return a->exploring ? a->var_view < a->var.ply : a->view < a->game.ply;
    case BTN_UNDO: return !a->demo && !a->review && a->game.ply > 0 && a->game.end != END_TIME && !vs_student(a);
    case BTN_END: return !a->demo && !a->review;
    case BTN_DLG_REVIEW:
    case BTN_DLG_NEW: return a->dialog;
    case BTN_BEST: return a->exploring || !move_is_none(better_move(a));
    default: return true;
    }
}

static int button_at(const App *a, int x, int y) {
    /* The dialog is modal over the board, so its buttons win. */
    for (int b = BTN_COUNT - 1; b >= 0; b--)
        if (in_rect(a->lay.btn[b], x, y) && button_enabled(a, (Button)b)) return b;
    return -1;
}

static void left_down(App *a, int x, int y) {
    layout(a);
    a->pressed_btn = button_at(a, x, y);
    if (a->pressed_btn >= 0) return;
    if (a->setup) {
        if (a->started && !in_rect(a->lay.setup, x, y)) a->setup = false;
        return;
    }
    if (a->dialog) {
        if (!in_rect(a->lay.dialog, x, y)) a->dialog = false;
        return;
    }
    for (int ply = 0; ply < a->game.ply; ply++)
        if (in_rect(move_cell(a, ply), x, y)) {
            go_to(a, ply + 1);
            return;
        }

    if (a->promo_to >= 0) {
        for (int i = 0; i < 4; i++)
            if (in_rect(promo_cell(a, i), x, y)) {
                Move m = pos_find_move(viewed(a), a->promo_from, a->promo_to, PROMO_TYPES[i]);
                a->promo_from = a->promo_to = -1;
                user_move(a, m);
                return;
            }
        a->promo_from = a->promo_to = -1;
        return;
    }

    int sq = square_at(a, x, y);
    if (sq < 0) {
        a->selected = -1;
        return;
    }
    clear_annotations(a);
    if (!live(a) && !a->review) { /* clicking the board returns to the game */
        go_to(a, a->game.ply);
        return;
    }
    if (!can_move(a)) return;
    if (a->selected >= 0 && is_target(a, sq, NULL)) {
        try_move(a, a->selected, sq);
        return;
    }
    uint8_t pc = viewed(a)->sq[sq];
    if (pc && PCOLOR(pc) == viewed(a)->side) { /* can_move: that's you, or either side in review */
        a->press_reselect = a->selected == sq;
        a->selected = a->press_sq = sq;
        a->press_x = x;
        a->press_y = y;
    } else {
        a->selected = -1;
    }
}

static void left_up(App *a, int x, int y) {
    if (a->pressed_btn >= 0) {
        int b = a->pressed_btn;
        a->pressed_btn = -1;
        if (button_at(a, x, y) == b) press_button(a, (Button)b);
        return;
    }
    if (a->press_sq < 0) return;
    int from = a->press_sq;
    a->press_sq = -1;
    if (a->dragging) {
        a->dragging = false;
        int to = square_at(a, x, y);
        if (to >= 0 && to != from && is_target(a, to, NULL)) try_move(a, from, to);
        return;
    }
    if (a->press_reselect) a->selected = -1; /* a second click on the same piece puts it down */
}

static void motion(App *a, int x, int y) {
    a->mx = x;
    a->my = y;
    if (a->press_sq >= 0 && !a->dragging) {
        int slop = 3 * a->lay.u;
        if (abs(x - a->press_x) > slop || abs(y - a->press_y) > slop) a->dragging = true;
    }
}

static void right_down(App *a, int x, int y) {
    if (a->dragging || a->press_sq >= 0) { /* a right click cancels a drag */
        a->dragging = false;
        a->press_sq = -1;
        return;
    }
    a->rpress_sq = square_at(a, x, y);
}

static void right_up(App *a, int x, int y) {
    int from = a->rpress_sq, to = square_at(a, x, y);
    a->rpress_sq = -1;
    if (from < 0 || to < 0) return;
    if (from == to) a->marks ^= 1ull << from;
    else toggle_arrow(a, from, to);
}

static void escape(App *a) {
    if (a->setup) {
        if (a->started) a->setup = false;
    } else if (a->promo_to >= 0) a->promo_from = a->promo_to = -1;
    else if (a->dragging || a->press_sq >= 0) a->dragging = false, a->press_sq = -1;
    else if (a->selected >= 0) a->selected = -1;
    else if (a->marks || a->n_arrows) clear_annotations(a);
    else if (a->exploring) stop_exploring(a);
    else if (a->dialog) a->dialog = false;
}

/* Returns false to quit. */
static bool handle_event(App *a, SDL_Event *e) {
    float d = tiny_pixel_density(a->win, a->ren);
    if (e->type == a->ev_bot) {
        if (e->user.code != a->bot_gen || !a->bot_thread) return true;
        SDL_WaitThread(a->bot_thread, NULL);
        a->bot_thread = NULL;
        Move m = unpack_move((uint32_t)(uintptr_t)e->user.data1);
        bool following = live(a);
        if (game_play(&a->game, m)) {
            if (following) a->view = a->game.ply;
            ensure_visible(a);
            if (game_over(&a->game)) game_ended(a);
            else start_bot(a); /* --demo: the other bot replies (a no-op when it's your turn) */
            save_game(a);
        } else if (a->demo) {
            a->demo_next = SDL_GetTicksNS(); /* out of room for moves: start over */
        }
        return true;
    }
    if (e->type == a->ev_an) { /* redraw, and once it's all graded the Student learns from it */
        if (e->user.code == a->an_gen && analysis_finished(a)) learn(a);
        return true;
    }
    if (e->type == a->ev_var) {
        if (e->user.code != a->var_gen || !a->var_thread) return true;
        SDL_WaitThread(a->var_thread, NULL);
        a->var_thread = NULL;
        a->var_evals[a->var_ply] = a->var_result;
        a->var_known[a->var_ply] = true;
        start_var(a); /* the shown move may have changed meanwhile */
        return true;
    }

    switch (e->type) {
    case SDL_EVENT_QUIT: return false;
    case SDL_EVENT_KEY_DOWN: {
        const SDL_KeyboardEvent *k = &e->key;
        if (k->mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
            if (k->key == SDLK_Q || k->key == SDLK_W) return false;
            if (k->key == SDLK_N && !k->repeat) {
                if (a->demo) demo_game(a);
                else open_setup(a);
            }
            if (k->key == SDLK_Z && !k->repeat && !a->setup) undo(a);
            break;
        }
        if (a->setup) { /* the picker takes the keyboard */
            const ChessBot *b = chess_bots[a->pick_bot];
            switch (k->key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER: play(a); break;
            case SDLK_ESCAPE: escape(a); break;
            case SDLK_LEFT: pick_level(a, a->pick_level - 1); break;
            case SDLK_RIGHT: pick_level(a, SDL_min(a->pick_level + 1, b->level_count - 1)); break;
            case SDLK_UP: pick_bot(a, a->pick_bot - 1); break;
            case SDLK_DOWN: pick_bot(a, a->pick_bot + 1); break;
            default: break;
            }
            break;
        }
        switch (k->key) {
        case SDLK_LEFT: step(a, -1); break;
        case SDLK_RIGHT: step(a, 1); break;
        case SDLK_UP:
        case SDLK_HOME: go_to(a, 0); break;
        case SDLK_DOWN:
        case SDLK_END: go_to(a, a->game.ply); break;
        case SDLK_BACKSPACE:
            if (!k->repeat) undo(a);
            break;
        case SDLK_ESCAPE: escape(a); break;
        case SDLK_F:
            if (!k->repeat) a->flipped = !a->flipped, save_game(a);
            break;
        case SDLK_B:
            if (!k->repeat && a->review && !a->exploring) a->show_best = !a->show_best;
            break;
        case SDLK_R:
            if (!k->repeat && game_over(&a->game)) enter_review(a);
            break;
        default: break;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int x = (int)(e->button.x * d), y = (int)(e->button.y * d);
        motion(a, x, y);
        if (e->button.button == SDL_BUTTON_LEFT) left_down(a, x, y);
        else if (e->button.button == SDL_BUTTON_RIGHT) right_down(a, x, y);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int x = (int)(e->button.x * d), y = (int)(e->button.y * d);
        motion(a, x, y);
        if (e->button.button == SDL_BUTTON_LEFT) left_up(a, x, y);
        else if (e->button.button == SDL_BUTTON_RIGHT) right_up(a, x, y);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: motion(a, (int)(e->motion.x * d), (int)(e->motion.y * d)); break;
    case SDL_EVENT_MOUSE_WHEEL: {
        /* Trackpads send many fractional deltas: accumulate them, and take at most one step per event. */
        bool list = in_rect(a->lay.list, a->mx, a->my);
        float *acc = list ? &a->wheel_list : &a->wheel_board;
        if ((*acc > 0) != (e->wheel.y > 0)) *acc = 0; /* direction changed */
        *acc += e->wheel.y;
        if (fabsf(*acc) < 1) break;
        int dir = *acc > 0 ? 1 : -1;
        *acc = 0;
        if (list) {
            a->scroll -= dir;
            clamp_scroll(a);
        } else if (in_rect(a->lay.board, a->mx, a->my)) {
            step(a, -dir);
        }
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        a->mx = a->my = -1;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        a->dragging = false;
        a->press_sq = a->rpress_sq = a->pressed_btn = -1;
        break;
    default: break;
    }
    return true;
}

/* Dev aid: CHESS_MOVES="e2e4 e7e5 ..." plays moves at startup (user is White,
   nothing is loaded or saved), and CHESS_REVIEW=1 then opens the review with the analysis finished, so the
   first frame (see TINY_SCREENSHOT) shows it. In review, CHESS_VIEW=<ply> picks
   the shown move, CHESS_BEST=1 turns on Show best, and CHESS_LINE="g1f3 ..."
   plays out a line from there. */
static void apply_dev_env(App *a) {
    cancel_bot(a);
    a->setup = false;
    a->started = true;
    a->user = WHITE;
    a->flipped = false;
    game_play_uci(&a->game, getenv("CHESS_MOVES"));
    a->view = a->game.ply;
    if (game_over(&a->game)) game_ended(a);
    if (getenv("CHESS_REVIEW") && game_over(&a->game)) {
        enter_review(a);
        SDL_WaitThread(a->an_thread, NULL);
        a->an_thread = NULL;
        a->view = a->game.ply;
        if (getenv("CHESS_VIEW")) a->view = SDL_clamp(atoi(getenv("CHESS_VIEW")), 0, a->game.ply);
        a->show_best = getenv("CHESS_BEST") != NULL;
        for (const char *line = getenv("CHESS_LINE"); line && *line;) { /* explore it, evaluated */
            static Game probe; /* parses the next move */
            game_new(&probe, viewed(a));
            if (game_play_uci(&probe, line) == 0) break;
            explore_move(a, probe.moves[0]);
            if (!a->var_thread) break;
            SDL_WaitThread(a->var_thread, NULL);
            a->var_thread = NULL;
            a->var_evals[a->var_ply] = a->var_result;
            a->var_known[a->var_ply] = true;
            line = strchr(line, ' ');
            while (line && *line == ' ') line++;
        }
    }
    ensure_visible(a);
}

int main(int argc, char **argv) {
    static App app;
    App *a = &app;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) {
            a->demo = true;
            continue;
        }
        char *end = NULL;
        double min = strcmp(argv[i], "--timer") == 0 && i + 1 < argc ? strtod(argv[++i], &end) : 0;
        if (end && end != argv[i] && !*end && min >= 1.0 / 60 && min <= MAX_TIMER_MIN) {
            a->timer_ms = (int64_t)(min * 60000);
            continue;
        }
        fprintf(stderr, "usage: chess [--demo] [--timer <minutes>]\n"
                        "  --demo           bots play each other, forever\n"
                        "  --timer <min>    a clock for you in new games, e.g. 5 or 0.5 (up to %d)\n",
                MAX_TIMER_MIN);
        return 2;
    }
    a->bots[0] = a->bots[1] = chess_bots[0];
    a->levels[0] = a->levels[1] = a->pick_level = chess_bots[0]->default_level;
    trainer_init(&a->trainer); /* a beginner, unless there's a memory to load */
    a->clock_ms = a->timer_ms; /* for CHESS_MOVES; new and loaded games set their own */
    a->clock_left = a->clock_ms * 1000000;

    /* SDL_rand otherwise seeds itself with the raw performance counter, and its
       LCG barely moves the first draw for nearby seeds: your color stayed the
       same for a minute or more of launches. Scramble the seed (splitmix64). */
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    uint64_t seed = SDL_GetPerformanceCounter() ^ (uint64_t)now;
    seed = (seed ^ seed >> 30) * 0xbf58476d1ce4e5b9ull;
    seed = (seed ^ seed >> 27) * 0x94d049bb133111ebull;
    SDL_srand(seed ^ seed >> 31);

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    a->ev_bot = SDL_RegisterEvents(3);
    a->ev_an = a->ev_bot + 1;
    a->ev_var = a->ev_bot + 2;
    if (!tiny_create_window("Chess", 940, 680, 560, 420, &a->win, &a->ren) || !tiny_font_init(&a->font, a->ren) ||
        !pieces_init(&a->pieces, a->ren)) {
        SDL_Quit();
        return 1;
    }

    /* Resume the saved game, else start on an empty board under the new-game picker.
       CHESS_SAVE=<file> saves somewhere else (dev aid). */
    Position start;
    pos_start(&start);
    game_new(&a->game, &start);
    reset_input(a);
    a->setup = !a->demo;
    if (a->demo) {
        demo_game(a);
    } else if (getenv("CHESS_MOVES")) {
        apply_dev_env(a);
    } else {
        char *base = SDL_GetPrefPath("TinyApps", "Chess");
        if (getenv("CHESS_SAVE")) snprintf(a->save_path, sizeof a->save_path, "%s", getenv("CHESS_SAVE"));
        else if (base) snprintf(a->save_path, sizeof a->save_path, "%sgame.txt", base);
        if (base && !getenv("CHESS_SAVE")) {
            snprintf(a->trainer_path, sizeof a->trainer_path, "%strainer.txt", base);
            snprintf(a->log_path, sizeof a->log_path, "%sstudent_log.txt", base);
            snprintf(a->archive_path, sizeof a->archive_path, "%sgames.pgn", base);
        }
        SDL_free(base);
        load_trainer(a);
        load_game(a);
    }
    tick_clock(a); /* starts your clock if it's your move */
    bool running = true;
    render(a);
    while (running) {
        SDL_Event e;
        Sint32 wait = -1; /* ms, or -1 to wait for an event */
        if (a->demo_next) { /* wake each second for the countdown, then start the next game */
            uint64_t now = SDL_GetTicksNS(), left = a->demo_next > now ? a->demo_next - now : 0;
            wait = (Sint32)(SDL_min(left / SDL_NS_PER_MS, 1000) + 1);
        }
        if (a->clock_since) wait = wait < 0 ? clock_wait_ms(a) : SDL_min(wait, clock_wait_ms(a));
        bool got = wait < 0 ? SDL_WaitEvent(&e) : SDL_WaitEventTimeout(&e, wait);
        tick_clock(a);
        if (got) {
            running = handle_event(a, &e);
            while (running && SDL_PollEvent(&e)) running = handle_event(a, &e);
        }
        tick_clock(a);
        if (running && a->demo_next && SDL_GetTicksNS() >= a->demo_next) demo_game(a);
        if (running) render(a);
    }

    save_game(a);
    cancel_bot(a);
    cancel_analysis(a);
    cancel_var(a);
    pieces_free(&a->pieces);
    tiny_font_free(&a->font);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}
