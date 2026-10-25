#include <string.h>

#include "check.h"
#include "games.h"
#include "sv.h"

static int count_outcomes(const SvGraph *g, int outcome) {
    int n = 0;
    for (int i = 0; i < g->nnodes; i++) n += g->nodes[i].outcome == outcome;
    return n;
}

static void test_ttt_full(void) {
    SvGraph g;
    CHECK(sv_init(&g, &sv_ttt, false));
    CHECK(sv_explore(&g, 1 << 20));
    sv_solve(&g);
    /* The well-known counts: 5,478 positions, 958 of them finished. */
    CHECK(g.nnodes == 5478);
    CHECK(g.nlayers == 10);
    static const int per_layer[10] = {1, 9, 72, 252, 756, 1260, 1520, 1140, 390, 78};
    for (int d = 0; d < 10; d++) CHECK(g.layer_start[d + 1] - g.layer_start[d] == per_layer[d]);
    CHECK(count_outcomes(&g, 0) == 626);
    CHECK(count_outcomes(&g, 1) == 316);
    CHECK(count_outcomes(&g, SV_NOBODY) == 16);
    CHECK(g.nodes[0].value == SV_NOBODY); /* perfect play draws */
    CHECK(g.nodes[0].nout == 9 && g.nodes[0].nin == 0);
    for (int i = 0; i < g.nnodes; i++) CHECK(g.nodes[i].value != SV_UNKNOWN);
    sv_free(&g);
}

static void test_ttt_folded(void) {
    SvGraph g;
    CHECK(sv_init(&g, &sv_ttt, true));
    CHECK(sv_explore(&g, 1 << 20));
    sv_solve(&g);
    CHECK(g.nnodes == 765);
    CHECK(count_outcomes(&g, 0) + count_outcomes(&g, 1) + count_outcomes(&g, SV_NOBODY) == 138);
    CHECK(g.nodes[0].nout == 3); /* corner, edge, center */
    CHECK(g.nodes[0].value == SV_NOBODY);
    sv_free(&g);
}

static void test_values(void) {
    SvGraph g;
    CHECK(sv_init(&g, &sv_ttt, false));
    CHECK(sv_explore(&g, 1 << 20));
    sv_solve(&g);
    /* X: a1 b1 (bottom row), O: a3 b3, X to move wins at c1 */
    uint8_t s[9] = {2, 2, 0, 0, 0, 0, 1, 1, 0};
    int n = sv_find(&g, s);
    CHECK(n >= 0 && g.nodes[n].mover == 0 && g.nodes[n].value == 0);
    CHECK(sv_value_for(g.nodes[n].value, 0) == 1 && sv_value_for(g.nodes[n].value, 1) == -1);
    /* X in two corners and O in the center... the fork after a corner opening:
       X a3, O b2 (center) still draws. */
    uint8_t t[9] = {1, 0, 0, 0, 2, 0, 0, 0, 0};
    n = sv_find(&g, t);
    CHECK(n >= 0 && g.nodes[n].value == SV_NOBODY);
    /* X a3, O b3 (edge next to it) loses for O. */
    uint8_t u[9] = {1, 2, 0, 0, 0, 0, 0, 0, 0};
    n = sv_find(&g, u);
    CHECK(n >= 0 && g.nodes[n].value == 0);
    sv_free(&g);
}

static void test_partial(void) {
    SvGraph g;
    CHECK(sv_init(&g, &sv_ttt, false));
    CHECK(!sv_explore(&g, 50)); /* stops at the budget */
    sv_solve(&g);
    CHECK(g.nodes[0].value == SV_UNKNOWN);
    sv_free(&g);
}

static void test_order(void) {
    SvGraph g;
    CHECK(sv_init(&g, &sv_ttt, true));
    CHECK(sv_explore(&g, 1 << 20));
    sv_solve(&g);
    sv_order(&g, true);
    for (int d = 0; d < g.nlayers; d++) {
        int a = g.layer_start[d], b = g.layer_start[d + 1], last = 0;
        for (int i = a; i < b; i++) {
            const SvNode *v = &g.nodes[g.layer[i]];
            CHECK(v->depth == d && v->pos == i - a);
            /* Grouped: X wins, then draws, then O wins. */
            int bucket = v->value == 0 ? 0 : v->value == SV_NOBODY ? 1 : 2;
            CHECK(bucket >= last);
            last = bucket;
        }
    }
    sv_free(&g);
}

static void test_paths(void) {
    SvGraph g;
    CHECK(sv_init(&g, &sv_ttt, true));
    CHECK(sv_explore(&g, 1 << 20));
    SvPath p;
    sv_path_reset(&g, &p);
    CHECK(p.n == 0 && p.node[0] == 0);
    CHECK(sv_path_push(&g, &p, 8)); /* X c1 */
    CHECK(!sv_path_push(&g, &p, 8)); /* taken */
    CHECK(sv_path_push(&g, &p, 4)); /* O b2 */
    /* The real board is kept, even though the node is a folded twin. */
    const uint8_t *s = sv_path_end(&p);
    CHECK(p.n == 2 && s[8] == 1 && s[4] == 2);
    CHECK(g.nodes[p.node[2]].depth == 2);
    int two = p.node[2];

    /* Jump somewhere else and back: the prefix holds. */
    CHECK(sv_path_push(&g, &p, 0));
    int three = p.node[3];
    CHECK(sv_path_pop(&p) && p.n == 2);
    CHECK(sv_path_to(&g, &p, three));
    CHECK(p.n == 3 && p.node[2] == two && p.node[3] == three);
    CHECK(sv_path_to(&g, &p, 0) && p.n == 0);
    CHECK(!sv_path_pop(&p));

    /* Unfold: the same real states map onto unfolded nodes. */
    CHECK(sv_path_push(&g, &p, 8) && sv_path_push(&g, &p, 4));
    SvGraph h;
    CHECK(sv_init(&h, &sv_ttt, false));
    CHECK(sv_explore(&h, 1 << 20));
    sv_path_refresh(&h, &p);
    CHECK(p.n == 2 && !memcmp(sv_state(&h, p.node[2]), sv_path_end(&p), 9));
    sv_free(&h);

    /* A finished game takes no more moves. */
    sv_path_reset(&g, &p);
    const SvMove win[] = {6, 0, 7, 1, 8};
    for (int i = 0; i < 5; i++) CHECK(sv_path_push(&g, &p, win[i]));
    CHECK(g.nodes[p.node[5]].outcome == 0);
    CHECK(!sv_path_push(&g, &p, 2));
    sv_free(&g);
}

static void test_move_names(void) {
    char buf[16];
    uint8_t s[9] = {0};
    sv_ttt.move_name(s, 6, buf, sizeof buf);
    CHECK(!strcmp(buf, "X a1"));
    s[6] = 1;
    sv_ttt.move_name(s, 2, buf, sizeof buf);
    CHECK(!strcmp(buf, "O c3"));
}

static void test_puzzle_back(void) {
    CHECK(sv_puzzle_set_date(10, 5));
    CHECK(!sv_puzzle_set_date(2, 30));
    SvGraph g;
    CHECK(sv_init(&g, &sv_puzzle, false));
    CHECK(sv_explore_back(&g, 1 << 22));
    sv_solve(&g);
    CHECK(g.closed);
    /* Every subset of a solution's pieces, and nothing else. */
    CHECK(g.nnodes == 20617);
    CHECK(g.nlayers == 10);
    CHECK(count_outcomes(&g, 0) == 44); /* Oct 5 has 44 solutions */
    for (int n = 0; n < g.nnodes; n++) CHECK(g.nodes[n].value == 0);
    for (int n = 0; n < g.nnodes; n++) {
        int placed = 0;
        for (int p = 0; p < 9; p++) placed += sv_puzzle_placed(sv_state(&g, n), p) >= 0;
        CHECK(g.nodes[n].depth == placed && g.nodes[n].nin == placed);
    }

    /* Moves off the graph are dead ends, and the path follows them there. */
    SvPath path;
    sv_path_reset(&g, &path);
    static SvMove moves[SV_MAX_MOVES];
    int k = sv_puzzle.moves(sv_path_end(&path), moves), live = 0, dead = -1;
    for (int i = 0; i < k; i++) {
        uint8_t t[SV_MAX_STATE] = {0};
        sv_puzzle.apply(sv_path_end(&path), moves[i], t);
        if (sv_state_value(&g, t) == 0) live++;
        else if (dead < 0) dead = i;
    }
    CHECK(live == g.nodes[0].nout && dead >= 0);
    CHECK(sv_path_push(&g, &path, moves[dead]));
    CHECK(path.n == 1 && path.node[1] == -1);
    CHECK(sv_state_value(&g, sv_path_end(&path)) == SV_NOBODY);
    /* Illegal: the same piece again, or onto the date. */
    CHECK(!sv_path_push(&g, &path, moves[dead]));
    sv_free(&g);
}

static void test_puzzle_lowest(void) {
    CHECK(sv_puzzle_set_date(10, 5));
    SvGraph g;
    CHECK(sv_init(&g, &sv_puzzle_lowest, false));
    CHECK(sv_explore(&g, 1 << 22));
    sv_solve(&g);
    CHECK(count_outcomes(&g, 0) == 44);
    CHECK(g.nodes[0].value == 0);
    int dead = 0;
    for (int n = 0; n < g.nnodes; n++) dead += g.nodes[n].value == SV_NOBODY;
    CHECK(dead > g.nnodes / 2); /* most of the solver's tree is dead ends */

    /* The same states are solvable under either rule. */
    SvGraph h;
    CHECK(sv_init(&h, &sv_puzzle, false));
    CHECK(sv_explore_back(&h, 1 << 22));
    sv_solve(&h);
    for (int n = 0; n < g.nnodes; n++) CHECK((sv_state_value(&h, sv_state(&g, n)) == 0) == (g.nodes[n].value == 0));
    sv_free(&h);
    sv_free(&g);
}

static void test_puzzle_names(void) {
    CHECK(sv_puzzle_set_date(10, 2));
    CHECK(!strcmp(sv_puzzle.name, "Year puzzle · Oct 2"));
    uint8_t s[18] = {0};
    static SvMove moves[SV_MAX_MOVES];
    int k = sv_puzzle_lowest.moves(s, moves);
    CHECK(k > 0);
    char buf[32];
    sv_puzzle.move_name(s, moves[0], buf, sizeof buf);
    CHECK(!strcmp(buf, "L5 on Jan"));
}

int main(void) {
    test_ttt_full();
    test_ttt_folded();
    test_values();
    test_partial();
    test_order();
    test_paths();
    test_move_names();
    test_puzzle_back();
    test_puzzle_lowest();
    test_puzzle_names();
    return check_report();
}
