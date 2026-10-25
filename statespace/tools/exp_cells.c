/* Experiments on cell choice and pruning for the Whole Year Puzzle.

   Usage: sv_exp_cells h5 [all|sample|MM-DD]      corner/notch cell rankings
          sv_exp_cells prune [all|sample|MM-DD]   pocket/parity prunes, cell rules
          sv_exp_cells pprune [few|MM-DD]         pocket/parity prunes, piece orders
          sv_exp_cells sweep                      row vs column sweep per date

   Trees are searched to exhaustion as in insights.c. A node is alive if some
   solution contains it. A pruned node still counts as visited (it was
   generated and tested) but is not expanded. */
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
#define ALLP ((1 << NP) - 1)
#define MAXSZ 42

static const char *const NAME[NP] = {"L5", "Z5", "X5", "P5", "U5", "T4", "O4", "S4", "L4"};

static uint64_t pmask[MAXPL];
static int ppiece[MAXPL];
static int valid[NP][MAXPL], nvalid[NP];
static int cover[NB][MAXPL], ncover[NB];
static uint64_t board, datem, DARKM, COL0, COL6;
static int date_cell[2];
static int nsol;
static int16_t sol[MAXSOL][NP];
static int psz[NP];
static uint64_t f_tab[1 << NP][MAXSZ];   /* subset T of pieces placed exactly: bit d set if size s, d dark cells */
static uint64_t ach[1 << NP][MAXSZ];     /* union over nonempty T within S */

static int popc(uint64_t m) { return __builtin_popcountll(m); }
static int imin(int a, int b) { return a < b ? a : b; }

static void emit(void *ctx, const void *s) {
    (void)ctx;
    if (nsol >= MAXSOL) return;
    for (int p = 0; p < NP; p++) sol[nsol][p] = (int16_t)sv_puzzle_placed(s, p);
    nsol++;
}

static void build_tables(void) {
    uint64_t dk[NP];
    for (int p = 0; p < NP; p++) {
        dk[p] = 0;
        for (int i = 0; i < nvalid[p]; i++) dk[p] |= 1ull << popc(pmask[valid[p][i]] & DARKM);
        psz[p] = popc(pmask[valid[p][0]]);
    }
    memset(f_tab, 0, sizeof f_tab);
    f_tab[0][0] = 1;
    for (int T = 1; T <= ALLP; T++) {
        int p = __builtin_ctz((unsigned)T), R = T & (T - 1);
        for (int s = psz[p]; s < MAXSZ; s++)
            for (int k = 0; k < 8; k++)
                if (dk[p] >> k & 1) f_tab[T][s] |= f_tab[R][s - psz[p]] << k;
    }
    memset(ach, 0, sizeof ach);
    for (int S = 1; S <= ALLP; S++)
        for (int T = S; T; T = (T - 1) & S)
            for (int s = 0; s < MAXSZ; s++) ach[S][s] |= f_tab[T][s];
}

static void load(int month, int day) {
    sv_puzzle_set_date(month, day);
    board = sv_puzzle_board_mask(), datem = sv_puzzle_date_mask();
    DARKM = COL0 = COL6 = 0;
    for (int b = 0; b < NB; b++) {
        if (((b / PZ_BOARD) + (b % PZ_BOARD)) & 1) DARKM |= 1ull << b;
        if (b % PZ_BOARD == 0) COL0 |= 1ull << b;
        if (b % PZ_BOARD == PZ_BOARD - 1) COL6 |= 1ull << b;
    }
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
    build_tables();
}

static int dist(int a, int b) { return abs(a / PZ_BOARD - b / PZ_BOARD) + abs(a % PZ_BOARD - b % PZ_BOARD); }
static int near_date(int b) { return imin(dist(b, date_cell[0]), dist(b, date_cell[1])); }

static int nb_count(int b) {
    int r = b / PZ_BOARD, c = b % PZ_BOARD, n = 0;
    n += r > 0 && (board >> (b - PZ_BOARD) & 1);
    n += r + 1 < PZ_BOARD && (board >> (b + PZ_BOARD) & 1);
    n += c > 0 && (board >> (b - 1) & 1);
    n += c + 1 < PZ_BOARD && (board >> (b + 1) & 1);
    return n;
}

static uint64_t nbr(uint64_t x) { return (x << 7) | (x >> 7) | ((x << 1) & ~COL0) | ((x >> 1) & ~COL6); }

/* Prune modes: 1 = every empty pocket's size must be a sum of unplaced piece
   sizes; 2 = the empty cells' dark/light split must be reachable by the
   unplaced pieces overall; 4 = per pocket, size and dark count together.
   All are sound (they never prune a node on a path to a solution). */
static int pruned(uint64_t filled, int used, int mode) {
    if (!mode) return 0;
    uint64_t empty = board & ~filled;
    int S = ALLP & ~used;
    if (mode & 2) {
        int tot = popc(empty), D = popc(empty & DARKM);
        if (tot >= MAXSZ || !(f_tab[S][tot] >> D & 1)) return 1;
    }
    if (mode & 5) {
        uint64_t rest = empty;
        while (rest) {
            uint64_t reg = rest & (0 - rest), prev;
            do {
                prev = reg;
                reg = (reg | nbr(reg)) & empty;
            } while (reg != prev);
            rest &= ~reg;
            int sz = popc(reg);
            if (sz >= MAXSZ) return 1;
            uint64_t row = ach[S][sz];
            if (mode & 4) {
                if (!(row >> popc(reg & DARKM) & 1)) return 1;
            } else if (!row) return 1;
        }
    }
    return 0;
}

typedef struct {
    long long nodes, alive, roots, caught, viol;
} St;

static int cand_buf[NP + 2][MAXSOL];

static int narrow(const int *cand, int n, int m, int *out) {
    int k = 0, p = ppiece[m];
    for (int i = 0; i < n; i++)
        if (sol[cand[i]][p] == m) out[k++] = cand[i];
    return k;
}

/* Common bookkeeping for a visited node; returns true if it is a leaf. */
static int visit(St *st, int ncand, int parent_alive, uint64_t filled, int used, int mode) {
    st->nodes++;
    if (ncand) st->alive++;
    else if (parent_alive) st->roots++;
    if (pruned(filled, used, mode)) {
        if (ncand) st->viol++;
        else if (parent_alive) st->caught++;
        return 1;
    }
    return 0;
}

static void dfs_piece(const int *order, int d, uint64_t filled, int used, int ncand, int pa, int mode, St *st) {
    if (visit(st, ncand, pa, filled, used, mode) || d == NP) return;
    int p = order[d];
    for (int i = 0; i < nvalid[p]; i++) {
        int m = valid[p][i];
        if (pmask[m] & filled) continue;
        int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
        dfs_piece(order, d + 1, filled | pmask[m], used | 1 << p, n, ncand > 0, mode, st);
    }
}

typedef enum { C_ROW, C_COL, C_EDGE, C_EDGE_NEAR, C_MRV, C_DYNNB, C_DYNNB_MRV, C_KINDS } CellRule;
static const char *const CELL_RULE[C_KINDS] = {
    "row by row (the solver's)",         "column by column",           "static: fewest neighbors, then row",
    "static: fewest neighbors, near date", "most constrained cell",    "dynamic: fewest empty neighbors, then row",
    "dynamic: fewest empty nbrs, then MRV"};
static int cell_order[NB], ncells_order;

static int key_of(CellRule rule, int b) {
    switch (rule) {
    case C_COL: return b % PZ_BOARD * 100 + b / PZ_BOARD;
    case C_EDGE: return nb_count(b) * 100 + b;
    case C_EDGE_NEAR: return nb_count(b) * 10000 + near_date(b) * 100 + b;
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

static int options(int b, uint64_t filled, int used) {
    int k = 0;
    for (int j = 0; j < ncover[b]; j++) {
        int m = cover[b][j];
        k += !(used >> ppiece[m] & 1) && !(pmask[m] & filled);
    }
    return k;
}

static int pick_cell(CellRule rule, uint64_t filled, int used) {
    uint64_t empty = board & ~filled;
    int cell = -1;
    if (rule == C_MRV || rule == C_DYNNB || rule == C_DYNNB_MRV) {
        long best = 1l << 40;
        for (int i = 0; i < ncells_order; i++) {
            int b = cell_order[i];
            if (filled >> b & 1) continue;
            long key;
            if (rule == C_MRV) key = options(b, filled, used);
            else if (rule == C_DYNNB) key = popc(nbr(1ull << b) & empty);
            else key = popc(nbr(1ull << b) & empty) * 1000 + options(b, filled, used);
            if (key < best) best = key, cell = b;
        }
    } else {
        for (int i = 0; i < ncells_order && cell < 0; i++)
            if (!(filled >> cell_order[i] & 1)) cell = cell_order[i];
    }
    return cell;
}

static void dfs_cell(CellRule rule, int d, uint64_t filled, int used, int ncand, int pa, int mode, St *st) {
    if (visit(st, ncand, pa, filled, used, mode) || d == NP) return;
    int cell = pick_cell(rule, filled, used);
    for (int j = 0; j < ncover[cell]; j++) {
        int m = cover[cell][j];
        if ((used >> ppiece[m] & 1) || (pmask[m] & filled)) continue;
        int n = ncand ? narrow(cand_buf[d], ncand, m, cand_buf[d + 1]) : 0;
        dfs_cell(rule, d + 1, filled | pmask[m], used | 1 << ppiece[m], n, ncand > 0, mode, st);
    }
}

static St run_cell(CellRule rule, int mode) {
    St st;
    memset(&st, 0, sizeof st);
    build_order(rule);
    for (int i = 0; i < nsol; i++) cand_buf[0][i] = i;
    dfs_cell(rule, 0, datem, 0, nsol, 1, mode, &st);
    return st;
}

static St run_order(const int *order, int mode) {
    St st;
    memset(&st, 0, sizeof st);
    for (int i = 0; i < nsol; i++) cand_buf[0][i] = i;
    dfs_piece(order, 0, datem, 0, nsol, 1, mode, &st);
    return st;
}

/* ---- date lists ---- */
typedef struct { int m, d; } Date;
static Date dates[400];
static int ndates;

static void pick_dates(const char *arg) {
    ndates = 0;
    int m, d;
    if (!strcmp(arg, "all")) {
        for (m = 1; m <= 12; m++)
            for (d = 1; d <= pz_days_in_month(2024, m); d++) dates[ndates++] = (Date){m, d};
    } else if (!strcmp(arg, "sample")) { /* the 5th and 20th of every month, Oct 5 among them */
        for (m = 1; m <= 12; m++) dates[ndates++] = (Date){m, 5}, dates[ndates++] = (Date){m, 20};
    } else if (!strcmp(arg, "few")) {
        static const Date few[] = {{10, 5}, {12, 25}, {3, 14}, {6, 29}, {1, 28}};
        for (unsigned i = 0; i < sizeof few / sizeof *few; i++) dates[ndates++] = few[i];
    } else if (sscanf(arg, "%d-%d", &m, &d) == 2 && sv_puzzle_set_date(m, d)) {
        dates[ndates++] = (Date){m, d};
    } else {
        fprintf(stderr, "bad dates %s\n", arg);
        exit(2);
    }
}

/* ---- hypothesis 5 ---- */
static void cmd_h5(const char *arg) {
    pick_dates(arg);
    static const CellRule rules[] = {C_ROW, C_COL, C_EDGE, C_EDGE_NEAR, C_DYNNB, C_DYNNB_MRV, C_MRV};
    enum { NR = sizeof rules / sizeof *rules };
    double nodes[NR] = {0}, lratio[NR] = {0}, alive[NR] = {0};
    int wins[NR] = {0};
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        long long n[NR], base = 0;
        for (int r = 0; r < NR; r++) {
            St st = run_cell(rules[r], 0);
            n[r] = st.nodes, nodes[r] += (double)st.nodes, alive[r] += (double)st.alive;
            if (r == 0) base = st.nodes;
        }
        for (int r = 0; r < NR; r++) lratio[r] += log((double)n[r] / (double)base), wins[r] += n[r] < base;
    }
    printf("H5: cell rules over %d dates (no prune). nodes = mean tree size per date\n", ndates);
    printf("  %-40s %12s %10s %10s %12s\n", "rule", "nodes/date", "mean/row", "geomean/row", "beats row");
    for (int r = 0; r < NR; r++)
        printf("  %-40s %12.0f %9.2fx %9.2fx %8d/%d\n", CELL_RULE[rules[r]], nodes[r] / ndates, nodes[r] / nodes[0],
               exp(lratio[r] / ndates), wins[r], ndates);
}

/* ---- hypotheses 6 and 7 ---- */
static const int MODES[] = {0, 1, 2, 3, 7};
static const char *const MODE_NAME[] = {"none", "pocket sizes", "parity (global)", "pocket + parity(global)",
                                        "pocket + parity per pocket"};
#define NM 5

static void cmd_prune(const char *arg) {
    pick_dates(arg);
    static const CellRule rules[] = {C_ROW, C_MRV};
    for (unsigned ri = 0; ri < 2; ri++) {
        double nodes[NM] = {0}, caught[NM] = {0}, roots[NM] = {0}, lr[NM] = {0};
        long long viol = 0;
        for (int i = 0; i < ndates; i++) {
            load(dates[i].m, dates[i].d);
            long long base = 0;
            for (int k = 0; k < NM; k++) {
                St st = run_cell(rules[ri], MODES[k]);
                if (k == 0) base = st.nodes;
                nodes[k] += (double)st.nodes, caught[k] += (double)st.caught, roots[k] += (double)st.roots;
                viol += st.viol;
                lr[k] += log((double)st.nodes / (double)base);
            }
        }
        printf("\nH6/H7: %s over %d dates (violations = alive nodes pruned: %lld)\n", CELL_RULE[rules[ri]], ndates, viol);
        printf("  %-28s %12s %9s %12s %14s\n", "prune", "nodes/date", "vs none", "geomean", "dead roots caught");
        for (int k = 0; k < NM; k++)
            printf("  %-28s %12.0f %8.2fx %11.2fx %13.1f%%\n", MODE_NAME[k], nodes[k] / ndates, nodes[k] / nodes[0],
                   exp(lr[k] / ndates), 100.0 * caught[k] / roots[k]);
    }
}

static void cmd_pprune(const char *arg) {
    pick_dates(arg);
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        int pent[NP] = {0, 1, 2, 3, 4, 5, 6, 7, 8}, tet[NP] = {5, 6, 7, 8, 0, 1, 2, 3, 4}, few[NP];
        for (int p = 0; p < NP; p++) few[p] = p;
        for (int a = 1; a < NP; a++)
            for (int j = a; j > 0 && nvalid[few[j - 1]] > nvalid[few[j]]; j--) {
                int t = few[j];
                few[j] = few[j - 1], few[j - 1] = t;
            }
        struct { const char *label; int *order; } named[] = {
            {"pentominoes first", pent}, {"fewest placements first", few}, {"tetrominoes first", tet}};
        printf("\nH6/H7 piece orders, %s %d (%d solutions)\n", pz_month_name(dates[i].m), dates[i].d, nsol);
        for (int o = 0; o < 3; o++) {
            long long base = 0;
            for (int k = 0; k < NM; k++) {
                St st = run_order(named[o].order, MODES[k]);
                if (k == 0) base = st.nodes;
                printf("  %-24s %-28s nodes %12lld  %7.2fx  caught %5.1f%%  viol %lld\n", named[o].label, MODE_NAME[k],
                       st.nodes, (double)st.nodes / (double)base, 100.0 * (double)st.caught / (double)st.roots, st.viol);
            }
        }
    }
}

/* ---- hypothesis 8 ---- */
static double rank_of(CellRule rule, int h) {
    int r = 0;
    for (int b = 0; b < NB; b++)
        if (board >> b & 1) r += key_of(rule, b) < key_of(rule, h);
    return r;
}

static double corr(const double *x, const double *y, int n) {
    double mx = 0, my = 0, sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < n; i++) mx += x[i], my += y[i];
    mx /= n, my /= n;
    for (int i = 0; i < n; i++) sxy += (x[i] - mx) * (y[i] - my), sxx += (x[i] - mx) * (x[i] - mx), syy += (y[i] - my) * (y[i] - my);
    return sxy / sqrt(sxx * syy);
}

static void cmd_sweep(void) {
    pick_dates("all");
    static double nr[400], nc[400], dlate[400], dlast[400], lrat[400], lrow[400], rowlate[400];
    double sr = 0, sc = 0, sbest = 0;
    int row_wins = 0, ties = 0;
    for (int i = 0; i < ndates; i++) {
        load(dates[i].m, dates[i].d);
        nr[i] = (double)run_cell(C_ROW, 0).nodes, nc[i] = (double)run_cell(C_COL, 0).nodes;
        double ra = (rank_of(C_ROW, date_cell[0]) + rank_of(C_ROW, date_cell[1])) / 2;
        double ca = (rank_of(C_COL, date_cell[0]) + rank_of(C_COL, date_cell[1])) / 2;
        double rl = fmax(rank_of(C_ROW, date_cell[0]), rank_of(C_ROW, date_cell[1]));
        double cl = fmax(rank_of(C_COL, date_cell[0]), rank_of(C_COL, date_cell[1]));
        dlate[i] = ra - ca; /* >0: the holes come later in the row sweep */
        dlast[i] = rl - cl;
        rowlate[i] = ra;
        lrat[i] = log(nc[i] / nr[i]); /* >0: the row sweep is cheaper */
        lrow[i] = log(nr[i]);
        sr += nr[i], sc += nc[i], sbest += nr[i] < nc[i] ? nr[i] : nc[i];
        row_wins += nr[i] < nc[i], ties += nr[i] == nc[i];
    }
    int agree_avg = 0, agree_last = 0, used_avg = 0, used_last = 0;
    for (int i = 0; i < ndates; i++) {
        if (dlate[i] != 0 && nr[i] != nc[i]) used_avg++, agree_avg += (dlate[i] > 0) == (nr[i] < nc[i]);
        if (dlast[i] != 0 && nr[i] != nc[i]) used_last++, agree_last += (dlast[i] > 0) == (nr[i] < nc[i]);
    }
    printf("H8: row vs column sweep over %d dates\n", ndates);
    printf("  mean nodes: row %.0f, column %.0f, per-date best of the two %.0f (%.1f%% below row)\n", sr / ndates,
           sc / ndates, sbest / ndates, 100.0 * (1 - sbest / sr));
    printf("  row wins %d, column wins %d, ties %d\n", row_wins, ndates - row_wins - ties, ties);
    printf("  corr(log col/row, rank gap of the average hole) = %+.3f\n", corr(dlate, lrat, ndates));
    printf("  corr(log col/row, rank gap of the later hole)   = %+.3f\n", corr(dlast, lrat, ndates));
    printf("  'sweep with the holes LATER wins': %d/%d (avg hole), %d/%d (later hole)\n", agree_avg, used_avg,
           agree_last, used_last);
    printf("  so 'holes EARLIER wins' is right %d/%d, %d/%d\n", used_avg - agree_avg, used_avg, used_last - agree_last,
           used_last);
    printf("  corr(log row nodes, average hole rank in the row sweep) = %+.3f\n", corr(rowlate, lrow, ndates));
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "";
    const char *arg = argc > 2 ? argv[2] : "sample";
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!strcmp(cmd, "h5")) cmd_h5(arg);
    else if (!strcmp(cmd, "prune")) cmd_prune(arg);
    else if (!strcmp(cmd, "pprune")) cmd_pprune(argc > 2 ? arg : "few");
    else if (!strcmp(cmd, "sweep")) cmd_sweep();
    else return fprintf(stderr, "usage: sv_exp_cells h5|prune [all|sample|MM-DD] | pprune [few|MM-DD] | sweep\n"), 2;
    return 0;
}
