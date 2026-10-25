/* Measures strategies for the Whole Year Puzzle in its state space.

   Usage: sv_insights pieces [MM-DD]    piece-order trees (pentominoes first?)
          sv_insights cells [all|MM-DD] cell-order trees (start by the holes?)
          sv_insights first [all|MM-DD] which first placements keep it solvable

   A strategy is a search tree searched to exhaustion: each node is a partial
   placement. A node is "alive" if some solution contains it, and a dead end
   otherwise; dead nodes are pure wasted work. Tree size is the work. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games.h"
#include "puzzle.h"

#define NP PZ_PIECES
#define MAXPL 2048
#define MAXSOL 4096
#define NB (PZ_BOARD * PZ_BOARD)

static const char *const NAME[NP] = {"L5", "Z5", "X5", "P5", "U5", "T4", "O4", "S4", "L4"};

static uint64_t pmask[MAXPL]; /* by move id */
static int ppiece[MAXPL];
static int valid[NP][MAXPL], nvalid[NP];     /* placements per piece that avoid the date */
static int cover[NB][MAXPL], ncover[NB];     /* placements covering a cell */
static uint64_t board, datem;
static int date_cell[2];
static int nsol;
static int16_t sol[MAXSOL][NP];              /* a solution's placement per piece */

static void emit(void *ctx, const void *s) {
    (void)ctx;
    if (nsol >= MAXSOL) return;
    for (int p = 0; p < NP; p++) sol[nsol][p] = (int16_t)sv_puzzle_placed(s, p);
    nsol++;
}

static void load(int month, int day) {
    sv_puzzle_set_date(month, day);
    board = sv_puzzle_board_mask(), datem = sv_puzzle_date_mask();
    int k = 0;
    for (int b = 0; b < NB; b++)
        if (datem >> b & 1) date_cell[k++] = b;
    uint8_t s[SV_MAX_STATE], t[SV_MAX_STATE];
    SvMove mv[SV_MAX_MOVES];
    sv_puzzle.initial(s);
    int n = sv_puzzle.moves(s, mv);
    memset(nvalid, 0, sizeof nvalid), memset(ncover, 0, sizeof ncover);
    for (int i = 0; i < n; i++) {
        int m = mv[i];
        sv_puzzle.apply(s, mv[i], t);
        int p = 0;
        while (sv_puzzle_placed(t, p) < 0) p++;
        pmask[m] = sv_puzzle_mask(mv[i]), ppiece[m] = p;
        valid[p][nvalid[p]++] = m;
        for (int b = 0; b < NB; b++)
            if (pmask[m] >> b & 1) cover[b][ncover[b]++] = m;
    }
    nsol = 0;
    sv_puzzle.goals(emit, NULL);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int dist(int a, int b) { return abs(a / PZ_BOARD - b / PZ_BOARD) + abs(a % PZ_BOARD - b % PZ_BOARD); }
static int near_date(int b) { return imin(dist(b, date_cell[0]), dist(b, date_cell[1])); }

typedef struct {
    long long nodes[NP + 1], alive[NP + 1], dead_roots[NP + 1];
} Stats;

static int cand_buf[NP + 2][MAXSOL];

/* Keeps the solutions that agree with placing move m. */
static int narrow(const int *cand, int n, int m, int *out) {
    int k = 0, p = ppiece[m];
    for (int i = 0; i < n; i++)
        if (sol[cand[i]][p] == m) out[k++] = cand[i];
    return k;
}

static void count(Stats *st, int d, int n, int parent_alive) {
    st->nodes[d]++;
    if (n) st->alive[d]++;
    else if (parent_alive) st->dead_roots[d]++;
}

/* Pieces are placed in a fixed order, each anywhere it fits. */
static void dfs_piece(const int *order, int d, uint64_t filled, int ncand, int parent_alive, Stats *st) {
    count(st, d, ncand, parent_alive);
    if (d == NP) return;
    int p = order[d];
    for (int i = 0; i < nvalid[p]; i++) {
        int m = valid[p][i];
        if (pmask[m] & filled) continue;
        int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
        dfs_piece(order, d + 1, filled | pmask[m], n, ncand > 0, st);
    }
}

typedef enum { C_ROW, C_COL, C_NEAR, C_FAR, C_EDGE, C_CENTER, C_MRV, C_KINDS } CellRule;
static const char *const CELL_RULE[C_KINDS] = {"row by row (the solver's)", "column by column", "nearest the date first",
                                               "farthest from the date first", "edges first", "center first",
                                               "most constrained cell first"};
static int cell_order[NB], ncells_order;

static int nb_count(int b) {
    int r = b / PZ_BOARD, c = b % PZ_BOARD, n = 0;
    n += r > 0 && (board >> (b - PZ_BOARD) & 1);
    n += r + 1 < PZ_BOARD && (board >> (b + PZ_BOARD) & 1);
    n += c > 0 && (board >> (b - 1) & 1);
    n += c + 1 < PZ_BOARD && (board >> (b + 1) & 1);
    return n;
}

static int key_of(CellRule rule, int b) {
    switch (rule) {
    case C_COL: return b % PZ_BOARD * 100 + b / PZ_BOARD;
    case C_NEAR: return near_date(b) * 100 + b;
    case C_FAR: return (99 - near_date(b)) * 100 + b;
    case C_EDGE: return nb_count(b) * 100 + b;
    case C_CENTER: return (9 - nb_count(b)) * 100 + b;
    default: return b;
    }
}

static void build_order(CellRule rule) {
    ncells_order = 0;
    for (int b = 0; b < NB; b++)
        if ((board & ~datem) >> b & 1) cell_order[ncells_order++] = b;
    for (int i = 1; i < ncells_order; i++) {
        int v = cell_order[i], j = i;
        while (j > 0 && key_of(rule, cell_order[j - 1]) > key_of(rule, v)) cell_order[j] = cell_order[j - 1], j--;
        cell_order[j] = v;
    }
}

/* Each step picks an empty cell by the rule and tries every unused piece
   covering it. */
static void dfs_cell(CellRule rule, int d, uint64_t filled, int used, int ncand, int parent_alive, Stats *st) {
    count(st, d, ncand, parent_alive);
    if (d == NP) return;
    int cell = -1;
    if (rule == C_MRV) {
        int best = 1 << 30;
        for (int i = 0; i < ncells_order; i++) {
            int b = cell_order[i];
            if (filled >> b & 1) continue;
            int k = 0;
            for (int j = 0; j < ncover[b]; j++) {
                int m = cover[b][j];
                k += !(used >> ppiece[m] & 1) && !(pmask[m] & filled);
            }
            if (k < best) best = k, cell = b;
        }
    } else {
        for (int i = 0; i < ncells_order && cell < 0; i++)
            if (!(filled >> cell_order[i] & 1)) cell = cell_order[i];
    }
    for (int j = 0; j < ncover[cell]; j++) {
        int m = cover[cell][j];
        if ((used >> ppiece[m] & 1) || (pmask[m] & filled)) continue;
        int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
        dfs_cell(rule, d + 1, filled | pmask[m], used | 1 << ppiece[m], n, ncand > 0, st);
    }
}

static void start_cands(void) {
    for (int i = 0; i < nsol; i++) cand_buf[0][i] = i;
}

static long long total(const long long *a) {
    long long t = 0;
    for (int i = 0; i <= NP; i++) t += a[i];
    return t;
}

static void print_stats(const char *label, const Stats *st) {
    long long n = total(st->nodes), a = total(st->alive), dr = total(st->dead_roots);
    printf("  %-34s nodes %12lld  alive %6lld (%.3f%%)  dead roots %10lld  waste/root %8.1f\n", label, n, a,
           100.0 * (double)a / (double)n, dr, dr ? (double)(n - a) / (double)dr : 0.0);
}

static void print_profile(const Stats *st) {
    printf("    depth       ");
    for (int d = 0; d <= NP; d++) printf("%11d", d);
    printf("\n    nodes       ");
    for (int d = 0; d <= NP; d++) printf("%11lld", st->nodes[d]);
    printf("\n    alive       ");
    for (int d = 0; d <= NP; d++) printf("%11lld", st->alive[d]);
    printf("\n    alive %%     ");
    for (int d = 0; d <= NP; d++) printf("%10.3f%%", 100.0 * (double)st->alive[d] / (double)st->nodes[d]);
    printf("\n    dead roots  ");
    for (int d = 0; d <= NP; d++) printf("%11lld", st->dead_roots[d]);
    printf("\n");
}

static uint64_t rng = 88172645463325252ull;
static unsigned rnd(void) {
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return (unsigned)(rng >> 11);
}

static void shuffle(int *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rnd() % (unsigned)(i + 1)), t = a[i];
        a[i] = a[j], a[j] = t;
    }
}

static void run_order(const int *order, Stats *st) {
    memset(st, 0, sizeof *st);
    start_cands();
    dfs_piece(order, 0, datem, nsol, 1, st);
}

static void order_name(const int *order, char *buf) {
    buf[0] = 0;
    for (int i = 0; i < NP; i++) strcat(buf, NAME[order[i]]), strcat(buf, i + 1 < NP ? " " : "");
}

static int cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return x < y ? -1 : x > y;
}

static void cmd_pieces(int month, int day, int samples) {
    load(month, day);
    printf("Piece order, %s %d: %d solutions\n", pz_month_name(month), day, nsol);
    printf("Placements per piece:");
    for (int p = 0; p < NP; p++) printf(" %s=%d", NAME[p], nvalid[p]);
    printf("\n\nNamed orders (the first piece is placed first):\n");
    int pent[NP] = {0, 1, 2, 3, 4, 5, 6, 7, 8}, tet[NP] = {5, 6, 7, 8, 0, 1, 2, 3, 4};
    int few[NP], many[NP];
    for (int i = 0; i < NP; i++) few[i] = many[i] = i;
    for (int i = 1; i < NP; i++)
        for (int j = i; j > 0 && nvalid[few[j - 1]] > nvalid[few[j]]; j--) {
            int t = few[j];
            few[j] = few[j - 1], few[j - 1] = t;
        }
    for (int i = 0; i < NP; i++) many[i] = few[NP - 1 - i];
    struct { const char *label; int *order; } named[] = {
        {"pentominoes first", pent}, {"tetrominoes first", tet}, {"fewest placements first", few},
        {"most placements first", many}};
    Stats st;
    for (unsigned i = 0; i < sizeof named / sizeof *named; i++) {
        char buf[64], label[96];
        order_name(named[i].order, buf);
        snprintf(label, sizeof label, "%s (%s)", named[i].label, buf);
        run_order(named[i].order, &st);
        printf("  %s\n", label);
        print_stats("", &st);
        print_profile(&st);
    }

    printf("\nRandom orders within each class (%d each), total nodes:\n", samples);
    for (int cls = 0; cls < 3; cls++) {
        long long *res = malloc(sizeof *res * (size_t)samples);
        for (int s = 0; s < samples; s++) {
            int order[NP], pent[5] = {0, 1, 2, 3, 4}, tet[4] = {5, 6, 7, 8};
            shuffle(pent, 5), shuffle(tet, 4);
            if (cls == 0) memcpy(order, pent, sizeof pent), memcpy(order + 5, tet, sizeof tet);
            else if (cls == 1) memcpy(order, tet, sizeof tet), memcpy(order + 4, pent, sizeof pent);
            else {
                for (int i = 0; i < NP; i++) order[i] = i;
                shuffle(order, NP);
            }
            run_order(order, &st);
            res[s] = total(st.nodes);
        }
        qsort(res, (size_t)samples, sizeof *res, cmp_ll);
        double mean = 0;
        for (int s = 0; s < samples; s++) mean += (double)res[s];
        printf("  %-20s min %12lld  median %12lld  mean %14.0f  max %12lld\n",
               cls == 0 ? "pentominoes first" : cls == 1 ? "tetrominoes first" : "any order", res[0],
               res[samples / 2], mean / samples, res[samples - 1]);
        free(res);
    }
}

static void each_date(const char *arg, void (*fn)(int, int, void *), void *ctx) {
    int m, d;
    if (strcmp(arg, "all")) {
        if (sscanf(arg, "%d-%d", &m, &d) != 2 || !sv_puzzle_set_date(m, d)) {
            fprintf(stderr, "bad date %s\n", arg);
            exit(2);
        }
        fn(m, d, ctx);
        return;
    }
    for (m = 1; m <= 12; m++)
        for (d = 1; d <= pz_days_in_month(2024, m); d++) fn(m, d, ctx);
}

typedef struct {
    long long nodes[C_KINDS], alive[C_KINDS], dead_roots[C_KINDS];
    double ratio_sum[C_KINDS];
    int dates, wins[C_KINDS];
    Stats last[C_KINDS];
} CellAgg;

static void cells_date(int month, int day, void *ctx) {
    CellAgg *g = ctx;
    load(month, day);
    long long best = 1ll << 60;
    long long n[C_KINDS];
    for (int r = 0; r < C_KINDS; r++) {
        Stats st;
        memset(&st, 0, sizeof st);
        build_order(r);
        start_cands();
        dfs_cell((CellRule)r, 0, datem, 0, nsol, 1, &st);
        n[r] = total(st.nodes);
        g->nodes[r] += n[r], g->alive[r] += total(st.alive), g->dead_roots[r] += total(st.dead_roots);
        g->last[r] = st;
        if (n[r] < best) best = n[r];
    }
    for (int r = 0; r < C_KINDS; r++) g->wins[r] += n[r] == best, g->ratio_sum[r] += (double)n[r] / (double)n[C_ROW];
    g->dates++;
}

static void cmd_cells(const char *arg) {
    CellAgg *g = calloc(1, sizeof *g);
    each_date(arg, cells_date, g);
    printf("Cell order, %d date%s. Each step takes an empty cell by the rule and tries every unused piece on it.\n",
           g->dates, g->dates == 1 ? "" : "s, averaged");
    printf("  %-32s %12s %10s %12s %10s %12s\n", "rule", "nodes/date", "alive %", "vs row-major", "best on", "waste/root");
    for (int r = 0; r < C_KINDS; r++) {
        double n = (double)g->nodes[r] / g->dates;
        printf("  %-32s %12.0f %9.3f%% %11.2fx %7d/%d %12.1f\n", CELL_RULE[r], n,
               100.0 * (double)g->alive[r] / (double)g->nodes[r], g->ratio_sum[r] / g->dates, g->wins[r], g->dates,
               g->dead_roots[r] ? (double)(g->nodes[r] - g->alive[r]) / (double)g->dead_roots[r] : 0.0);
    }
    if (g->dates == 1) {
        for (int r = 0; r < C_KINDS; r++) {
            printf("\n  %s\n", CELL_RULE[r]);
            print_profile(&g->last[r]);
        }
    }
    free(g);
}

typedef struct {
    long long placements[8], alive[8];       /* by distance from the date, 1..6+ */
    long long edge[2], edge_alive[2];        /* touching an edge, or not */
    long long piece[NP], piece_alive[NP];
    int dates;
} FirstAgg;

static void first_date(int month, int day, void *ctx) {
    FirstAgg *g = ctx;
    load(month, day);
    static bool live[MAXPL];
    memset(live, 0, sizeof live);
    for (int i = 0; i < nsol; i++)
        for (int p = 0; p < NP; p++) live[sol[i][p]] = true;
    for (int p = 0; p < NP; p++)
        for (int i = 0; i < nvalid[p]; i++) {
            int m = valid[p][i], dmin = 99, edge = 0;
            for (int b = 0; b < NB; b++)
                if (pmask[m] >> b & 1) {
                    dmin = imin(dmin, near_date(b));
                    edge |= nb_count(b) < 4;
                }
            int bucket = imin(dmin, 7);
            g->placements[bucket]++, g->alive[bucket] += live[m];
            g->edge[edge]++, g->edge_alive[edge] += live[m];
            g->piece[p]++, g->piece_alive[p] += live[m];
        }
    g->dates++;
}

static void cmd_first(const char *arg) {
    FirstAgg *g = calloc(1, sizeof *g);
    each_date(arg, first_date, g);
    printf("First placement, %d date%s: how often a placement keeps the puzzle solvable\n", g->dates,
           g->dates == 1 ? "" : "s");
    printf("\nBy distance from the date holes to the nearest cell of the piece (Manhattan):\n");
    for (int b = 1; b <= 7; b++)
        if (g->placements[b])
            printf("  %d%s  %9lld placements  %8lld solvable  %6.2f%%\n", b, b == 7 ? "+" : " ", g->placements[b],
                   g->alive[b], 100.0 * (double)g->alive[b] / (double)g->placements[b]);
    printf("\nBy whether the piece touches an edge of the board (or its notches):\n");
    for (int e = 0; e < 2; e++)
        printf("  %-13s %9lld placements  %8lld solvable  %6.2f%%\n", e ? "touches edge" : "interior only", g->edge[e],
               g->edge_alive[e], 100.0 * (double)g->edge_alive[e] / (double)g->edge[e]);
    printf("\nBy piece:\n");
    for (int p = 0; p < NP; p++)
        printf("  %s  %9lld placements  %8lld solvable  %6.2f%%\n", NAME[p], g->piece[p], g->piece_alive[p],
               100.0 * (double)g->piece_alive[p] / (double)g->piece[p]);
    free(g);
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "";
    const char *arg = argc > 2 ? argv[2] : "10-05";
    if (!strcmp(cmd, "pieces")) {
        int m, d;
        if (sscanf(arg, "%d-%d", &m, &d) != 2 || !sv_puzzle_set_date(m, d)) return fprintf(stderr, "bad date\n"), 2;
        cmd_pieces(m, d, argc > 3 ? atoi(argv[3]) : 20);
    } else if (!strcmp(cmd, "cells")) cmd_cells(arg);
    else if (!strcmp(cmd, "first")) cmd_first(arg);
    else return fprintf(stderr, "usage: sv_insights pieces [MM-DD [samples]] | cells [all|MM-DD] | first [all|MM-DD]\n"), 2;
    return 0;
}
