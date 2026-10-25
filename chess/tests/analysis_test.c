/* Analysis engine tests: mates and signs, eval swings, game-over positions,
   classification, accuracy and labels. Also prints the per-position time. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "analysis.h"
#include "check.h"

static Position fen(const char *s) {
    Position p;
    bool ok = pos_from_fen(&p, s);
    CHECK(ok);
    return p;
}

static PositionEval eval_fen(const char *s) {
    Position p = fen(s);
    return analysis_evaluate(&p, NULL, 0, NULL);
}

static bool is_move(Move m, const char *uci) {
    char buf[8];
    move_uci(m, buf);
    return strcmp(buf, uci) == 0;
}

static bool fmt_is(PositionEval e, const char *want) {
    char buf[16];
    analysis_format(&e, buf, sizeof buf);
    if (strcmp(buf, want)) fprintf(stderr, "format: got \"%s\", want \"%s\"\n", buf, want);
    return strcmp(buf, want) == 0;
}

static void test_mates(void) {
    /* Back-rank mate in 1, for White and (mirrored) for Black. */
    PositionEval e = eval_fen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
    CHECK(e.mate == 1);
    CHECK(e.score == EVAL_MATE_SCORE);
    CHECK(is_move(e.best, "d1d8"));
    CHECK(fmt_is(e, "M1"));

    e = eval_fen("3r2k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1");
    CHECK(e.mate == -1);
    CHECK(e.score == -EVAL_MATE_SCORE);
    CHECK(is_move(e.best, "d8d1"));
    CHECK(fmt_is(e, "-M1"));

    /* Rook ladder: mate in 2 (Ra7, then Rb8#), both colors. */
    e = eval_fen("7k/8/8/8/8/8/R7/1R4K1 w - - 0 1");
    CHECK(e.mate == 2);
    CHECK(e.score == EVAL_MATE_SCORE);
    e = eval_fen("1r4k1/r7/8/8/8/8/8/7K b - - 0 1");
    CHECK(e.mate == -2);
    CHECK(fmt_is(e, "-M2"));

    /* The side to move gets mated next move: White mates in 1, seen from Black. */
    e = eval_fen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 b - - 0 1");
    CHECK(e.mate == 0 || e.mate == 1); /* Black may have no defence, or ...h6 / ...g6 gives luft */
    CHECK(e.score > 300);

    /* Scholar's mate available. */
    e = eval_fen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");
    CHECK(e.mate == 1);
    CHECK(is_move(e.best, "h5f7"));
}

static void test_game_over(void) {
    /* Fool's mate: White is checkmated. */
    PositionEval e = eval_fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(e.mate == 0);
    CHECK(e.score == -EVAL_MATE_SCORE);
    CHECK(move_is_none(e.best));
    CHECK(analysis_is_decided(&e));
    CHECK(fmt_is(e, "0-1"));

    /* Black checkmated. */
    e = eval_fen("3R2k1/5ppp/8/8/8/8/5PPP/6K1 b - - 1 1");
    CHECK(e.mate == 0 && e.score == EVAL_MATE_SCORE && move_is_none(e.best));
    CHECK(fmt_is(e, "1-0"));

    /* Stalemate. */
    e = eval_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(e.mate == 0 && e.score == 0 && move_is_none(e.best));
    CHECK(fmt_is(e, "1/2"));

    /* Insufficient material. */
    e = eval_fen("8/8/4k3/8/8/3NK3/8/8 w - - 0 1");
    CHECK(e.score == 0 && move_is_none(e.best));

    /* Threefold: the current position already occurred twice before. */
    Position p = fen("4k3/8/8/8/8/8/8/Q3K3 w - - 0 1");
    uint64_t hist[4] = {p.hash, 1, p.hash, 2};
    e = analysis_evaluate(&p, hist, 4, NULL);
    CHECK(e.score == 0 && move_is_none(e.best));
    /* ...but once is not enough to end the game. */
    e = analysis_evaluate(&p, hist, 2, NULL);
    CHECK(e.score > 500 && !move_is_none(e.best));
}

static void test_repetition_draw(void) {
    /* White is a queen up, but every move it has repeats an earlier game
       position, which the search scores as a draw. */
    Position p = fen("4k3/8/8/8/8/8/8/Q3K3 w - - 60 40");
    Move moves[MAX_MOVES];
    uint64_t hist[MAX_MOVES];
    int n = pos_legal_moves(&p, moves);
    for (int i = 0; i < n; i++) {
        Position c = p;
        pos_make(&c, moves[i]);
        hist[i] = c.hash;
    }
    PositionEval e = analysis_evaluate(&p, hist, n, NULL);
    CHECK(e.score == 0 && e.mate == 0 && !move_is_none(e.best));
    e = analysis_evaluate(&p, NULL, 0, NULL);
    CHECK(e.score > 800);
}

static void test_eval_swing(void) {
    /* 1.e4 d5: roughly level. 2.Qg4?? hangs the queen to ...Bxg4. */
    const char *start = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    Position before = fen(start);
    PositionEval be = analysis_evaluate(&before, NULL, 0, NULL);
    CHECK(be.mate == 0);
    CHECK(be.score > -100 && be.score < 150);

    Move blunder = pos_find_move(&before, SQ(3, 0), SQ(6, 3), 0); /* Qd1-g4 */
    CHECK(!move_is_none(blunder));
    Position after = before;
    pos_make(&after, blunder);
    PositionEval ae = analysis_evaluate(&after, NULL, 0, NULL);
    CHECK(ae.score < -600);
    CHECK(is_move(ae.best, "c8g4"));

    CHECK(analysis_classify(&before, blunder, &be, &ae) == CLASS_BLUNDER);

    /* The engine's own move is Best. */
    Position best_after = before;
    pos_make(&best_after, be.best);
    PositionEval bae = analysis_evaluate(&best_after, NULL, 0, NULL);
    CHECK(analysis_classify(&before, be.best, &be, &bae) == CLASS_BEST);

    /* Accuracy: all-best play is 100; a blunder costs a lot. */
    PositionEval line[3] = {be, bae, bae};
    CHECK_NEAR(analysis_accuracy(line, 2, WHITE), 100, 1e-9);
    CHECK_NEAR(analysis_accuracy(line, 2, BLACK), 100, 1e-9);
    PositionEval bad[2] = {be, ae};
    double acc = analysis_accuracy(bad, 1, WHITE);
    CHECK(acc >= 0 && acc < 20);
    CHECK_NEAR(analysis_accuracy(bad, 1, BLACK), 100, 1e-9); /* Black hasn't moved */
}

static void test_classify(void) {
    /* Only one legal move: Forced. */
    Position p = fen("7k/8/8/8/8/8/6q1/7K w - - 0 1"); /* Kxg2 only */
    Move only = pos_find_move(&p, SQ(7, 0), SQ(6, 1), 0);
    PositionEval be = analysis_evaluate(&p, NULL, 0, NULL), ae = {0, 0, MOVE_NONE};
    CHECK(be.score == 0); /* K v K after the capture */
    CHECK(analysis_classify(&p, only, &be, &ae) == CLASS_FORCED);

    /* Missing a mate in 1 while staying clearly winning: Miss. */
    p = fen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
    be = analysis_evaluate(&p, NULL, 0, NULL);
    Move h3 = pos_find_move(&p, SQ(7, 1), SQ(7, 2), 0);
    Position after = p;
    pos_make(&after, h3);
    ae = analysis_evaluate(&after, NULL, 0, NULL);
    MoveClass c = analysis_classify(&p, h3, &be, &ae);
    printf("missed mate in 1 (h3): after %d cp -> %s\n", ae.score, analysis_class_name(c));
    CHECK(c == CLASS_MISS || c == CLASS_BLUNDER);

    /* Delivering mate is Best even if it isn't the engine's pick. */
    Move mate = pos_find_move(&p, SQ(3, 0), SQ(3, 7), 0);
    after = p;
    pos_make(&after, mate);
    ae = analysis_evaluate(&after, NULL, 0, NULL);
    PositionEval other = be;
    other.best = h3;
    CHECK(analysis_classify(&p, mate, &other, &ae) == CLASS_BEST);

    /* Thresholds from synthetic evals around a level position. */
    p = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Move e4 = pos_find_move(&p, SQ(4, 1), SQ(4, 3), 0), a3 = pos_find_move(&p, SQ(0, 1), SQ(0, 2), 0);
    be = (PositionEval){30, 0, e4};
    CHECK(analysis_classify(&p, e4, &be, &(PositionEval){25, 0, MOVE_NONE}) == CLASS_BEST);
    CHECK(analysis_classify(&p, a3, &be, &(PositionEval){20, 0, MOVE_NONE}) == CLASS_EXCELLENT);
    CHECK(analysis_classify(&p, a3, &be, &(PositionEval){-20, 0, MOVE_NONE}) == CLASS_GOOD);
    CHECK(analysis_classify(&p, a3, &be, &(PositionEval){-60, 0, MOVE_NONE}) == CLASS_INACCURACY);
    CHECK(analysis_classify(&p, a3, &be, &(PositionEval){-180, 0, MOVE_NONE}) == CLASS_MISTAKE);
    CHECK(analysis_classify(&p, a3, &be, &(PositionEval){-400, 0, MOVE_NONE}) == CLASS_BLUNDER);

    CHECK(strcmp(analysis_class_name(CLASS_BLUNDER), "Blunder") == 0);
    CHECK(strcmp(analysis_class_symbol(CLASS_BLUNDER), "??") == 0);
    CHECK(strcmp(analysis_class_symbol(CLASS_BEST), "") == 0);
    for (int i = 1; i < CLASS_COUNT; i++) CHECK(analysis_class_glyph((MoveClass)i)[0] != 0);
}

static void test_format(void) {
    CHECK(fmt_is((PositionEval){130, 0, {12, 28, 0, 0}}, "+1.3"));
    CHECK(fmt_is((PositionEval){-40, 0, {12, 28, 0, 0}}, "-0.4"));
    CHECK(fmt_is((PositionEval){-4, 0, {12, 28, 0, 0}}, "0.0"));
    CHECK(fmt_is((PositionEval){0, 0, {12, 28, 0, 0}}, "0.0"));
    CHECK(fmt_is((PositionEval){1234, 0, {12, 28, 0, 0}}, "+12.3"));
    CHECK(fmt_is((PositionEval){EVAL_MATE_SCORE, 3, {12, 28, 0, 0}}, "M3"));
    CHECK(fmt_is((PositionEval){-EVAL_MATE_SCORE, -2, {12, 28, 0, 0}}, "-M2"));
    CHECK(fmt_is((PositionEval){EVAL_MATE_SCORE, 0, MOVE_NONE}, "1-0"));
    CHECK(fmt_is((PositionEval){-EVAL_MATE_SCORE, 0, MOVE_NONE}, "0-1"));
}

static void test_stop_and_determinism(void) {
    const char *mid = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 9";
    Position p = fen(mid);
    volatile int stop = 1;
    PositionEval e = analysis_evaluate(&p, NULL, 0, &stop);
    CHECK(!move_is_none(e.best));
    PositionEval a = analysis_evaluate(&p, NULL, 0, NULL), b = analysis_evaluate(&p, NULL, 0, NULL);
    CHECK(a.score == b.score && a.mate == b.mate && move_eq(a.best, b.best));
}

static void bench(void) {
    static const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 9",
        "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
        "r2q1rk1/1b1nbppp/p2ppn2/1p6/3NP3/1BN1BP2/PPPQ2PP/2KR3R w - - 0 12",
        "2r2rk1/pp1q1ppp/2n1pn2/3p4/3P4/P1NBPN2/1P3PPP/R2Q1RK1 b - - 0 14",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "8/5pk1/6p1/3P3p/2K4P/6P1/8/8 w - - 0 45",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    };
    int n = (int)(sizeof fens / sizeof fens[0]);
    double total = 0, worst = 0;
    for (int i = 0; i < n; i++) {
        Position p = fen(fens[i]);
        clock_t t0 = clock();
        PositionEval e = analysis_evaluate(&p, NULL, 0, NULL);
        double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
        char s[16], m[8];
        analysis_format(&e, s, sizeof s);
        move_san(&p, e.best, m);
        printf("  %6.1f ms  %-6s %-6s %s\n", ms, s, m, fens[i]);
        total += ms;
        if (ms > worst) worst = ms;
    }
    printf("bench: %.1f ms average, %.1f ms worst\n", total / n, worst);
}

int main(void) {
    test_mates();
    test_game_over();
    test_repetition_draw();
    test_eval_swing();
    test_classify();
    test_format();
    test_stop_and_determinism();
    bench();
    return check_report();
}
