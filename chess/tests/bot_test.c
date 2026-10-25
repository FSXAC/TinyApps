/* Bots: each one's personality, and that every bot on the roster plays legal moves. */
#include <math.h>
#include <string.h>

#include "analysis.h"
#include "board.h"
#include "bot.h"
#include "check.h"
#include "trainer.h"

static Game game; /* too big for the stack */

static Move ask(const ChessBot *b, const char *fen, int level) {
    Position p;
    CHECK(pos_from_fen(&p, fen));
    BotRequest req = {p, NULL, 0, 12345, NULL, level, {0, 0, 0}};
    return b->choose(&req);
}

static bool plays(const ChessBot *b, const char *fen, const char *uci) {
    char got[6];
    move_uci(ask(b, fen, b->default_level), got);
    if (strcmp(got, uci) != 0) printf("%s played %s, wanted %s (%s)\n", b->name, got, uci, fen);
    return strcmp(got, uci) == 0;
}

/* The reply to `moves` from the start. */
static Move reply(const ChessBot *b, const char *moves) {
    static uint64_t hist[GAME_MAX_PLY];
    Position start;
    pos_start(&start);
    game_new(&game, &start);
    game_play_uci(&game, moves);
    BotRequest req = {*game_pos(&game), hist, game_history(&game, hist), 99, NULL, b->default_level, {0, 0, 0}};
    return b->choose(&req);
}

static void test_tiny(void) {
    char uci[6];
    CHECK(bot_tiny.level_count == 3 && bot_tiny.levels);
    for (int lv = 0; lv < bot_tiny.level_count; lv++) {
        /* Mate in one. */
        move_uci(ask(&bot_tiny, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", lv), uci);
        CHECK(strcmp(uci, "a1a8") == 0);
        /* Takes a hanging queen. */
        move_uci(ask(&bot_tiny, "4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1", lv), uci);
        CHECK(strcmp(uci, "d2d5") == 0);
        /* Doesn't take a defended pawn with its queen. */
        move_uci(ask(&bot_tiny, "4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", lv), uci);
        CHECK(strcmp(uci, "d1d5") != 0);
    }
}

static void test_greedy(void) {
    /* The queen over a pawn, even though a pawn guards it. */
    CHECK(plays(&bot_greedy, "4k3/8/4p3/3q4/8/8/1p6/1R1RK3 w - - 0 1", "d1d5"));
    /* Between equal prizes, the cheaper piece takes. */
    CHECK(plays(&bot_greedy, "4k3/8/8/3r4/2P5/8/8/3QK3 w - - 0 1", "c4d5"));
    CHECK(plays(&bot_greedy, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", "a1a8")); /* mates */
}

static void test_mirror(void) {
    char uci[6];
    move_uci(reply(&bot_mirror, "e2e4"), uci);
    CHECK(strcmp(uci, "e7e5") == 0);
    move_uci(reply(&bot_mirror, "e2e4 e7e5 g1f3"), uci);
    CHECK(strcmp(uci, "g8f6") == 0);
    move_uci(reply(&bot_mirror, "e2e4 e7e5 g1f3 g8f6 f1c4 f8c5 e1g1"), uci);
    CHECK(strcmp(uci, "e8g8") == 0); /* castling mirrors too */
    /* As White it copies Black: 1.a3 e5 2.e4. */
    move_uci(reply(&bot_mirror, "a2a3 e7e5"), uci);
    CHECK(strcmp(uci, "e2e4") == 0);
}

static void test_queenie(void) {
    /* Gives check with the queen, and safely. */
    const char *fens[] = {"4k3/8/8/8/8/8/8/3QK3 w - - 0 1", "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
                          "4k3/3p1p2/8/8/8/8/8/3QK3 w - - 0 1"};
    for (int i = 0; i < 3; i++) {
        Position p, next;
        pos_from_fen(&p, fens[i]);
        Move m = ask(&bot_queenie, fens[i], 0);
        next = p;
        pos_make(&next, m);
        CHECK(PTYPE(p.sq[m.from]) == QUEEN);
        if (i != 1) CHECK(pos_in_check(&next));
        CHECK(pos_see(&p, m) >= 0);
    }
    /* Mates when it can, queen or not. */
    CHECK(plays(&bot_queenie, "6k1/5ppp/8/8/8/8/5PPP/R2Q2K1 w - - 0 1", "a1a8") ||
          plays(&bot_queenie, "6k1/5ppp/8/8/8/8/5PPP/R2Q2K1 w - - 0 1", "d1d8"));
}

static Move ask_slipping(const char *fen, BotSlips slips, uint64_t seed) {
    Position p;
    CHECK(pos_from_fen(&p, fen));
    BotRequest req = {p, NULL, 0, seed, NULL, 0, slips};
    return bot_student.choose(&req);
}

/* The expected-points loss of `m`, as the review would grade it. */
static MoveClass grade(const char *fen, Move m) {
    Position p, next;
    pos_from_fen(&p, fen);
    next = p;
    pos_make(&next, m);
    PositionEval before = analysis_evaluate(&p, NULL, 0, NULL), after = analysis_evaluate(&next, NULL, 0, NULL);
    return analysis_classify(&p, m, &before, &after);
}

static void test_student(void) {
    char uci[6];
    /* Never slipping, it plays like an engine: mates, takes a hanging queen. */
    CHECK(plays(&bot_student, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", "a1a8"));
    CHECK(plays(&bot_student, "4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1", "d2d5"));
    /* Always blundering, it plays a move the review calls bad: its search is
       shallower than the review's, so now and then it's only a Mistake. */
    const char *mid = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8";
    for (uint64_t seed = 1; seed <= 3; seed++) {
        Move m = ask_slipping(mid, (BotSlips){0, 0, 1}, seed);
        MoveClass c = grade(mid, m);
        move_uci(m, uci);
        bool bad = c == CLASS_BLUNDER || c == CLASS_MISS || c == CLASS_MISTAKE;
        if (!bad) printf("Student's blunder %s graded %s\n", uci, analysis_class_name(c));
        CHECK(bad);
    }
}

static void test_trainer(void) {
    Trainer t;
    trainer_init(&t);
    BotSlips beginner = trainer_slips(&t);
    CHECK(beginner.blunder > 0.05 && beginner.inaccuracy + beginner.mistake + beginner.blunder < 0.9);
    /* A clean, won game: your rates fall, the Student sharpens. */
    TrainerGame g = {42, 1, 20, {1, 0, 0}};
    CHECK(trainer_record(&t, &g));
    CHECK(!trainer_record(&t, &g)); /* once per game */
    CHECK(t.games == 1 && t.graded == 1 && t.wins == 1);
    BotSlips after = trainer_slips(&t);
    CHECK(after.blunder < beginner.blunder && after.mistake < beginner.mistake);
    /* A result alone moves only the edge; short games don't touch the rates. */
    double blunders = t.rate[2];
    TrainerGame lost = {43, 0, 3, {0, 0, 3}};
    CHECK(trainer_record(&t, &lost));
    CHECK(t.rate[2] == blunders && t.graded == 1 && t.losses == 1);
    /* Settles at your level, a step ahead: the Student slips less than you. */
    for (int i = 0; i < 40; i++) {
        TrainerGame d = {100 + (unsigned)i, 0.5, 30, {3, 3, 3}};
        trainer_record(&t, &d);
    }
    CHECK(t.rate[2] > 0.09 && t.rate[2] < 0.11);
    CHECK(trainer_slips(&t).blunder < t.rate[2]);
    /* Losing streaks soften it. */
    BotSlips level = trainer_slips(&t);
    for (int i = 0; i < 4; i++) {
        TrainerGame l = {200 + (unsigned)i, 0, 0, {0, 0, 0}};
        trainer_record(&t, &l);
    }
    CHECK(trainer_slips(&t).blunder > level.blunder);
    /* Text round trip; garbage reads as a beginner. */
    char text[512];
    trainer_format(&t, text, sizeof text);
    Trainer u;
    CHECK(trainer_parse(&u, text));
    CHECK(u.games == t.games && u.graded == t.graded && u.losses == t.losses && u.last_seed == t.last_seed);
    CHECK(fabs(u.rate[1] - t.rate[1]) < 1e-4 && fabs(u.edge - t.edge) < 1e-4);
    CHECK(!trainer_parse(&u, "nonsense"));
    CHECK(u.games == 0 && u.rate[2] == 0.15);
    /* Tallies the user's graded moves only, and not from lost positions. */
    MoveClass cls[6] = {CLASS_BOOK, CLASS_BOOK, CLASS_BLUNDER, CLASS_MISTAKE, CLASS_FORCED, CLASS_MISS};
    PositionEval ev[7] = {{0}};
    TrainerGame tg = {0};
    trainer_tally(&tg, cls, ev, 6, BLACK);
    CHECK(tg.moves == 2 && tg.slips[1] == 2 && tg.slips[2] == 0);
    trainer_tally(&tg, cls, ev, 6, WHITE);
    CHECK(tg.moves == 1 && tg.slips[2] == 1);
    ev[5].score = 900; /* Black is lost before the miss */
    trainer_tally(&tg, cls, ev, 6, BLACK);
    CHECK(tg.moves == 1 && tg.slips[1] == 1);
}

/* Every bot on the roster against every other: legal moves only. */
static void test_roster(void) {
    static uint64_t hist[GAME_MAX_PLY];
    CHECK(chess_bot_count >= 1 && chess_bot_count <= 5); /* the picker shows five */
    for (int w = 0; w < chess_bot_count; w++)
        for (int b = 0; b < chess_bot_count; b++) {
            const ChessBot *side[2] = {chess_bots[w], chess_bots[b]};
            Position start;
            pos_start(&start);
            game_new(&game, &start);
            /* The Student searches every move, so its games are kept short. */
            int plies = side[0] == &bot_student || side[1] == &bot_student ? 12 : 40;
            for (int i = 0; i < plies && !game_over(&game); i++) {
                const ChessBot *bot = side[game_pos(&game)->side];
                int lv = bot->levels ? i % bot->level_count : 0;
                BotRequest req = {*game_pos(&game), hist, game_history(&game, hist), (uint64_t)i * 7919, NULL, lv, {0.1, 0.1, 0.1}};
                Move m = bot->choose(&req);
                if (!game_play(&game, m)) printf("%s made an illegal move\n", bot->name);
                CHECK(game.ply == i + 1);
            }
        }
    /* A single legal move is always found. */
    for (int i = 0; i < chess_bot_count; i++) {
        char uci[6];
        move_uci(ask(chess_bots[i], "k7/8/8/8/8/8/8/1R5K b - - 0 1", 0), uci);
        CHECK(strcmp(uci, "a8a7") == 0);
    }
}

int main(void) {
    test_tiny();
    test_greedy();
    test_mirror();
    test_queenie();
    test_student();
    test_trainer();
    test_roster();
    return check_report();
}
