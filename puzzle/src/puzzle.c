#include "puzzle.h"

#include <stdio.h>
#include <string.h>

/* Shapes as they sit in the tray (orientation 0). */
static const char *const SHAPES[PZ_PIECES][4] = {
    {"XXXX", "X...", NULL},     /* L5 */
    {"XX.", ".X.", ".XX", NULL}, /* Z5 */
    {".X.", "XXX", ".X.", NULL}, /* X5 */
    {"XX", "XX", "X.", NULL},   /* P5 */
    {"X.X", "XXX", NULL},       /* U5 */
    {"XXX", ".X.", NULL},       /* T4 */
    {"XX", "XX", NULL},         /* O4 */
    {".XX", "XX.", NULL},       /* S4 */
    {"XXX", "X..", NULL},       /* L4 */
};

static const PzPlace TRAY[PZ_PIECES] = {
    {0, 8, 0}, {0, 0, 8}, {0, 0, 12}, {0, 0, 16}, {0, 4, 12},
    {0, 4, 8}, {0, 4, 16}, {0, 7, 8}, {0, 7, 12},
};

static PzCell base[PZ_PIECES][PZ_MAX_CELLS];
static int base_n[PZ_PIECES];

static void init_shapes(void) {
    static bool done;
    if (done) return;
    done = true;
    for (int p = 0; p < PZ_PIECES; p++)
        for (int r = 0; SHAPES[p][r]; r++)
            for (int c = 0; SHAPES[p][r][c]; c++)
                if (SHAPES[p][r][c] == 'X') base[p][base_n[p]++] = (PzCell){r, c};
}

int pz_piece_size(int p) {
    init_shapes();
    return base_n[p];
}

int pz_piece_cells(int p, int orient, PzCell out[PZ_MAX_CELLS]) {
    init_shapes();
    int n = base_n[p], minr = 99, minc = 99;
    for (int i = 0; i < n; i++) {
        int r = base[p][i].r, c = base[p][i].c;
        if (orient & 4) c = -c;
        for (int k = 0; k < (orient & 3); k++) { /* clockwise: (r,c) -> (c,-r) */
            int t = r;
            r = c;
            c = -t;
        }
        out[i] = (PzCell){r, c};
        if (r < minr) minr = r;
        if (c < minc) minc = c;
    }
    for (int i = 0; i < n; i++) out[i].r -= minr, out[i].c -= minc;
    return n;
}

int pz_rotate(int orient) { return (orient & 4) | ((orient + 1) & 3); }
/* Mirroring after k turns equals k turns the other way after mirroring. */
int pz_flip(int orient) { return ((orient & 4) ^ 4) | ((4 - (orient & 3)) & 3); }

/* ------------------------------------------------------------------ */
/* Board. */

static const char *const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const DAYS[32] = {"",   "1",  "2",  "3",  "4",  "5",  "6",  "7",
                                     "8",  "9",  "10", "11", "12", "13", "14", "15",
                                     "16", "17", "18", "19", "20", "21", "22", "23",
                                     "24", "25", "26", "27", "28", "29", "30", "31"};

bool pz_board_cell(int r, int c) {
    if (r < 0 || c < 0 || r >= PZ_BOARD || c >= PZ_BOARD) return false;
    if (r < 2) return c < 6;
    if (r < 6) return true;
    return c >= 2 && c <= 4;
}

bool pz_board_gap(int r, int c) {
    return r >= 0 && c >= 0 && r < PZ_BOARD && c < PZ_BOARD && !pz_board_cell(r, c);
}

const char *pz_label(int r, int c) {
    if (!pz_board_cell(r, c)) return NULL;
    if (r < 2) return MONTHS[r * 6 + c];
    if (r < 6) return DAYS[(r - 2) * 7 + c + 1];
    return DAYS[29 + c - 2];
}

void pz_date_cells(int month, int day, PzCell out[2]) {
    out[0] = (PzCell){(month - 1) / 6, (month - 1) % 6};
    if (day <= 28) out[1] = (PzCell){2 + (day - 1) / 7, (day - 1) % 7};
    else out[1] = (PzCell){6, 2 + day - 29};
}

/* ------------------------------------------------------------------ */
/* Placement. */

void pz_tray(PzPlace out[PZ_PIECES]) { memcpy(out, TRAY, sizeof TRAY); }

static bool covers(PzPlace pl, int p, int r, int c) {
    PzCell cells[PZ_MAX_CELLS];
    int n = pz_piece_cells(p, pl.orient, cells);
    for (int i = 0; i < n; i++)
        if (pl.r + cells[i].r == r && pl.c + cells[i].c == c) return true;
    return false;
}

int pz_owner(const PzPlace pl[PZ_PIECES], int r, int c) {
    for (int p = 0; p < PZ_PIECES; p++)
        if (covers(pl[p], p, r, c)) return p;
    return -1;
}

bool pz_fits(const PzPlace pl[PZ_PIECES], int p, PzPlace at) {
    PzCell cells[PZ_MAX_CELLS];
    int n = pz_piece_cells(p, at.orient, cells);
    for (int i = 0; i < n; i++) {
        int r = at.r + cells[i].r, c = at.c + cells[i].c;
        if (r < 0 || c < 0 || r >= PZ_TABLE_H || c >= PZ_TABLE_W) return false;
        if (pz_board_gap(r, c)) return false;
        for (int q = 0; q < PZ_PIECES; q++)
            if (q != p && covers(pl[q], q, r, c)) return false;
    }
    return true;
}

bool pz_solved(const PzPlace pl[PZ_PIECES], int month, int day) {
    PzCell d[2];
    pz_date_cells(month, day, d);
    for (int r = 0; r < PZ_BOARD; r++)
        for (int c = 0; c < PZ_BOARD; c++) {
            if (!pz_board_cell(r, c)) continue;
            bool date = (r == d[0].r && c == d[0].c) || (r == d[1].r && c == d[1].c);
            if ((pz_owner(pl, r, c) >= 0) == date) return false;
        }
    return true;
}

/* ------------------------------------------------------------------ */
/* Solver: exact cover by backtracking on a 64-bit board mask. Always fill
   the lowest empty cell; each placement is indexed by its lowest cell. */

typedef struct {
    uint64_t mask;
    int8_t piece, orient, r, c;
} Placement;

#define MAX_PLACEMENTS 2048
static Placement places[MAX_PLACEMENTS];
static int by_cell_start[PZ_BOARD * PZ_BOARD + 1];
static bool places_ready;

static int bit(int r, int c) { return r * PZ_BOARD + c; }

static void build_placements(void) {
    if (places_ready) return;
    places_ready = true;
    int n = 0;
    for (int b = 0; b < PZ_BOARD * PZ_BOARD; b++) {
        by_cell_start[b] = n;
        for (int p = 0; p < PZ_PIECES; p++) {
            uint64_t seen[8];
            int nseen = 0;
            for (int o = 0; o < 8; o++)
                for (int r = 0; r < PZ_BOARD; r++)
                    for (int c = 0; c < PZ_BOARD; c++) {
                        PzCell cells[PZ_MAX_CELLS];
                        int k = pz_piece_cells(p, o, cells);
                        uint64_t m = 0;
                        bool ok = true;
                        for (int i = 0; i < k && ok; i++) {
                            int rr = r + cells[i].r, cc = c + cells[i].c;
                            if (!pz_board_cell(rr, cc)) ok = false;
                            else m |= 1ull << bit(rr, cc);
                        }
                        if (!ok || __builtin_ctzll(m) != b) continue;
                        /* Skip orientations that repeat an earlier one here. */
                        bool dup = false;
                        for (int s = 0; s < nseen; s++) dup |= seen[s] == m;
                        if (dup) continue;
                        if (nseen < 8) seen[nseen++] = m;
                        places[n++] = (Placement){m, (int8_t)p, (int8_t)o, (int8_t)r, (int8_t)c};
                    }
        }
    }
    by_cell_start[PZ_BOARD * PZ_BOARD] = n;
}

typedef struct {
    uint64_t full;
    int used, count, max;
    PzPlace cur[PZ_PIECES];
    PzPlace *out;
} Search;

static void search(Search *s, uint64_t filled) {
    if (filled == s->full) {
        if (s->count < s->max) memcpy(s->out + (size_t)s->count * PZ_PIECES, s->cur, sizeof s->cur);
        s->count++;
        return;
    }
    int b = __builtin_ctzll(~filled & s->full);
    for (int i = by_cell_start[b]; i < by_cell_start[b + 1]; i++) {
        const Placement *pl = &places[i];
        if ((s->used >> pl->piece) & 1 || (pl->mask & filled)) continue;
        s->used |= 1 << pl->piece;
        s->cur[(int)pl->piece] = (PzPlace){pl->orient, pl->r, pl->c};
        search(s, filled | pl->mask);
        s->used &= ~(1 << pl->piece);
    }
}

int pz_solve(int month, int day, PzPlace *out, int max) {
    build_placements();
    Search s = {0};
    for (int r = 0; r < PZ_BOARD; r++)
        for (int c = 0; c < PZ_BOARD; c++)
            if (pz_board_cell(r, c)) s.full |= 1ull << bit(r, c);
    PzCell d[2];
    pz_date_cells(month, day, d);
    uint64_t date = (1ull << bit(d[0].r, d[0].c)) | (1ull << bit(d[1].r, d[1].c));
    s.out = out;
    s.max = out ? max : 0;
    search(&s, date);
    return s.count;
}

/* ------------------------------------------------------------------ */
/* Calendar. */

int pz_days_in_month(int year, int month) {
    static const int N[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return N[month - 1];
}

int pz_weekday(int year, int month, int day) { /* Sakamoto */
    static const int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 + T[month - 1] + day) % 7;
}

const char *pz_month_name(int month) {
    static const char *const N[12] = {"January", "February", "March",     "April",   "May",      "June",
                                      "July",    "August",   "September", "October", "November", "December"};
    return N[month - 1];
}

const char *pz_weekday_name(int wd) {
    static const char *const N[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return N[wd];
}

/* ------------------------------------------------------------------ */
/* Log of solved dates. */

bool pz_log_has(const PzLog *l, int32_t date) {
    for (int i = 0; i < l->n; i++)
        if (l->dates[i] == date) return true;
    return false;
}

bool pz_log_add(PzLog *l, int32_t date) {
    if (pz_log_has(l, date) || l->n >= PZ_LOG_MAX) return false;
    l->dates[l->n++] = date;
    return true;
}

int pz_log_count_year(const PzLog *l, int year) {
    int n = 0;
    for (int i = 0; i < l->n; i++) n += l->dates[i] / 10000 == year;
    return n;
}

bool pz_log_load(PzLog *l, const char *path) {
    l->n = 0;
    FILE *f = fopen(path, "r");
    if (!f) return true;
    char line[64];
    while (fgets(line, sizeof line, f)) {
        int y, m, d;
        if (sscanf(line, "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
            pz_log_add(l, pz_date(y, m, d));
    }
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool pz_log_save(const PzLog *l, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    for (int i = 0; i < l->n; i++)
        fprintf(f, "%04d-%02d-%02d\n", l->dates[i] / 10000, l->dates[i] / 100 % 100, l->dates[i] % 100);
    return fclose(f) == 0;
}
