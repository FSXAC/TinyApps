/* Experiments 9-12 on the Whole Year Puzzle (dates, forced cells, dead-root depth, backtracking).

   sv_exp_dates h9                 solution count vs where the holes sit (all dates)
   sv_exp_dates h10                forced cells at the empty start vs hole distance (all dates)
   sv_exp_dates h11 [step]         where dead roots begin, for row / MRV / pentominoes-first trees
   sv_exp_dates h12 MM-DD [trials] backtracking policies for a random human
*/
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

static uint64_t pmask[MAXPL];
static int ppiece[MAXPL];
static int valid[NP][MAXPL], nvalid[NP];
static int cover[NB][MAXPL], ncover[NB];
static uint64_t board, datem;
static int date_cell[2];
static int nsol;
static int16_t sol[MAXSOL][NP];
static int cur_month, cur_day;

static void emit(void *ctx, const void *s) {
    (void)ctx;
    if (nsol >= MAXSOL) return;
    for (int p = 0; p < NP; p++) sol[nsol][p] = (int16_t)sv_puzzle_placed(s, p);
    nsol++;
}

static void load(int month, int day) {
    sv_puzzle_set_date(month, day);
    cur_month = month, cur_day = day;
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

/* ---- statistics helpers ---- */
typedef struct { double n, sx, sy, sxx, syy, sxy; } Corr;
static void corr_add(Corr *c, double x, double y) {
    c->n++, c->sx += x, c->sy += y, c->sxx += x * x, c->syy += y * y, c->sxy += x * y;
}
static double corr_r(const Corr *c) {
    double cov = c->sxy - c->sx * c->sy / c->n, vx = c->sxx - c->sx * c->sx / c->n, vy = c->syy - c->sy * c->sy / c->n;
    return vx > 0 && vy > 0 ? cov / sqrt(vx * vy) : 0;
}

static void each_date(void (*fn)(int, int, void *), void *ctx, int step) {
    int idx = 0;
    for (int m = 1; m <= 12; m++)
        for (int d = 1; d <= pz_days_in_month(2024, m); d++, idx++)
            if (idx % step == 0) fn(m, d, ctx);
}

/* ================= H9 ================= */
typedef struct { int m, d, n, mc, dc, hd; } DateRow;
static DateRow rows9[400];
static int nrows9;

static void h9_date(int m, int d, void *ctx) {
    (void)ctx;
    load(m, d);
    DateRow *r = &rows9[nrows9++];
    r->m = m, r->d = d, r->n = nsol, r->mc = date_cell[0], r->dc = date_cell[1], r->hd = dist(date_cell[0], date_cell[1]);
}

static int cmp_rows(const void *a, const void *b) { return ((const DateRow *)a)->n - ((const DateRow *)b)->n; }

static void cmd_h9(void) {
    each_date(h9_date, NULL, 1);
    int zero = 0;
    double tot = 0;
    for (int i = 0; i < nrows9; i++) zero += rows9[i].n == 0, tot += rows9[i].n;
    printf("H9: solutions per date, %d dates, mean %.1f, zero-solution dates %d (cap %d)\n", nrows9, tot / nrows9, zero, MAXSOL);
    DateRow sorted[400];
    memcpy(sorted, rows9, sizeof sorted);
    qsort(sorted, (size_t)nrows9, sizeof sorted[0], cmp_rows);
    int mn = sorted[0].n, mx = sorted[nrows9 - 1].n;
    printf("  min %d, median %d, max %d\n  fewest:", mn, sorted[nrows9 / 2].n, mx);
    for (int i = 0; i < 10; i++) printf(" %02d-%02d(%d)", sorted[i].m, sorted[i].d, sorted[i].n);
    printf("\n  most:  ");
    for (int i = nrows9 - 1; i >= nrows9 - 10; i--) printf(" %02d-%02d(%d)", sorted[i].m, sorted[i].d, sorted[i].n);
    printf("\n");
    /* correlations: against log(1+n) and n */
    struct { const char *label; Corr c, cl; } cs[8];
    memset(cs, 0, sizeof cs);
    const char *labels[8] = {"month cell row", "month cell col", "day cell row", "day cell col", "hole distance (Manhattan)",
                             "month number", "day number", "hole row-distance"};
    for (int i = 0; i < nrows9; i++) {
        DateRow *r = &rows9[i];
        double x[8] = {r->mc / 7, r->mc % 7, r->dc / 7, r->dc % 7, r->hd, r->m, r->d, abs(r->mc / 7 - r->dc / 7)};
        for (int k = 0; k < 8; k++) {
            corr_add(&cs[k].c, x[k], r->n);
            corr_add(&cs[k].cl, x[k], log(1.0 + r->n));
        }
    }
    printf("  Pearson r with solution count (and with log count):\n");
    for (int k = 0; k < 8; k++) printf("    %-28s r = %+.3f   (log: %+.3f)\n", labels[k], corr_r(&cs[k].c), corr_r(&cs[k].cl));
    printf("  mean solutions by month:");
    for (int m = 1; m <= 12; m++) {
        double s = 0, c = 0;
        for (int i = 0; i < nrows9; i++) if (rows9[i].m == m) s += rows9[i].n, c++;
        printf(" %d:%.0f", m, s / c);
    }
    printf("\n  mean solutions by day-cell position (row,col):");
    for (int r = 2; r < 7; r++)
        for (int c = 0; c < 7; c++) {
            double s = 0, k = 0;
            for (int i = 0; i < nrows9; i++) if (rows9[i].dc == r * 7 + c) s += rows9[i].n, k++;
            if (k) printf(" (%d,%d):%.0f", r, c, s / k);
        }
    printf("\n  mean solutions by hole distance:");
    for (int h = 0; h < 14; h++) {
        double s = 0, k = 0;
        for (int i = 0; i < nrows9; i++) if (rows9[i].hd == h) s += rows9[i].n, k++;
        if (k) printf(" %d:%.0f(n=%.0f)", h, s / k, k);
    }
    printf("\n  mean solutions by month-cell position:");
    for (int c = 0; c < 12; c++) {
        int pos = (c < 6 ? 0 : 7) + c % 6;
        double s = 0, k = 0;
        for (int i = 0; i < nrows9; i++) if (rows9[i].mc == pos) s += rows9[i].n, k++;
        if (k) printf(" %d:%.0f", c + 1, s / k);
    }
    printf("\n");
}

/* ================= H10 ================= */
static int nb_count(int b) {
    int r = b / PZ_BOARD, c = b % PZ_BOARD, n = 0;
    n += r > 0 && (board >> (b - PZ_BOARD) & 1);
    n += r + 1 < PZ_BOARD && (board >> (b + PZ_BOARD) & 1);
    n += c > 0 && (board >> (b - 1) & 1);
    n += c + 1 < PZ_BOARD && (board >> (b + 1) & 1);
    return n;
}

typedef struct {
    int dates;
    Corr forced_vs_hd, forced_vs_nsol, forcedpl_vs_hd;
    double forced_sum, live_sum, validpl_sum, mand_sum, forcedpl_sum;
    int live_min, live_max, forced_min, forced_max;
    long long cells_at[8], forced_at[8];         /* by distance to nearest hole 1..7+ */
    long long cells_nb[5], forced_nb[5];         /* by in-board neighbours */
    double forced_by_hd[14], dates_by_hd[14];
    double min_cover_sum;
    long long min_cover_hist[8];
    Corr d1_vs_hd;
    double d1_sum;
    long long d1_n, d1_cells_by_ns[4], d1_forced_by_ns[4], d1_pl_by_ns[4];
    long long d1_cells_at[8], d1_forced_at[8];
    double d1_by_hd[14], d1_cnt_hd[14];
    long long cover_hist[8];                     /* live covers per cell: 1,2,3,4,5,6,7+ */
} H10;

static void h10_date(int m, int d, void *ctx) {
    H10 *g = ctx;
    load(m, d);
    if (!nsol) return;
    static int livecount[MAXPL];
    static int count_in_sol[MAXPL];
    memset(count_in_sol, 0, sizeof count_in_sol);
    for (int i = 0; i < nsol; i++)
        for (int p = 0; p < NP; p++) count_in_sol[sol[i][p]]++;
    int live = 0, validpl = 0, mand = 0;
    for (int p = 0; p < NP; p++)
        for (int i = 0; i < nvalid[p]; i++) {
            int mm = valid[p][i];
            livecount[mm] = count_in_sol[mm] > 0;
            live += livecount[mm], validpl++, mand += count_in_sol[mm] == nsol;
        }
    int forced = 0, mincov = 99;
    static bool forced_pl[MAXPL];
    memset(forced_pl, 0, sizeof forced_pl);
    for (int b = 0; b < NB; b++) {
        if (!((board & ~datem) >> b & 1)) continue;
        int k = 0, only = -1;
        for (int j = 0; j < ncover[b]; j++) {
            int mm = cover[b][j];
            if (livecount[mm]) k++, only = mm;
        }
        int nd = imin(near_date(b), 7), nbc = nb_count(b);
        g->cells_at[nd]++, g->cells_nb[nbc]++;
        mincov = imin(mincov, k);
        if (k == 1) {
            forced++, g->forced_at[nd]++, g->forced_nb[nbc]++;
            forced_pl[only] = true;
        }
    }
    int fpl = 0;
    for (int mm = 0; mm < MAXPL; mm++) fpl += forced_pl[mm];
    /* After one placement: restrict to solutions containing it, count cells with one distinct cover. */
    int hd0 = dist(date_cell[0], date_cell[1]);
    for (int p = 0; p < NP; p++)
        for (int i = 0; i < nvalid[p]; i++) {
            int mm = valid[p][i];
            if (!livecount[mm]) continue;
            int first[NB], multi[NB];
            for (int b = 0; b < NB; b++) first[b] = -1, multi[b] = 0;
            for (int si = 0; si < nsol; si++) {
                if (sol[si][p] != mm) continue;
                for (int q = 0; q < NP; q++) {
                    if (q == p) continue;
                    int pm = sol[si][q];
                    for (int b = 0; b < NB; b++)
                        if (pmask[pm] >> b & 1) {
                            if (first[b] < 0) first[b] = pm;
                            else if (first[b] != pm) multi[b] = 1;
                        }
                }
            }
            int f1 = 0;
            for (int b = 0; b < NB; b++) {
                if (!((board & ~datem & ~pmask[mm]) >> b & 1)) continue;
                int nd = imin(near_date(b), 7);
                g->d1_cells_at[nd]++;
                if (!multi[b]) f1++, g->d1_forced_at[nd]++;
            }
            { int ns = count_in_sol[mm], bk = ns == 1 ? 0 : ns <= 3 ? 1 : ns <= 9 ? 2 : 3;
              int rem = 0; for (int b = 0; b < NB; b++) rem += ((board & ~datem & ~pmask[mm]) >> b & 1);
              g->d1_pl_by_ns[bk]++, g->d1_cells_by_ns[bk] += rem, g->d1_forced_by_ns[bk] += f1; }
            g->d1_sum += f1, g->d1_n++;
            corr_add(&g->d1_vs_hd, hd0, f1);
            if (hd0 < 14) g->d1_by_hd[hd0] += f1, g->d1_cnt_hd[hd0]++;
        }
    int hd = dist(date_cell[0], date_cell[1]);
    g->dates++;
    g->min_cover_sum += mincov, g->min_cover_hist[imin(mincov, 7)]++;
    g->forced_sum += forced, g->live_sum += live, g->validpl_sum += validpl, g->mand_sum += mand, g->forcedpl_sum += fpl;
    corr_add(&g->forced_vs_hd, hd, forced);
    corr_add(&g->forced_vs_nsol, log(1.0 + nsol), forced);
    corr_add(&g->forcedpl_vs_hd, hd, fpl);
    if (g->dates == 1 || live < g->live_min) g->live_min = live;
    if (live > g->live_max) g->live_max = live;
    if (g->dates == 1 || forced < g->forced_min) g->forced_min = forced;
    if (forced > g->forced_max) g->forced_max = forced;
    if (hd < 14) g->forced_by_hd[hd] += forced, g->dates_by_hd[hd]++;
}

static void cmd_h10(void) {
    H10 *g = calloc(1, sizeof *g);
    each_date(h10_date, g, 1);
    printf("H10: forced cells at the empty start (a cell with exactly 1 solvable covering placement), %d dates\n", g->dates);
    printf("  per date: valid placements %.0f, solvable (in some solution) %.1f [%d..%d], mandatory (in all solutions) %.2f,\n"
           "            forced cells %.2f [%d..%d] of 41, distinct placements forced %.2f\n",
           g->validpl_sum / g->dates, g->live_sum / g->dates, g->live_min, g->live_max, g->mand_sum / g->dates,
           g->forced_sum / g->dates, g->forced_min, g->forced_max, g->forcedpl_sum / g->dates);
    printf("  r(forced cells, hole Manhattan distance) = %+.3f; r(forced placements, hole distance) = %+.3f; r(forced cells, log solutions) = %+.3f\n",
           corr_r(&g->forced_vs_hd), corr_r(&g->forcedpl_vs_hd), corr_r(&g->forced_vs_nsol));
    printf("  fraction of cells forced, by distance of the cell to the nearest hole:\n");
    for (int k = 1; k <= 7; k++)
        if (g->cells_at[k])
            printf("    %d%s: %7lld cells, %6lld forced = %5.2f%%\n", k, k == 7 ? "+" : " ", g->cells_at[k], g->forced_at[k],
                   100.0 * (double)g->forced_at[k] / (double)g->cells_at[k]);
    printf("  tightest cell at start: fewest solvable covers per date: mean %.1f; histogram (1..7+):", g->min_cover_sum / g->dates);
    for (int k = 1; k <= 7; k++) printf(" %d:%lld", k, g->min_cover_hist[k]);
    printf("\n  AFTER ONE solvable placement (%lld placements pooled): forced cells mean %.2f of the remaining; r(forced, hole distance) = %+.3f\n", g->d1_n, g->d1_sum / (double)g->d1_n, corr_r(&g->d1_vs_hd));
    for (int k = 1; k <= 7; k++)
        if (g->d1_cells_at[k])
            printf("    cell %d%s from a hole: %8lld cells, %7lld forced = %5.2f%%\n", k, k == 7 ? "+" : " ", g->d1_cells_at[k], g->d1_forced_at[k],
                   100.0 * (double)g->d1_forced_at[k] / (double)g->d1_cells_at[k]);
    { const char *nsl[4] = {"1 solution", "2-3 solutions", "4-9 solutions", "10+ solutions"};
      for (int k = 0; k < 4; k++)
        printf("    placement is in %-14s: %7lld placements, %5.1f%% of remaining cells forced\n", nsl[k], g->d1_pl_by_ns[k],
               g->d1_cells_by_ns[k] ? 100.0 * (double)g->d1_forced_by_ns[k] / (double)g->d1_cells_by_ns[k] : 0.0); }
    printf("    mean forced cells after one placement, by distance between holes:");
    for (int h = 0; h < 14; h++)
        if (g->d1_cnt_hd[h]) printf(" %d:%.2f", h, g->d1_by_hd[h] / g->d1_cnt_hd[h]);
    printf("\n  by in-board neighbours of the cell:\n");
    for (int k = 0; k <= 4; k++)
        if (g->cells_nb[k])
            printf("    %d nbrs: %7lld cells, %6lld forced = %5.2f%%\n", k, g->cells_nb[k], g->forced_nb[k],
                   100.0 * (double)g->forced_nb[k] / (double)g->cells_nb[k]);
    printf("  mean forced cells by distance between the two holes:");
    for (int h = 0; h < 14; h++)
        if (g->dates_by_hd[h]) printf(" %d:%.2f(n=%.0f)", h, g->forced_by_hd[h] / g->dates_by_hd[h], g->dates_by_hd[h]);
    printf("\n");
    free(g);
}

/* ================= H11 ================= */
typedef struct { long long nodes[NP + 1], alive[NP + 1], dead_roots[NP + 1], waste[NP + 1]; } Stats;
static int cand_buf[NP + 2][MAXSOL];

static int narrow(const int *cand, int n, int m, int *out) {
    int k = 0, p = ppiece[m];
    for (int i = 0; i < n; i++)
        if (sol[cand[i]][p] == m) out[k++] = cand[i];
    return k;
}

static long long dfs_piece(const int *order, int d, uint64_t filled, int ncand, int parent_alive, Stats *st) {
    st->nodes[d]++;
    if (ncand) st->alive[d]++;
    else if (parent_alive) st->dead_roots[d]++;
    long long size = 1;
    if (d < NP) {
        int p = order[d];
        for (int i = 0; i < nvalid[p]; i++) {
            int m = valid[p][i];
            if (pmask[m] & filled) continue;
            int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
            size += dfs_piece(order, d + 1, filled | pmask[m], n, ncand > 0, st);
        }
    }
    if (!ncand && parent_alive) st->waste[d] += size;
    return size;
}

static int cell_order[NB], ncells_order;
static void build_order_row(void) {
    ncells_order = 0;
    for (int b = 0; b < NB; b++)
        if ((board & ~datem) >> b & 1) cell_order[ncells_order++] = b;
}

static long long dfs_cell(bool mrv, int d, uint64_t filled, int used, int ncand, int parent_alive, Stats *st) {
    st->nodes[d]++;
    if (ncand) st->alive[d]++;
    else if (parent_alive) st->dead_roots[d]++;
    long long size = 1;
    if (d < NP) {
        int cell = -1;
        if (mrv) {
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
            size += dfs_cell(mrv, d + 1, filled | pmask[m], used | 1 << ppiece[m], n, ncand > 0, st);
        }
    }
    if (!ncand && parent_alive) st->waste[d] += size;
    return size;
}

static void start_cands(void) {
    for (int i = 0; i < nsol; i++) cand_buf[0][i] = i;
}

typedef struct { Stats st[3]; int dates; } H11;

static void h11_date(int m, int d, void *ctx) {
    H11 *g = ctx;
    load(m, d);
    if (!nsol) return;
    int pent[NP] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    build_order_row();
    Stats a;
    for (int s = 0; s < 3; s++) {
        memset(&a, 0, sizeof a);
        start_cands();
        if (s == 0) dfs_cell(false, 0, datem, 0, nsol, 1, &a);
        else if (s == 1) dfs_cell(true, 0, datem, 0, nsol, 1, &a);
        else dfs_piece(pent, 0, datem, nsol, 1, &a);
        for (int k = 0; k <= NP; k++) {
            g->st[s].nodes[k] += a.nodes[k], g->st[s].alive[k] += a.alive[k];
            g->st[s].dead_roots[k] += a.dead_roots[k], g->st[s].waste[k] += a.waste[k];
        }
    }
    g->dates++;
}

static void cmd_h11(int step) {
    H11 *g = calloc(1, sizeof *g);
    each_date(h11_date, g, step);
    const char *lab[3] = {"row by row (the solver's)", "most constrained cell", "pentominoes first (L5 Z5 X5 P5 U5 T4 O4 S4 L4)"};
    printf("H11: where dead roots begin (first dead node below an alive parent), pooled over %d dates (every %dth)\n", g->dates, step);
    for (int s = 0; s < 3; s++) {
        Stats *a = &g->st[s];
        long long dr = 0, w = 0;
        for (int k = 0; k <= NP; k++) dr += a->dead_roots[k], w += a->waste[k];
        printf("  %s: %lld dead roots, %lld wasted nodes (%.1f per root)\n", lab[s], dr, w, (double)w / (double)dr);
        printf("    depth of dead root    ");
        for (int k = 1; k <= NP; k++) printf("%9d", k);
        printf("\n    share of dead roots   ");
        for (int k = 1; k <= NP; k++) printf("%8.1f%%", 100.0 * (double)a->dead_roots[k] / (double)dr);
        printf("\n    share of wasted nodes ");
        for (int k = 1; k <= NP; k++) printf("%8.1f%%", 100.0 * (double)a->waste[k] / (double)w);
        printf("\n    P(fatal | alive parent)");
        for (int k = 1; k <= NP; k++) {
            long long ch = a->alive[k] + a->dead_roots[k];
            printf("%8.1f%%", ch ? 100.0 * (double)a->dead_roots[k] / (double)ch : 0.0);
        }
        printf("\n");
    }
    free(g);
}

/* ================= H12 ================= */
typedef enum { P_CHRONO, P_JUMP, P_RESTART, P_ORACLE, P_SHALLOW, P_N } Policy;
static const char *const POLICY[P_N] = {"undo one move at a time", "jump back to last position with >2 options", "restart from empty",
                                        "oracle: jump to deepest alive position (lower bound)",
                                        "jump back to the last position at depth <=3"};
static const int PENT[NP] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

typedef struct { long long placements, removals, actions; bool ok; } Trial;

static Trial run_trial(Policy pol, long long cap) {
    static int list[NP + 1][MAXPL];
    int nlist[NP + 1], nxt[NP + 1], ncand[NP + 1];
    uint64_t filled[NP + 1];
    Trial t = {0, 0, 0, false};
    int d = 0;
#define ENTER(dd)                                                                              \
    do {                                                                                       \
        int p_ = PENT[dd], k_ = 0;                                                             \
        for (int i_ = 0; i_ < nvalid[p_]; i_++)                                                \
            if (!(pmask[valid[p_][i_]] & filled[dd])) list[dd][k_++] = valid[p_][i_];          \
        shuffle(list[dd], k_);                                                                 \
        nlist[dd] = k_, nxt[dd] = 0;                                                           \
    } while (0)
    filled[0] = datem, ncand[0] = nsol;
    start_cands();
    ENTER(0);
    while (t.placements < cap) {
        if (d == NP) {
            t.ok = true;
            return t;
        }
        if (nxt[d] < nlist[d]) {
            int m = list[d][nxt[d]++];
            t.placements++, t.actions++;
            filled[d + 1] = filled[d] | pmask[m];
            ncand[d + 1] = ncand[d] ? narrow(cand_buf[d], ncand[d], m, cand_buf[d + 1]) : 0;
            d++;
            if (d < NP) ENTER(d);
            continue;
        }
        /* stuck: nothing untried here */
        int target = -1;
        if (pol == P_CHRONO) target = d - 1;
        else if (pol == P_JUMP) {
            for (int a = d - 1; a >= 0; a--)
                if (nlist[a] > 2 && nxt[a] < nlist[a]) { target = a; break; }
        } else if (pol == P_ORACLE) {
            for (int a = d - 1; a >= 0; a--)
                if (ncand[a] > 0 && nxt[a] < nlist[a]) { target = a; break; }
        }
        else if (pol == P_SHALLOW) {
            for (int a = imin(d - 1, 2); a >= 0; a--)
                if (nxt[a] < nlist[a]) { target = a; break; }
        }
        if (target < 0) { /* restart */
            t.removals += d, t.actions += 1;
            d = 0;
            ENTER(0);
        } else {
            t.removals += d - target, t.actions += d - target;
            d = target;
        }
    }
    return t;
}

static int cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return x < y ? -1 : x > y;
}

static void cmd_h12(int m, int d, int trials) {
    load(m, d);
    rng ^= (uint64_t)(m * 31 + d) * 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 10; i++) rnd();
    long long cap = 20000000;
    for (int pol = 0; pol < P_N; pol++) {
        long long *pl = malloc(sizeof *pl * (size_t)trials);
        double pls = 0, rem = 0, act = 0;
        int fail = 0;
        for (int i = 0; i < trials; i++) {
            Trial t = run_trial((Policy)pol, cap);
            pl[i] = t.placements;
            pls += (double)t.placements, rem += (double)t.removals, act += (double)t.actions;
            fail += !t.ok;
        }
        qsort(pl, (size_t)trials, sizeof *pl, cmp_ll);
        printf("H12 %02d-%02d sols=%d %d | trials=%d | mean_placements=%.1f median=%lld p90=%lld mean_removals=%.1f mean_actions=%.1f fails=%d | %s\n",
               m, d, nsol, pol, trials, pls / trials, pl[trials / 2], pl[trials * 9 / 10], rem / trials, act / trials, fail,
               POLICY[pol]);
        fflush(stdout);
        free(pl);
    }
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "";
    if (!strcmp(cmd, "h9")) cmd_h9();
    else if (!strcmp(cmd, "h10")) cmd_h10();
    else if (!strcmp(cmd, "h11")) cmd_h11(argc > 2 ? atoi(argv[2]) : 12);
    else if (!strcmp(cmd, "h12") && argc > 2) {
        int m, d;
        if (sscanf(argv[2], "%d-%d", &m, &d) != 2 || !sv_puzzle_set_date(m, d)) return fprintf(stderr, "bad date\n"), 2;
        cmd_h12(m, d, argc > 3 ? atoi(argv[3]) : 2000);
    } else return fprintf(stderr, "usage: sv_exp_dates h9|h10|h11 [step]|h12 MM-DD [trials]\n"), 2;
    return 0;
}
