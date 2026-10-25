/* Experiments on piece choice for the Whole Year Puzzle (hypotheses 1-4).

   Usage: sv_exp_pieces dyn   [N]   dynamic rarest-piece-first vs static orders
          sv_exp_pieces surv        survival of placements by how constrained the cells they cover are (all dates)
          sv_exp_pieces sweep [N]   tree size as X5 / O4 move from first to last
          sv_exp_pieces flex  [N]   which pieces finish; flexible-last orders
          sv_exp_pieces pairs [N]   piece-pair coupling, and pair-first orders

   N dates are sampled evenly over the year (Oct 5 is always included). Trees are
   searched to exhaustion unless they pass BUDGET nodes (marked >=). */
#include <math.h>
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
enum { L5, Z5, X5, P5, U5, T4, O4, S4, L4 };

static uint64_t pmask[MAXPL];
static int ppiece[MAXPL];
static int valid[NP][MAXPL], nvalid[NP];
static int cover[NB][MAXPL], ncover[NB];
static uint64_t board, datem;
static int nsol;
static int16_t sol[MAXSOL][NP];

static long long budget = 400000000ll, visited;
static bool capped;

static void emit(void *ctx, const void *s) {
    (void)ctx;
    if (nsol >= MAXSOL) return;
    for (int p = 0; p < NP; p++) sol[nsol][p] = (int16_t)sv_puzzle_placed(s, p);
    nsol++;
}

static void load(int month, int day) {
    sv_puzzle_set_date(month, day);
    board = sv_puzzle_board_mask(), datem = sv_puzzle_date_mask();
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

typedef struct {
    long long nodes[NP + 1], alive[NP + 1], dead_roots[NP + 1];
} Stats;

static int cand_buf[NP + 2][MAXSOL];

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

static long long total(const long long *a) {
    long long t = 0;
    for (int i = 0; i <= NP; i++) t += a[i];
    return t;
}

static void start_cands(void) {
    for (int i = 0; i < nsol; i++) cand_buf[0][i] = i;
}

static void dfs_piece(const int *order, int d, uint64_t filled, int ncand, int parent_alive, Stats *st) {
    if (++visited > budget) {
        capped = true;
        return;
    }
    count(st, d, ncand, parent_alive);
    if (d == NP) return;
    int p = order[d];
    for (int i = 0; i < nvalid[p] && !capped; i++) {
        int m = valid[p][i];
        if (pmask[m] & filled) continue;
        int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
        dfs_piece(order, d + 1, filled | pmask[m], n, ncand > 0, st);
    }
}

/* Result of one tree. */
typedef struct {
    long long nodes, alive, dead_roots;
    bool capped;
} Tree;

static Tree run_order(const int *order) {
    Stats st;
    memset(&st, 0, sizeof st);
    visited = 0, capped = false;
    start_cands();
    dfs_piece(order, 0, datem, nsol, 1, &st);
    Tree t = {total(st.nodes), total(st.alive), total(st.dead_roots), capped};
    return t;
}

/* Dynamic: place the unused piece with the fewest placements that fit here. */
static long long last_count[NP], pos_sum[NP], leaves;

static void dfs_dyn(int d, uint64_t filled, int used, int ncand, int parent_alive, Stats *st, int *chosen,
                    bool tally) {
    if (++visited > budget) {
        capped = true;
        return;
    }
    count(st, d, ncand, parent_alive);
    if (d == NP) {
        if (ncand && tally) {
            leaves++;
            last_count[chosen[NP - 1]]++;
            for (int i = 0; i < NP; i++) pos_sum[chosen[i]] += i;
        }
        return;
    }
    int best = -1, bc = 1 << 30;
    for (int p = 0; p < NP; p++) {
        if (used >> p & 1) continue;
        int c = 0;
        for (int i = 0; i < nvalid[p]; i++) c += !(pmask[valid[p][i]] & filled);
        if (c < bc) bc = c, best = p;
        if (!c) return; /* a piece with nowhere to go: dead */
    }
    chosen[d] = best;
    for (int i = 0; i < nvalid[best] && !capped; i++) {
        int m = valid[best][i];
        if (pmask[m] & filled) continue;
        int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
        dfs_dyn(d + 1, filled | pmask[m], used | 1 << best, n, ncand > 0, st, chosen, tally);
    }
}

static Tree run_dyn(bool tally) {
    Stats st;
    memset(&st, 0, sizeof st);
    visited = 0, capped = false;
    int chosen[NP];
    start_cands();
    dfs_dyn(0, datem, 0, nsol, 1, &st, chosen, tally);
    Tree t = {total(st.nodes), total(st.alive), total(st.dead_roots), capped};
    return t;
}

static void fewest_order(int *order) {
    for (int i = 0; i < NP; i++) order[i] = i;
    for (int i = 1; i < NP; i++)
        for (int j = i; j > 0 && nvalid[order[j - 1]] > nvalid[order[j]]; j--) {
            int t = order[j];
            order[j] = order[j - 1], order[j - 1] = t;
        }
}

/* ---- dates ---- */

typedef struct {
    int m, d;
} Date;
static Date dates[400];
static int ndates;

static void sample_dates(int n) {
    Date all[366];
    int k = 0;
    for (int m = 1; m <= 12; m++)
        for (int d = 1; d <= pz_days_in_month(2024, m); d++) all[k++] = (Date){m, d};
    ndates = 0;
    if (n >= 366) {
        memcpy(dates, all, sizeof all);
        ndates = 366;
        return;
    }
    dates[ndates++] = (Date){10, 5};
    for (int i = 0; i < n - 1; i++) {
        Date c = all[(i * 366) / (n > 1 ? n - 1 : 1) % 366];
        if (c.m == 10 && c.d == 5) continue;
        dates[ndates++] = c;
    }
}

static void geo_add(double *sum, int *cnt, double v) {
    if (v > 0) *sum += log(v), (*cnt)++;
}
static double geo(double sum, int cnt) { return cnt ? exp(sum / cnt) : 0; }

/* ---- 1: dynamic rarest-piece-first ---- */

static void cmd_dyn(int n) {
    sample_dates(n);
    enum { PENT, FEW, DYN, K };
    const char *label[K] = {"pentominoes first (static)", "fewest placements at start (static)",
                            "rarest piece at each node (dynamic)"};
    double gs[K] = {0}, gr[K] = {0}, alive_pct[K] = {0}, dead[K] = {0};
    int gc[K] = {0}, gcr[K] = {0}, caps[K] = {0};
    int wins_dyn = 0;
    printf("Dynamic rarest-piece-first, %d dates (nodes = whole tree)\n", ndates);
    printf("  %-8s %-6s %12s %12s %12s\n", "date", "sols", "pent", "fewest", "dynamic");
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        int pent[NP] = {0, 1, 2, 3, 4, 5, 6, 7, 8}, few[NP];
        fewest_order(few);
        Tree t[K];
        t[PENT] = run_order(pent);
        t[FEW] = run_order(few);
        t[DYN] = run_dyn(false);
        printf("  %2d-%02d    %-6d", dates[i].m, dates[i].d, nsol);
        for (int k = 0; k < K; k++) printf(" %11lld%s", t[k].nodes, t[k].capped ? "+" : " ");
        printf("\n");
        for (int k = 0; k < K; k++) {
            geo_add(&gs[k], &gc[k], (double)t[k].nodes);
            geo_add(&gr[k], &gcr[k], (double)t[k].nodes / (double)t[FEW].nodes);
            alive_pct[k] += 100.0 * (double)t[k].alive / (double)t[k].nodes;
            dead[k] += (double)t[k].dead_roots;
            caps[k] += t[k].capped;
        }
        wins_dyn += t[DYN].nodes < t[FEW].nodes;
    }
    printf("\n  %-40s %14s %12s %10s %12s\n", "order", "geomean nodes", "vs fewest", "alive %", "dead roots");
    for (int k = 0; k < K; k++)
        printf("  %-40s %14.0f %11.2fx %9.4f%% %12.0f%s\n", label[k], geo(gs[k], gc[k]), geo(gr[k], gcr[k]),
               alive_pct[k] / ndates, dead[k] / ndates, caps[k] ? "  (some capped)" : "");
    printf("  dynamic smaller than static fewest on %d of %d dates\n", wins_dyn, ndates);
}

/* ---- 2: survival by constrainedness; X5 / O4 sweep ---- */

static void cmd_surv(void) {
    sample_dates(366);
    enum { BINS = 4 };
    const char *bin_label[BINS] = {"covers 1 of 3 most-constrained cells", "best cell rank 3-9", "best cell rank 10-19",
                                   "best cell rank 20+"};
    static long long pl[NP][BINS], al[NP][BINS];
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        static bool live[MAXPL];
        memset(live, 0, sizeof live);
        for (int s = 0; s < nsol; s++)
            for (int p = 0; p < NP; p++) live[sol[s][p]] = true;
        /* rank the open cells by how few placements can cover them */
        int cells[NB], nc = 0, rank[NB];
        for (int b = 0; b < NB; b++)
            if ((board & ~datem) >> b & 1) cells[nc++] = b;
        for (int a = 1; a < nc; a++)
            for (int j = a; j > 0 && ncover[cells[j - 1]] > ncover[cells[j]]; j--) {
                int t = cells[j];
                cells[j] = cells[j - 1], cells[j - 1] = t;
            }
        for (int r = 0; r < nc; r++) rank[cells[r]] = r;
        for (int p = 0; p < NP; p++)
            for (int k = 0; k < nvalid[p]; k++) {
                int m = valid[p][k], best = 99;
                for (int b = 0; b < NB; b++)
                    if (pmask[m] >> b & 1 && !(datem >> b & 1) && rank[b] < best) best = rank[b];
                int bin = best < 3 ? 0 : best < 10 ? 1 : best < 20 ? 2 : 3;
                pl[p][bin]++, al[p][bin] += live[m];
            }
    }
    printf("Placement survival (at the empty board) by the most-constrained cell it covers, %d dates.\n", ndates);
    printf("Cells ranked by how few placements (any piece) can cover them.\n");
    printf("  %-40s", "");
    for (int p = 0; p < NP; p++) printf("%9s", NAME[p]);
    printf("%9s\n", "all");
    for (int b = 0; b < BINS; b++) {
        printf("  %-40s", bin_label[b]);
        long long tp = 0, ta = 0;
        for (int p = 0; p < NP; p++) {
            printf("%8.1f%%", pl[p][b] ? 100.0 * (double)al[p][b] / (double)pl[p][b] : 0.0);
            tp += pl[p][b], ta += al[p][b];
        }
        printf("%8.1f%%\n", 100.0 * (double)ta / (double)tp);
    }
    printf("  share of placements in each bin (all pieces): ");
    long long all = 0;
    for (int b = 0; b < BINS; b++)
        for (int p = 0; p < NP; p++) all += pl[p][b];
    for (int b = 0; b < BINS; b++) {
        long long t = 0;
        for (int p = 0; p < NP; p++) t += pl[p][b];
        printf(" %.1f%%", 100.0 * (double)t / (double)all);
    }
    printf("\n");
}

static void cmd_sweep(int n) {
    sample_dates(n);
    int pieces[2] = {X5, O4};
    printf("Position sweep, %d dates: piece moved to position k (0 = first); the rest in fewest-placements order.\n",
           ndates);
    printf("Geomean tree nodes (+ = some trees hit the %lld budget, so a lower bound), and geomean vs position 0.\n",
           budget);
    for (int w = 0; w < 2; w++) {
        int x = pieces[w];
        double gs[NP] = {0}, gr[NP] = {0};
        int gc[NP] = {0}, caps[NP] = {0};
        for (int i = 0; i < ndates; i++) {
            load(dates[i].m, dates[i].d);
            int base[NP], rest[NP - 1], nr = 0;
            fewest_order(base);
            for (int j = 0; j < NP; j++)
                if (base[j] != x) rest[nr++] = base[j];
            double first = 0;
            for (int k = 0; k < NP; k++) {
                int order[NP], r = 0;
                for (int j = 0; j < NP; j++) order[j] = j == k ? x : rest[r++];
                Tree t = run_order(order);
                if (k == 0) first = (double)t.nodes;
                geo_add(&gs[k], &gc[k], (double)t.nodes);
                if (first > 0) gr[k] += log((double)t.nodes / first);
                caps[k] += t.capped;
            }
        }
        printf("\n  %s moved to position k:\n    k     ", NAME[x]);
        for (int k = 0; k < NP; k++) printf("%12d", k);
        printf("\n    nodes ");
        for (int k = 0; k < NP; k++) printf("%11.0f%s", exp(gs[k] / ndates), caps[k] ? "+" : " ");
        printf("\n    vs k=0");
        for (int k = 0; k < NP; k++) printf("%11.2fx", exp(gr[k] / ndates));
        printf("\n");
    }
}

/* ---- 3: finishers and flexible-last orders ---- */

static void cmd_flex(int n) {
    sample_dates(n);
    static double loose[NP], looseness_cells[NP];
    static long long loosest[NP], nsols_total;
    static double nv_sum[NP];
    memset(last_count, 0, sizeof last_count), memset(pos_sum, 0, sizeof pos_sum), leaves = 0;
    enum { FEW, FLEXLAST, PENT, FLEXFIRST, REV, K };
    const char *label[K] = {"fewest placements first", "pentominoes first, L5/P5 last among them, L4 last",
                            "pentominoes first (L5 Z5 X5 P5 U5 ...)", "flexible first: L4 P5 L5 S4 U5 T4 Z5 O4 X5",
                            "reverse of fewest"};
    double gs[K] = {0};
    int gc[K] = {0}, caps[K] = {0};
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        /* how open the cells each solution piece covers are */
        for (int s = 0; s < nsol; s++) {
            int bestb = -1, bestc = -1;
            for (int b = 0; b < NB; b++)
                if ((board & ~datem) >> b & 1 && ncover[b] > bestc) bestc = ncover[b], bestb = b;
            for (int p = 0; p < NP; p++) {
                uint64_t mk = pmask[sol[s][p]];
                double sum = 0;
                int cnt = 0;
                for (int b = 0; b < NB; b++)
                    if (mk >> b & 1) sum += ncover[b], cnt++;
                loose[p] += sum / cnt, looseness_cells[p] += 1;
                if (mk >> bestb & 1) loosest[p]++;
            }
            nsols_total++;
        }
        for (int p = 0; p < NP; p++) nv_sum[p] += nvalid[p];
        run_dyn(true);
        int few[NP], rev[NP];
        fewest_order(few);
        for (int j = 0; j < NP; j++) rev[j] = few[NP - 1 - j];
        int flexlast[NP] = {Z5, X5, U5, L5, P5, O4, T4, S4, L4};
        int pent[NP] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        int flexfirst[NP] = {L4, P5, L5, S4, U5, T4, Z5, O4, X5};
        const int *ord[K] = {few, flexlast, pent, flexfirst, rev};
        for (int k = 0; k < K; k++) {
            Tree t = run_order(ord[k]);
            geo_add(&gs[k], &gc[k], (double)t.nodes);
            caps[k] += t.capped;
        }
    }
    printf("Finishers, %d dates, %lld solutions (every solution counted once)\n", ndates, nsols_total);
    printf("  %-6s %10s %14s %16s %16s %12s\n", "piece", "placements", "mean cell openness", "covers most-open cell",
           "last under rarest", "mean position");
    for (int p = 0; p < NP; p++)
        printf("  %-6s %10.0f %14.1f %15.1f%% %15.1f%% %12.2f\n", NAME[p], nv_sum[p] / ndates,
               loose[p] / looseness_cells[p], 100.0 * (double)loosest[p] / (double)nsols_total,
               100.0 * (double)last_count[p] / (double)leaves, (double)pos_sum[p] / (double)leaves);
    printf("  (openness = placements that cover a cell; position 0 = first chosen by the dynamic rarest-piece rule)\n");
    printf("\n  %-52s %16s\n", "order", "geomean nodes");
    for (int k = 0; k < K; k++) printf("  %-52s %15.0f%s\n", label[k], geo(gs[k], gc[k]), caps[k] ? "+" : "");
}

/* ---- 4: pair coupling ---- */

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

static void cmd_pairs(int n) {
    sample_dates(n);
    static double r_sum[NP][NP], lift_sum[NP][NP], gsize[NP][NP];
    static int gcnt[NP][NP];
    double gbase = 0;
    int gbc = 0;
    /* pooled for correlation: log r vs log nodes (relative to baseline per date) */
    static double xs[400 * 40], ys[400 * 40];
    int npts = 0;
    int idx_a[400 * 40], idx_b[400 * 40];
    (void)idx_a, (void)idx_b;
    double tight_sum = 0, loose_sum = 0;
    int tl_cnt = 0;
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        static bool live[MAXPL];
        memset(live, 0, sizeof live);
        for (int s = 0; s < nsol; s++)
            for (int p = 0; p < NP; p++) live[sol[s][p]] = true;
        double pa[NP];
        for (int p = 0; p < NP; p++) {
            int a = 0;
            for (int k = 0; k < nvalid[p]; k++) a += live[valid[p][k]];
            pa[p] = (double)a / nvalid[p];
        }
        int few[NP];
        fewest_order(few);
        long long base_nodes = run_order(few).nodes;
        geo_add(&gbase, &gbc, (double)base_nodes);
        double r_of[NP][NP], nodes_of[NP][NP];
        for (int a = 0; a < NP; a++)
            for (int b = a + 1; b < NP; b++) {
                long long pairs = 0;
                for (int i1 = 0; i1 < nvalid[a]; i1++)
                    for (int i2 = 0; i2 < nvalid[b]; i2++) pairs += !(pmask[valid[a][i1]] & pmask[valid[b][i2]]);
                int keys[MAXSOL];
                for (int s = 0; s < nsol; s++) keys[s] = sol[s][a] * MAXPL + sol[s][b];
                qsort(keys, (size_t)nsol, sizeof *keys, cmp_int);
                int distinct = 0;
                for (int s = 0; s < nsol; s++) distinct += s == 0 || keys[s] != keys[s - 1];
                double r = (double)distinct / (double)pairs;
                r_of[a][b] = r;
                r_sum[a][b] += r;
                lift_sum[a][b] += r / (pa[a] * pa[b]);
                int first2[2] = {nvalid[a] <= nvalid[b] ? a : b, nvalid[a] <= nvalid[b] ? b : a};
                int order[NP], k = 2;
                order[0] = first2[0], order[1] = first2[1];
                for (int j = 0; j < NP; j++)
                    if (few[j] != a && few[j] != b) order[k++] = few[j];
                Tree t = run_order(order);
                nodes_of[a][b] = (double)t.nodes;
                geo_add(&gsize[a][b], &gcnt[a][b], (double)t.nodes / (double)base_nodes);
                xs[npts] = log(r), ys[npts] = log((double)t.nodes / (double)base_nodes), npts++;
            }
        /* tightest 5 pairs on this date against the loosest 5 */
        int na = 0;
        struct { double r, nodes; } pr[40];
        for (int a = 0; a < NP; a++)
            for (int b = a + 1; b < NP; b++) pr[na].r = r_of[a][b], pr[na].nodes = nodes_of[a][b], na++;
        for (int x = 1; x < na; x++)
            for (int y = x; y > 0 && pr[y - 1].r > pr[y].r; y--) {
                __typeof__(pr[0]) t = pr[y];
                pr[y] = pr[y - 1], pr[y - 1] = t;
            }
        double ts = 0, ls = 0;
        for (int x = 0; x < 5; x++) ts += log(pr[x].nodes / (double)base_nodes), ls += log(pr[na - 1 - x].nodes / (double)base_nodes);
        tight_sum += ts / 5, loose_sum += ls / 5, tl_cnt++;
    }
    printf("Pair coupling, %d dates. r = share of non-overlapping placement pairs (A,B) that sit in some solution\n", ndates);
    printf("(low r = tightly coupled). lift = r / (P(A alive) * P(B alive)). tree = pair placed first (the rarer piece\n");
    printf("first), the rest in fewest-placements order, as a ratio to the plain fewest-first tree.\n\n");
    printf("  pair    mean r   lift   tree vs baseline\n");
    struct { int a, b; double r; } rows[40];
    int nr = 0;
    for (int a = 0; a < NP; a++)
        for (int b = a + 1; b < NP; b++) rows[nr].a = a, rows[nr].b = b, rows[nr].r = r_sum[a][b] / ndates, nr++;
    for (int x = 1; x < nr; x++)
        for (int y = x; y > 0 && rows[y - 1].r > rows[y].r; y--) {
            __typeof__(rows[0]) t = rows[y];
            rows[y] = rows[y - 1], rows[y - 1] = t;
        }
    for (int x = 0; x < nr; x++) {
        int a = rows[x].a, b = rows[x].b;
        if (x < 8 || x >= nr - 6)
            printf("  %s+%s  %8.5f %6.2f   %6.2fx\n", NAME[a], NAME[b], rows[x].r, lift_sum[a][b] / ndates,
                   geo(gsize[a][b], gcnt[a][b]));
        else if (x == 8) printf("  ...\n");
    }
    double mx = 0, my = 0;
    for (int i = 0; i < npts; i++) mx += xs[i], my += ys[i];
    mx /= npts, my /= npts;
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < npts; i++) sxy += (xs[i] - mx) * (ys[i] - my), sxx += (xs[i] - mx) * (xs[i] - mx), syy += (ys[i] - my) * (ys[i] - my);
    printf("\n  correlation of log r with log tree size over %d (date, pair) points: %.3f\n", npts, sxy / sqrt(sxx * syy));
    printf("  per date, the 5 tightest pairs first: %.2fx the baseline tree; the 5 loosest pairs: %.2fx\n",
           exp(tight_sum / tl_cnt), exp(loose_sum / tl_cnt));
    printf("  baseline (fewest placements first) geomean nodes: %.0f\n", geo(gbase, gbc));
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "";
    int n = argc > 2 ? atoi(argv[2]) : 24;
    if (argc > 3) budget = atoll(argv[3]);
    if (!strcmp(cmd, "dyn")) cmd_dyn(n);
    else if (!strcmp(cmd, "surv")) cmd_surv();
    else if (!strcmp(cmd, "sweep")) cmd_sweep(n);
    else if (!strcmp(cmd, "flex")) cmd_flex(n);
    else if (!strcmp(cmd, "pairs")) cmd_pairs(n);
    else return fprintf(stderr, "usage: sv_exp_pieces dyn|surv|sweep|flex|pairs [dates [budget]]\n"), 2;
    return 0;
}
