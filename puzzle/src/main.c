/*
 * Puzzle - the Whole Year Puzzle on SDL3. Drag the nine pieces onto the
 * calendar board so only today's month and day show. Solved dates are kept
 * in a log; once a date is solved its solver unlocks.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "puzzle.h"
#include "tiny.h"

#define MAX_SOLUTIONS 1024

typedef struct {
    int x, y, w, h;
} Rect;

enum { BTN_NONE, BTN_TODAY, BTN_SOLVE, BTN_PREV, BTN_NEXT, BTN_DONE, BTN_COUNT };

typedef struct {
    int u, cs;          /* unit and table cell size, in pixels */
    int tx, ty;         /* table origin */
    int pad;            /* desktop margin around the table */
    int hy, hh;         /* header row */
    Rect btn[BTN_COUNT];
    int kx, ky, kp;     /* tracker: first square, pitch */
    int ix;             /* info text column */
} Layout;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TinyFont font;
    SDL_Cursor *arrow, *hand, *move;

    char log_path[1024];
    PzLog log;
    int ty, tm, td; /* today */
    int y, m, d;    /* the date being played */

    PzPlace pl[PZ_PIECES];
    int drag;       /* piece being dragged, or -1 */
    int grab;       /* which of its cells is held */
    int fx, fy;     /* where in that cell, in pixels */
    int dorient;    /* its orientation while dragged */
    int mx, my;     /* mouse, in pixels */
    int pressed;    /* button held down */

    bool solving;
    PzPlace *sols;
    int nsols, sol;
} App;

static int floor_div(int n, int d) { return n >= 0 ? n / d : -1 - (-n - 1) / d; }
static bool in_rect(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static int32_t selected(const App *a) { return pz_date(a->y, a->m, a->d); }
static bool unlocked(const App *a) { return pz_log_has(&a->log, selected(a)); }

/* ------------------------------------------------------------------ */
/* Layout. */

static const char *button_label(int b) {
    switch (b) {
    case BTN_TODAY: return "Today";
    case BTN_SOLVE: return "Solve";
    case BTN_PREV: return "‹";
    case BTN_NEXT: return "›";
    case BTN_DONE: return "Done";
    }
    return "";
}

static int button_width(int u, const char *label) {
    return SDL_max(tiny_text_width(label, TINY_BODY * u, TINY_BOLD) + 2 * TINY_PAD * u, 24 * u);
}

static Layout layout(const App *a) {
    Layout L = {0};
    int w, h;
    SDL_GetRenderOutputSize(a->ren, &w, &h);
    int u = L.u = tiny_unit(a->win, a->ren);
    int M = TINY_MARGIN * u, P = TINY_PAD * u;
    L.pad = P;
    L.hh = 22 * u;

    /* Height: header, table, tracker (12 rows at a quarter cell each). */
    int fixed_h = M + L.hh + P + L.pad + L.pad + 2 * P + M;
    int cs = SDL_min((h - fixed_h) * 4 / (PZ_TABLE_H * 4 + 12), (w - 2 * M - 2 * L.pad) / PZ_TABLE_W);
    L.cs = cs = SDL_max(cs / u, 8) * u;
    L.kp = SDL_max(cs / 4 / u, 4) * u;

    int tw = PZ_TABLE_W * cs, th = PZ_TABLE_H * cs;
    int total_h = L.hh + P + L.pad + th + L.pad + 2 * P + 12 * L.kp;
    L.tx = (w - tw) / 2;
    L.hy = SDL_max((h - total_h) / 2, M);
    L.ty = L.hy + L.hh + P + L.pad;
    L.ky = L.ty + th + L.pad + 2 * P;
    L.kx = L.tx - L.pad + 8 * u;
    L.ix = L.kx + 31 * L.kp + TINY_MARGIN * u;

    /* Buttons, right to left. */
    int bh = 20 * u, by = L.hy + (L.hh - bh) / 2, x = L.tx + tw + L.pad;
    const int order_solve[] = {BTN_DONE, BTN_NEXT, BTN_PREV}, order_play[] = {BTN_SOLVE, BTN_TODAY};
    const int *order = a->solving ? order_solve : order_play;
    int n = a->solving ? 3 : 2;
    for (int i = 0; i < n; i++) {
        int b = order[i];
        if (b == BTN_TODAY && selected(a) == pz_date(a->ty, a->tm, a->td)) continue;
        int bw = button_width(u, button_label(b));
        x -= bw;
        L.btn[b] = (Rect){x, by, bw, bh};
        x -= TINY_GAP * u;
    }
    return L;
}

/* The table cell under a pixel (may be outside the table). */
static void cell_at(const Layout *L, int x, int y, int *r, int *c) {
    *r = floor_div(y - L->ty, L->cs);
    *c = floor_div(x - L->tx, L->cs);
}

/* The piece and cell index under the mouse, or -1. */
static int piece_at(const App *a, const Layout *L, int *cell) {
    int r, c;
    cell_at(L, a->mx, a->my, &r, &c);
    int p = pz_owner(a->pl, r, c);
    if (p >= 0 && cell) {
        PzCell cells[PZ_MAX_CELLS];
        int n = pz_piece_cells(p, a->pl[p].orient, cells);
        for (int i = 0; i < n; i++)
            if (a->pl[p].r + cells[i].r == r && a->pl[p].c + cells[i].c == c) *cell = i;
    }
    return p;
}

/* The tracker day under the mouse; false if none. */
static bool day_at(const App *a, const Layout *L, int *m, int *d) {
    int col = floor_div(a->mx - L->kx, L->kp), row = floor_div(a->my - L->ky, L->kp);
    if (row < 0 || row >= 12 || col < 0 || col >= 31) return false;
    if (col + 1 > pz_days_in_month(a->y, row + 1)) return false;
    *m = row + 1;
    *d = col + 1;
    return true;
}

/* Where the dragged piece's bounding box is, in pixels, and where it would snap. */
static void drag_origin(const App *a, const Layout *L, int *x, int *y) {
    PzCell cells[PZ_MAX_CELLS];
    pz_piece_cells(a->drag, a->dorient, cells);
    *x = a->mx - cells[a->grab].c * L->cs - a->fx;
    *y = a->my - cells[a->grab].r * L->cs - a->fy;
}

static PzPlace drag_target(const App *a, const Layout *L) {
    int x, y;
    drag_origin(a, L, &x, &y);
    return (PzPlace){a->dorient, floor_div(y - L->ty + L->cs / 2, L->cs), floor_div(x - L->tx + L->cs / 2, L->cs)};
}

/* ------------------------------------------------------------------ */
/* Game actions. */

static void record_if_solved(App *a) {
    if (pz_solved(a->pl, a->m, a->d) && pz_log_add(&a->log, selected(a)) && a->log_path[0])
        if (!pz_log_save(&a->log, a->log_path)) fprintf(stderr, "puzzle: can't save %s\n", a->log_path);
}

static void select_date(App *a, int y, int m, int d) {
    a->y = y, a->m = m, a->d = d;
    a->drag = -1;
    a->solving = false;
    pz_tray(a->pl);
}

static void show_solution(App *a, int i) {
    if (a->nsols <= 0) return;
    int shown = SDL_min(a->nsols, MAX_SOLUTIONS);
    a->sol = (i % shown + shown) % shown;
    memcpy(a->pl, a->sols + (size_t)a->sol * PZ_PIECES, sizeof a->pl);
}

static void start_solver(App *a) {
    if (!unlocked(a)) return;
    a->drag = -1;
    a->nsols = pz_solve(a->m, a->d, a->sols, MAX_SOLUTIONS);
    a->solving = a->nsols > 0;
    show_solution(a, 0);
}

/* Turns a piece in place around the cell under the mouse, or the dragged
   piece around the held cell. */
static void reorient(App *a, bool flip) {
    if (a->drag >= 0) {
        a->dorient = flip ? pz_flip(a->dorient) : pz_rotate(a->dorient);
        return;
    }
    Layout L = layout(a);
    int k = 0, p = piece_at(a, &L, &k);
    if (p < 0) return;
    PzPlace at = a->pl[p];
    PzCell before[PZ_MAX_CELLS], after[PZ_MAX_CELLS];
    pz_piece_cells(p, at.orient, before);
    at.orient = flip ? pz_flip(at.orient) : pz_rotate(at.orient);
    pz_piece_cells(p, at.orient, after);
    at.r += before[k].r - after[k].r;
    at.c += before[k].c - after[k].c;
    if (pz_fits(a->pl, p, at)) {
        a->pl[p] = at;
        record_if_solved(a);
    }
}

static void begin_drag(App *a) {
    Layout L = layout(a);
    int k = 0, p = piece_at(a, &L, &k);
    if (p < 0) return;
    PzCell cells[PZ_MAX_CELLS];
    pz_piece_cells(p, a->pl[p].orient, cells);
    a->drag = p;
    a->grab = k;
    a->dorient = a->pl[p].orient;
    a->fx = a->mx - (L.tx + (a->pl[p].c + cells[k].c) * L.cs);
    a->fy = a->my - (L.ty + (a->pl[p].r + cells[k].r) * L.cs);
}

static void end_drag(App *a) {
    if (a->drag < 0) return;
    Layout L = layout(a);
    PzPlace at = drag_target(a, &L);
    if (pz_fits(a->pl, a->drag, at)) a->pl[a->drag] = at;
    a->drag = -1;
    record_if_solved(a);
}

/* ------------------------------------------------------------------ */
/* Drawing. */

static void cell_rect(const Layout *L, int r, int c, int *x, int *y) {
    *x = L->tx + c * L->cs;
    *y = L->ty + r * L->cs;
}

static bool has_cell(const PzCell *cells, int n, int r, int c) {
    for (int i = 0; i < n; i++)
        if (cells[i].r == r && cells[i].c == c) return true;
    return false;
}

/* A piece with its bounding box at (x,y): a paper outline, then the face
   inset 1u on its outer edges so neighbouring pieces stay distinct. */
static void draw_piece(SDL_Renderer *r, const Layout *L, int p, int orient, int x, int y, TinyColor face) {
    PzCell cells[PZ_MAX_CELLS];
    int n = pz_piece_cells(p, orient, cells), cs = L->cs, u = L->u;
    tiny_set_color(r, TINY_PAPER);
    for (int i = 0; i < n; i++) tiny_fill_rect(r, x + cells[i].c * cs, y + cells[i].r * cs, cs, cs);
    tiny_set_color(r, face);
    for (int i = 0; i < n; i++) {
        int cr = cells[i].r, cc = cells[i].c;
        int l = has_cell(cells, n, cr, cc - 1) ? 0 : u, t = has_cell(cells, n, cr - 1, cc) ? 0 : u;
        int rr = has_cell(cells, n, cr, cc + 1) ? 0 : u, b = has_cell(cells, n, cr + 1, cc) ? 0 : u;
        tiny_fill_rect(r, x + cc * cs + l, y + cr * cs + t, cs - l - rr, cs - t - b);
    }
}

static void draw_board(App *a, const Layout *L, bool solved) {
    SDL_Renderer *r = a->ren;
    int cs = L->cs, u = L->u;
    PzCell date[2];
    pz_date_cells(a->m, a->d, date);

    /* Frame and drop shadow around the board's outline, then the paper. */
    tiny_set_color(r, TINY_INK);
    for (int br = 0; br < PZ_BOARD; br++)
        for (int bc = 0; bc < PZ_BOARD; bc++) {
            if (!pz_board_cell(br, bc)) continue;
            int x, y;
            cell_rect(L, br, bc, &x, &y);
            tiny_fill_rect(r, x - u, y - u, cs + 3 * u, cs + 3 * u);
        }
    int scale = tiny_text_width("Jan", TINY_BODY * u, TINY_PLAIN) <= cs - 6 * u ? TINY_BODY * u : TINY_CAPTION * u;
    for (int br = 0; br < PZ_BOARD; br++)
        for (int bc = 0; bc < PZ_BOARD; bc++) {
            if (!pz_board_cell(br, bc)) continue;
            int x, y;
            cell_rect(L, br, bc, &x, &y);
            tiny_set_color(r, TINY_PAPER);
            tiny_fill_rect(r, x, y, cs, cs);
            tiny_set_color(r, TINY_SHADE);
            if (pz_board_cell(br - 1, bc)) tiny_fill_rect(r, x, y, cs, u);
            if (pz_board_cell(br, bc - 1)) tiny_fill_rect(r, x, y, u, cs);

            bool is_date = (br == date[0].r && bc == date[0].c) || (br == date[1].r && bc == date[1].c);
            TinyColor label = TINY_DIM;
            if (is_date) {
                /* The cells to leave showing; inverted once solved. */
                int in = 2 * u;
                tiny_frame_round(r, x + in, y + in, cs - 2 * in + u, cs - 2 * in + u, TINY_RADIUS_SMALL, u,
                                 TINY_CORNERS, solved ? TINY_INK : TINY_PAPER, TINY_INK);
                label = solved ? TINY_PAPER : TINY_INK;
            }
            tiny_set_color(r, label);
            tiny_text_centered(&a->font, pz_label(br, bc), x + cs / 2 + u / 2, y + (cs - tiny_text_height(scale)) / 2,
                               scale, TINY_PLAIN);
        }
}

static void draw_tracker(App *a, const Layout *L) {
    SDL_Renderer *r = a->ren;
    int u = L->u, kp = L->kp, s = kp - u;
    int32_t today = pz_date(a->ty, a->tm, a->td);
    int hm = 0, hd = 0;
    bool hovering = a->drag < 0 && day_at(a, L, &hm, &hd);
    static const char *const LETTERS = "JFMAMJJASOND";
    for (int m = 1; m <= 12; m++) {
        int y = L->ky + (m - 1) * kp;
        char letter[2] = {LETTERS[m - 1], 0};
        tiny_set_color(r, TINY_DIM);
        tiny_text(&a->font, letter, L->kx - 8 * u, y + (s - tiny_text_height(u)) / 2, u, TINY_PLAIN);
        for (int d = 1; d <= pz_days_in_month(a->y, m); d++) {
            int x = L->kx + (d - 1) * kp;
            int32_t date = pz_date(a->y, m, d);
            if (date == selected(a)) {
                tiny_set_color(r, TINY_INK);
                tiny_fill_rect(r, x - u, y - u, s + 2 * u, s + 2 * u);
                tiny_set_color(r, TINY_PAPER);
                tiny_fill_rect(r, x, y, s, s);
            }
            bool done = pz_log_has(&a->log, date);
            TinyColor frame = date == today ? TINY_INK : date > today ? TINY_SHADE : TINY_GRAY;
            tiny_set_color(r, done ? TINY_INK : frame);
            int in = date == selected(a) ? u : 0;
            tiny_fill_rect(r, x + in, y + in, s - 2 * in, s - 2 * in);
            if (!done) {
                bool hot = hovering && hm == m && hd == d;
                tiny_set_color(r, hot ? TINY_SHADE : TINY_PAPER);
                tiny_fill_rect(r, x + in + u, y + in + u, s - 2 * in - 2 * u, s - 2 * in - 2 * u);
            }
        }
    }

    /* Info column. */
    int x = L->ix, y = L->ky, body = TINY_BODY * u, cap = TINY_CAPTION * u;
    char line[64];
    int n = pz_log_count_year(&a->log, a->y);
    snprintf(line, sizeof line, "%d day%s solved in %d", n, n == 1 ? "" : "s", a->y);
    tiny_set_color(r, TINY_INK);
    tiny_text(&a->font, line, x, y, body, TINY_BOLD);
    y += tiny_line_height(body) + TINY_GAP * u;
    static const char *const HINTS[] = {
        "Drag pieces onto the board",
        "Space rotates · F or right-click flips",
        "Esc puts them back · T for today",
        "Click a day to play that date",
    };
    tiny_set_color(r, TINY_DIM);
    for (size_t i = 0; i < sizeof HINTS / sizeof *HINTS; i++, y += tiny_line_height(cap))
        tiny_text(&a->font, HINTS[i], x, y, cap, TINY_PLAIN);
    if (!unlocked(a)) {
        y += tiny_line_height(cap);
        tiny_text(&a->font, "Solve this date to unlock the solver", x, y, cap, TINY_PLAIN);
    } else if (a->solving && a->nsols > MAX_SOLUTIONS) {
        y += tiny_line_height(cap);
        snprintf(line, sizeof line, "Showing the first %d", MAX_SOLUTIONS);
        tiny_text(&a->font, line, x, y, cap, TINY_PLAIN);
    }
}

static void draw_header(App *a, const Layout *L, bool solved) {
    SDL_Renderer *r = a->ren;
    int u = L->u, x = L->tx - L->pad;

    /* Buttons; Solve is drawn disabled until the date has been solved once. */
    int left = L->tx + PZ_TABLE_W * L->cs + L->pad;
    for (int b = 1; b < BTN_COUNT; b++) {
        Rect br = L->btn[b];
        if (!br.w) continue;
        if (br.x < left) left = br.x;
        TinyColor label;
        if (b == BTN_SOLVE && !unlocked(a)) {
            tiny_frame_round(r, br.x, br.y, br.w, br.h, TINY_RADIUS, u, TINY_CORNERS, TINY_PAPER, TINY_GRAY);
            label = TINY_GRAY;
        } else {
            bool hot = a->drag < 0 && in_rect(br, a->mx, a->my);
            TinyState st = hot && a->pressed == b ? TINY_PRESSED : hot ? TINY_HOVER : TINY_NORMAL;
            label = tiny_button(r, br.x, br.y, br.w, br.h, u, st);
        }
        tiny_set_color(r, label);
        tiny_text_centered(&a->font, button_label(b), br.x + br.w / 2, br.y + (br.h - tiny_text_height(TINY_BODY * u)) / 2,
                           TINY_BODY * u, TINY_BOLD);
    }

    /* Solver position, left of its buttons. */
    char text[64];
    int body = TINY_BODY * u;
    if (a->solving) {
        snprintf(text, sizeof text, "%d of %d", a->sol + 1, a->nsols);
        int tw = tiny_text_width(text, body, TINY_PLAIN);
        left -= TINY_PAD * u + tw;
        tiny_set_color(r, TINY_DIM);
        tiny_text(&a->font, text, left, L->hy + (L->hh - tiny_text_height(body)) / 2, body, TINY_PLAIN);
    }

    /* The date, as large as fits, then a "Solved" chip. */
    snprintf(text, sizeof text, "%s, %s %d", pz_weekday_name(pz_weekday(a->y, a->m, a->d)), pz_month_name(a->m), a->d);
    const char *chip = solved ? "Solved!" : unlocked(a) ? "Solved before" : NULL;
    int chip_w = chip ? tiny_text_width(chip, body, TINY_BOLD) + 2 * TINY_PAD * u + TINY_PAD * u : 0;
    int scale = TINY_TITLE * u;
    if (x + tiny_text_width(text, scale, TINY_BOLD) + chip_w > left - TINY_PAD * u) scale = body;
    tiny_set_color(r, TINY_INK);
    x += tiny_text(&a->font, text, x, L->hy + (L->hh - tiny_text_height(scale)) / 2, scale, TINY_BOLD);
    if (chip) {
        x += TINY_PAD * u;
        int cw = chip_w - TINY_PAD * u, ch = 20 * u, cy = L->hy + (L->hh - ch) / 2;
        if (solved) {
            tiny_set_color(r, TINY_INK);
            tiny_fill_round(r, x, cy, cw, ch, TINY_RADIUS_SMALL, u, TINY_CORNERS);
            tiny_set_color(r, TINY_PAPER);
        } else {
            tiny_frame_round(r, x, cy, cw, ch, TINY_RADIUS_SMALL, u, TINY_CORNERS, TINY_PAPER, TINY_GRAY);
            tiny_set_color(r, TINY_DIM);
        }
        tiny_text_centered(&a->font, chip, x + cw / 2, cy + (ch - tiny_text_height(body)) / 2, body, TINY_BOLD);
    }
}

static void render(App *a) {
    SDL_Renderer *r = a->ren;
    Layout L = layout(a);
    int u = L.u, cs = L.cs;
    bool solved = a->drag < 0 && pz_solved(a->pl, a->m, a->d);

    tiny_set_color(r, TINY_PAPER);
    SDL_RenderClear(r);
    draw_header(a, &L, solved);

    /* The desktop the pieces lie on. */
    int dx = L.tx - L.pad, dy = L.ty - L.pad, dw = PZ_TABLE_W * cs + 2 * L.pad, dh = PZ_TABLE_H * cs + 2 * L.pad;
    tiny_set_color(r, TINY_INK);
    tiny_fill_rect(r, dx - u, dy - u, dw + 2 * u, dh + 2 * u);
    tiny_fill_gray50(r, dx, dy, dw, dh, u);
    draw_board(a, &L, solved);

    /* Where the dragged piece would land. */
    PzPlace target = {0};
    bool lands = false;
    if (a->drag >= 0) {
        target = drag_target(a, &L);
        lands = pz_fits(a->pl, a->drag, target);
    }
    if (lands) {
        PzCell cells[PZ_MAX_CELLS];
        int n = pz_piece_cells(a->drag, target.orient, cells);
        for (int i = 0; i < n; i++) {
            int x, y;
            cell_rect(&L, target.r + cells[i].r, target.c + cells[i].c, &x, &y);
            tiny_set_color(r, TINY_PAPER);
            tiny_fill_rect(r, x, y, cs, cs);
            tiny_fill_gray50(r, x + u, y + u, cs - 2 * u, cs - 2 * u, u);
        }
    }

    int hover = a->drag < 0 ? piece_at(a, &L, NULL) : -1;
    for (int p = 0; p < PZ_PIECES; p++) {
        if (p == a->drag) continue;
        int x, y;
        cell_rect(&L, a->pl[p].r, a->pl[p].c, &x, &y);
        draw_piece(r, &L, p, a->pl[p].orient, x, y, p == hover ? TINY_DIM : TINY_INK);
    }
    if (a->drag >= 0) {
        int x, y;
        drag_origin(a, &L, &x, &y);
        PzCell cells[PZ_MAX_CELLS];
        int n = pz_piece_cells(a->drag, a->dorient, cells);
        tiny_set_color(r, TINY_INK); /* lifted: a drop shadow */
        for (int i = 0; i < n; i++) tiny_fill_rect(r, x + cells[i].c * cs + 2 * u, y + cells[i].r * cs + 2 * u, cs, cs);
        draw_piece(r, &L, a->drag, a->dorient, x, y, TINY_INK);
    }

    draw_tracker(a, &L);
    tiny_present(r);
}

/* ------------------------------------------------------------------ */
/* Events. */

static void update_cursor(App *a) {
    Layout L = layout(a);
    int m, d;
    bool clickable = piece_at(a, &L, NULL) >= 0 || day_at(a, &L, &m, &d);
    for (int b = 1; b < BTN_COUNT; b++)
        if (L.btn[b].w && in_rect(L.btn[b], a->mx, a->my) && (b != BTN_SOLVE || unlocked(a))) clickable = true;
    SDL_SetCursor(a->drag >= 0 ? a->move : clickable ? a->hand : a->arrow);
}

static void press_button(App *a, int b) {
    switch (b) {
    case BTN_TODAY: select_date(a, a->ty, a->tm, a->td); break;
    case BTN_SOLVE: start_solver(a); break;
    case BTN_PREV: show_solution(a, a->sol - 1); break;
    case BTN_NEXT: show_solution(a, a->sol + 1); break;
    case BTN_DONE: a->solving = false; break;
    }
}

static void mouse_down(App *a, int button) {
    Layout L = layout(a);
    if (button == SDL_BUTTON_RIGHT) {
        reorient(a, true);
        return;
    }
    if (button != SDL_BUTTON_LEFT || a->drag >= 0) return;
    for (int b = 1; b < BTN_COUNT; b++)
        if (L.btn[b].w && in_rect(L.btn[b], a->mx, a->my)) {
            a->pressed = b;
            return;
        }
    int m, d;
    if (day_at(a, &L, &m, &d)) {
        if (pz_date(a->y, m, d) != selected(a)) select_date(a, a->y, m, d);
        return;
    }
    begin_drag(a);
}

static void mouse_up(App *a, int button) {
    if (button != SDL_BUTTON_LEFT) return;
    if (a->pressed) {
        Layout L = layout(a);
        int b = a->pressed;
        a->pressed = BTN_NONE;
        if (in_rect(L.btn[b], a->mx, a->my)) press_button(a, b);
        return;
    }
    end_drag(a);
}

static void key_down(App *a, const SDL_KeyboardEvent *k) {
    if (k->repeat && k->key != SDLK_LEFT && k->key != SDLK_RIGHT) return;
    switch (k->key) {
    case SDLK_SPACE: case SDLK_R: reorient(a, false); break;
    case SDLK_F: reorient(a, true); break;
    case SDLK_T: if (selected(a) != pz_date(a->ty, a->tm, a->td)) select_date(a, a->ty, a->tm, a->td); break;
    case SDLK_LEFT: if (a->solving) show_solution(a, a->sol - 1); break;
    case SDLK_RIGHT: if (a->solving) show_solution(a, a->sol + 1); break;
    case SDLK_ESCAPE:
        if (a->drag >= 0) a->drag = -1;
        else if (a->solving) a->solving = false;
        else pz_tray(a->pl);
        break;
    }
}

/* Returns false to quit. */
static bool handle_event(App *a, SDL_Event *e) {
    float dens = tiny_pixel_density(a->win, a->ren);
    switch (e->type) {
    case SDL_EVENT_QUIT:
        return false;
    case SDL_EVENT_KEY_DOWN:
        if (e->key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
            if (e->key.key == SDLK_Q || e->key.key == SDLK_W) return false;
            break;
        }
        key_down(a, &e->key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        a->mx = (int)(e->motion.x * dens), a->my = (int)(e->motion.y * dens);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        a->mx = (int)(e->button.x * dens), a->my = (int)(e->button.y * dens);
        mouse_down(a, e->button.button);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        a->mx = (int)(e->button.x * dens), a->my = (int)(e->button.y * dens);
        mouse_up(a, e->button.button);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (a->drag >= 0) end_drag(a);
        a->pressed = BTN_NONE;
        break;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Today and midnight. */

static void read_today(App *a) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    a->ty = t->tm_year + 1900, a->tm = t->tm_mon + 1, a->td = t->tm_mday;
}

/* Milliseconds until local midnight, when "today" moves on. */
static int ms_to_midnight(void) {
    time_t now = time(NULL);
    struct tm t = *localtime(&now);
    t.tm_hour = 24, t.tm_min = 0, t.tm_sec = 1;
    double s = difftime(mktime(&t), now);
    return s < 1 ? 1000 : s > 3600 ? 3600 * 1000 : (int)(s * 1000);
}

int main(int argc, char **argv) {
    static App app;
    App *a = &app;
    a->drag = -1;
    read_today(a);
    a->y = a->ty, a->m = a->tm, a->d = a->td;
    for (int i = 1; i < argc; i++) { /* --date YYYY-MM-DD plays another day */
        int y, m, d;
        if (!strcmp(argv[i], "--date") && i + 1 < argc && sscanf(argv[++i], "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 &&
            m <= 12 && d >= 1 && d <= pz_days_in_month(y, m))
            a->y = y, a->m = m, a->d = d;
        else {
            fprintf(stderr, "usage: puzzle [--date YYYY-MM-DD]\n");
            return 2;
        }
    }
    pz_tray(a->pl);
    a->sols = malloc(sizeof(PzPlace) * PZ_PIECES * MAX_SOLUTIONS);
    if (!a->sols) return 1;

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    char *dir = SDL_GetPrefPath("tinyapps", "puzzle");
    if (dir) {
        snprintf(a->log_path, sizeof a->log_path, "%ssolved.txt", dir);
        SDL_free(dir);
        if (!pz_log_load(&a->log, a->log_path)) fprintf(stderr, "puzzle: can't read %s\n", a->log_path);
    }
    if (!tiny_create_window("Whole Year Puzzle", 800, 660, 480, 400, &a->win, &a->ren) ||
        !tiny_font_init(&a->font, a->ren)) {
        SDL_Quit();
        return 1;
    }
    a->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    a->hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    a->move = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);

    /* Event-driven; the only timed wake-up is midnight, to move "today". */
    bool running = true;
    render(a);
    while (running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, ms_to_midnight())) {
            running = handle_event(a, &e);
            while (running && SDL_PollEvent(&e)) running = handle_event(a, &e);
        }
        read_today(a);
        if (running) {
            update_cursor(a);
            render(a);
        }
    }

    SDL_DestroyCursor(a->arrow);
    SDL_DestroyCursor(a->hand);
    SDL_DestroyCursor(a->move);
    free(a->sols);
    tiny_font_free(&a->font);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;
}
