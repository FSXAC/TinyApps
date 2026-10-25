#include <string.h>

#include "board.h"
#include "book.h"
#include "check.h"

static long perft(const Position *p, int depth) {
    if (depth == 0) return 1;
    Move moves[MAX_MOVES];
    int n = pos_legal_moves(p, moves);
    if (depth == 1) return n;
    long total = 0;
    for (int i = 0; i < n; i++) {
        Position next = *p;
        pos_make(&next, moves[i]);
        total += perft(&next, depth - 1);
    }
    return total;
}

static long perft_fen(const char *fen, int depth) {
    Position p;
    if (!pos_from_fen(&p, fen)) return -1;
    return perft(&p, depth);
}

/* Standard perft suites: these exercise castling, en passant, promotion and pins. */
static void test_perft(void) {
    CHECK(perft_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4) == 197281);
    CHECK(perft_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3) == 97862);
    CHECK(perft_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5) == 674624);
    CHECK(perft_fen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4) == 422333);
    CHECK(perft_fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3) == 62379);
}

static void test_fen_roundtrip(void) {
    const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/8/8/3pP3/8/8/8/k6K w - d6 0 3",
    };
    for (int i = 0; i < 3; i++) {
        Position p;
        char out[100];
        CHECK(pos_from_fen(&p, fens[i]));
        pos_to_fen(&p, out, sizeof out);
        CHECK(strcmp(out, fens[i]) == 0);
    }
    Position p;
    CHECK(!pos_from_fen(&p, "8/8/8/8/8/8/8/8 w - - 0 1")); /* no kings */
    CHECK(!pos_from_fen(&p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1"));
    /* An en passant square nobody can use is dropped, so repetition isn't fooled. */
    CHECK(pos_from_fen(&p, "4k3/8/8/8/4P3/8/8/4K3 b - e3 0 1") && p.ep == -1);
}

static const char *san_of(const char *fen, const char *uci) {
    static char out[8];
    Position p;
    pos_from_fen(&p, fen);
    int promo = uci[4] ? (int)(strchr("..nbrq", uci[4]) - "..nbrq") : 0;
    Move m = pos_find_move(&p, SQ(uci[0] - 'a', uci[1] - '1'), SQ(uci[2] - 'a', uci[3] - '1'), promo);
    if (move_is_none(m)) return "(illegal)";
    move_san(&p, m, out);
    return out;
}

static void test_san(void) {
    const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    CHECK(strcmp(san_of(start, "e2e4"), "e4") == 0);
    CHECK(strcmp(san_of(start, "g1f3"), "Nf3") == 0);
    CHECK(strcmp(san_of("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1"), "O-O") == 0);
    CHECK(strcmp(san_of("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8"), "O-O-O") == 0);
    /* Disambiguation by file, then by rank. */
    CHECK(strcmp(san_of("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1", "b1d2"), "Nbd2") == 0);
    CHECK(strcmp(san_of("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1", "a1a3"), "R1a3") == 0);
    CHECK(strcmp(san_of("8/8/8/3pP3/8/8/8/k6K w - d6 0 3", "e5d6"), "exd6") == 0);
    CHECK(strcmp(san_of("8/4P3/8/8/8/8/8/k6K w - - 0 1", "e7e8q"), "e8=Q") == 0);
    CHECK(strcmp(san_of("k7/4P3/8/8/8/8/8/1R5K w - - 0 1", "e7e8r"), "e8=R+") == 0);
    CHECK(strcmp(san_of("4k3/8/8/8/8/8/8/R3K3 w - - 0 1", "a1a8"), "Ra8+") == 0);
}

static Game game; /* too big for the stack */

static void play(Game *g, const char *uci_moves) {
    const char *s = uci_moves;
    while (*s) {
        int promo = s[4] && s[4] != ' ' ? (int)(strchr("..nbrq", s[4]) - "..nbrq") : 0;
        Move m = pos_find_move(game_pos(g), SQ(s[0] - 'a', s[1] - '1'), SQ(s[2] - 'a', s[3] - '1'), promo);
        CHECK(game_play(g, m));
        s += promo ? 5 : 4;
        while (*s == ' ') s++;
    }
}

static void test_game_end(void) {
    Position start;
    pos_start(&start);

    game_new(&game, &start); /* fool's mate */
    play(&game, "f2f3 e7e5 g2g4 d8h4");
    CHECK(game.result == RESULT_BLACK && game.end == END_CHECKMATE);
    CHECK(strcmp(game.san[3], "Qh4#") == 0);
    CHECK(!game_play(&game, pos_find_move(game_pos(&game), SQ(0, 1), SQ(0, 2), 0)));
    game_undo(&game, 1);
    CHECK(!game_over(&game) && game.ply == 3);

    game_new(&game, &start); /* knights out and back twice */
    play(&game, "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1");
    CHECK(!game_over(&game));
    play(&game, "f6g8");
    CHECK(game.result == RESULT_DRAW && game.end == END_REPETITION);

    Position p;
    pos_from_fen(&p, "7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
    game_new(&game, &p);
    play(&game, "f7f8");
    CHECK(game.end == END_CHECKMATE); /* Kg6 covers h7 */
    game_new(&game, &p);
    play(&game, "f7g7");
    CHECK(game.end == END_CHECKMATE && game.result == RESULT_WHITE);
    game_new(&game, &p);
    play(&game, "f7f6");
    CHECK(!game_over(&game)); /* Qf6+ is only check: Kg8 */
    game_new(&game, &p);
    play(&game, "g6h6");
    CHECK(game.end == END_STALEMATE);

    pos_from_fen(&p, "4k3/8/8/8/8/8/4q3/4K3 w - - 0 1");
    game_new(&game, &p);
    play(&game, "e1e2");
    CHECK(game.end == END_MATERIAL);

    pos_from_fen(&p, "4k3/8/8/8/8/8/R7/4K3 w - - 99 60");
    game_new(&game, &p);
    play(&game, "a2a3");
    CHECK(game.end == END_FIFTY);

    game_new(&game, &start);
    game_resign(&game, WHITE);
    CHECK(game.result == RESULT_BLACK && game.end == END_RESIGN);
}

static Game game2;

static void test_uci_roundtrip(void) {
    Position start;
    pos_start(&start);
    game_new(&game, &start);
    const char *line = "e2e4 d7d5 e4d5 g8f6 d5d6 e7e5 d6c7 f8c5 c7d8q e8d8 e1e2"; /* captures, promotion */
    CHECK(game_play_uci(&game, line) == 11);
    char buf[GAME_MAX_PLY * 6];
    game_uci(&game, buf, sizeof buf);
    CHECK(strcmp(buf, line) == 0);
    game_new(&game2, &start);
    CHECK(game_play_uci(&game2, buf) == 11);
    CHECK(game_pos(&game2)->hash == game_pos(&game)->hash);

    game_new(&game2, &start); /* stops at the first bad move */
    CHECK(game_play_uci(&game2, "e2e4 e7e5 e4e5 d2d4") == 2);
    game_new(&game2, &start);
    CHECK(game_play_uci(&game2, "e2e4 zz e7e5") == 1);
    CHECK(game_play_uci(&game2, "") == 0);
    game_uci(&game, buf, 10); /* cut at a move boundary */
    CHECK(strcmp(buf, "e2e4") == 0);
}

static void test_insufficient(void) {
    Position p;
    pos_from_fen(&p, "4k3/8/8/8/8/8/8/4KB2 w - - 0 1");
    CHECK(pos_insufficient_material(&p));
    pos_from_fen(&p, "4kb2/8/8/8/8/8/8/4KB2 w - - 0 1"); /* f1 and f8 bishops: opposite colors */
    CHECK(!pos_insufficient_material(&p));
    pos_from_fen(&p, "4k1b1/8/8/8/8/8/8/4KB2 w - - 0 1"); /* f1 and g8: both light */
    CHECK(pos_insufficient_material(&p));
    pos_from_fen(&p, "4k3/8/8/8/8/8/8/3NKN2 w - - 0 1");
    CHECK(!pos_insufficient_material(&p));
}

static void test_see(void) {
    Position p;
    pos_from_fen(&p, "4k3/8/4p3/3q4/8/8/8/3RK3 w - - 0 1"); /* Rxd5 exd5: wins Q for R */
    CHECK(pos_see(&p, pos_find_move(&p, SQ(3, 0), SQ(3, 4), 0)) == 400);
    pos_from_fen(&p, "4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1"); /* Qxd5 cxd5: loses Q for P */
    CHECK(pos_see(&p, pos_find_move(&p, SQ(3, 0), SQ(3, 4), 0)) == -800);
    pos_from_fen(&p, "4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1"); /* free pawn */
    CHECK(pos_see(&p, pos_find_move(&p, SQ(3, 0), SQ(3, 4), 0)) == 100);
    pos_from_fen(&p, "4k3/8/8/8/2p5/8/8/3QK3 w - - 0 1"); /* Qd3 walks into cxd3 */
    CHECK(pos_see(&p, pos_find_move(&p, SQ(3, 0), SQ(3, 2), 0)) == -900);
}

static void test_pgn(void) {
    static Game g;
    static char out[GAME_MAX_PLY * 16];
    Position start;
    pos_start(&start);
    game_new(&g, &start);
    game_pgn(&g, out, sizeof out);
    CHECK(strcmp(out, "*") == 0);
    game_play_uci(&g, "f2f3 e7e5 g2g4 d8h4"); /* fool's mate */
    game_pgn(&g, out, sizeof out);
    CHECK(strcmp(out, "1. f3 e5 2. g4 Qh4# 0-1") == 0);
    /* Long games wrap, never past 79 columns, and a short buffer cuts at a move. */
    game_new(&g, &start);
    CHECK(game_play_uci(&g, "a2a3 a7a6 a3a4 a6a5 b2b3 b7b6 b3b4 b6b5 c2c3 c7c6 c3c4 c6c5 d2d3 d7d6 d3d4 d6d5 "
                            "e2e3 e7e6 e3e4 e6e5 f2f3 f7f6 f3f4 f6f5 g2g3 g7g6 g3g4 g6g5 h2h3 h7h6") == 30);
    game_pgn(&g, out, sizeof out);
    CHECK(strchr(out, '\n') != NULL);
    for (const char *l = out; l; l = strchr(l, '\n'), l = l ? l + 1 : NULL)
        CHECK((strchr(l, '\n') ? (size_t)(strchr(l, '\n') - l) : strlen(l)) <= 79);
    CHECK(strstr(out, "15. h3 h6 *") != NULL);
    game_pgn(&g, out, 12);
    CHECK(strcmp(out, "1. a3 a6") == 0);
}

static void test_timeout(void) {
    Position p;
    pos_from_fen(&p, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    game_new(&game, &p);
    game_timeout(&game, BLACK); /* White can still mate: a loss */
    CHECK(game.result == RESULT_WHITE && game.end == END_TIME);
    game_new(&game, &p);
    game_timeout(&game, WHITE); /* Black has a bare king: a draw */
    CHECK(game.result == RESULT_DRAW && game.end == END_TIME);
    game_undo(&game, 0);
    CHECK(!game_over(&game));
}

static void test_book(void) {
    /* Every line is legal from the start, and its end is a named book position. */
    for (int i = 0; i < book_line_count; i++) {
        Position p;
        pos_start(&p);
        const char *s = book_lines[i].moves;
        bool ok = true;
        while (*s && ok) {
            Move m = pos_find_move(&p, SQ(s[0] - 'a', s[1] - '1'), SQ(s[2] - 'a', s[3] - '1'), 0);
            ok = !move_is_none(m);
            if (ok) pos_make(&p, m);
            s += 4;
            while (*s == ' ') s++;
        }
        if (!ok) printf("book line %d (%s) is illegal near \"%s\"\n", i, book_lines[i].name, s - 4);
        CHECK(ok);
        const char *name = NULL;
        CHECK(book_lookup(&p, &name) && name);
    }
    Position p;
    pos_start(&p);
    CHECK(book_lookup(&p, NULL));
    Move e4 = pos_find_move(&p, SQ(4, 1), SQ(4, 3), 0), a4 = pos_find_move(&p, SQ(0, 1), SQ(0, 3), 0);
    CHECK(book_move(&p, e4));
    CHECK(!book_move(&p, a4));
    /* Transpositions count: 1.c4 e6 2.d4 Nf6 reaches 1.d4 Nf6 2.c4 e6 lines. */
    const char *seq[] = {"c2c4", "e7e6", "d2d4", "g8f6", "b1c3", "f8b4"};
    const char *name = NULL;
    for (int i = 0; i < 6; i++) pos_make(&p, pos_find_move(&p, SQ(seq[i][0] - 'a', seq[i][1] - '1'),
                                                          SQ(seq[i][2] - 'a', seq[i][3] - '1'), 0));
    CHECK(book_lookup(&p, &name) && name && strcmp(name, "Nimzo-Indian Defense") == 0);
}

int main(void) {
    test_perft();
    test_fen_roundtrip();
    test_san();
    test_game_end();
    test_insufficient();
    test_uci_roundtrip();
    test_see();
    test_timeout();
    test_pgn();
    test_book();
    return check_report();
}
