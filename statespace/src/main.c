/*
 * State Space - draws a game's whole state graph: one node per position, one
 * edge per move, layered by how many moves have been played. Each node is
 * marked with its value under perfect play. Play a game on the side board
 * and its path lights up in the graph.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "games.h"
#include "puzzle.h"
#include "sv.h"
#include "tiny.h"
#include "views.h"

#define MAX_NODES 2000000
#define MAX_FAINT_EDGES 20000

typedef struct {
    const char *id, *args; /* args: for the usage line */
    const SvGame *game;
    const SvView *view;
    bool (*configure)(const char *arg); /* optional: the argument after the id, or NULL */
} Entry;

/* "MM-DD" or "YYYY-MM-DD"; today without one. */
static bool puzzle_date(const char *arg) {
    int y, m, d;
    if (!arg) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        return sv_puzzle_set_date(t->tm_mon + 1, t->tm_mday);
    }
    if (sscanf(arg, "%d-%d-%d", &y, &m, &d) == 3) return sv_puzzle_set_date(m, d);
    return sscanf(arg, "%d-%d", &m, &d) == 2 && sv_puzzle_set_date(m, d);
}

static const Entry GAMES[] = {
    {"ttt", "", &sv_ttt, &sv_ttt_view, NULL},
    {"puzzle", " [MM-DD]", &sv_puzzle, &sv_puzzle_view, puzzle_date},
    {"puzzle-lowest", " [MM-DD]", &sv_puzzle_lowest, &sv_puzzle_lowest_view, puzzle_date},
};

typedef struct {
    int x, y, w, h;
} Rect;

enum { BTN_NONE, BTN_FOLD, BTN_GROUP, BTN_FIT, BTN_BACK, BTN_START, BTN_COUNT };

typedef struct {
    int u;
    Rect canvas, graph, panel, board, btn[BTN_COUNT]; /* graph: the canvas minus label gutters */
    int head_y, head_h, foot_y;
    int text_y; /* panel text below the buttons */
    int list_y; /* then the moves played, or the moves through the focus */
    int row_h, thumb_max;
} Layout;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TinyFont font;
    SDL_Cursor *arrow, *hand, *grab;

    const Entry *ent;
    SvGraph g;
    bool complete; /* every state explored */
    bool fold, group;
    int maxw;      /* widest layer */
    SvPath path;
    int redo;      /* moves on the path that can be stepped forward into */

    /* Moves from the path's end, with the value each leads to. */
    int nmoves;
    SvMove moves[SV_MAX_MOVES];
    int8_t move_value[SV_MAX_MOVES];
    /* A point on the side board that several moves pass through, and those
       moves (indices into moves[]), best first. */
    bool focused;
    int fx, fy;
    int ncand;
    int cand[SV_MAX_MOVES];

    double zoom; /* pixels per slot of the widest layer */
    double panx; /* slot at the canvas's center, from -maxw/2 to maxw/2 */
    int scrolly;
    bool need_fit; /* fit the view on the next frame */
    int row_h;     /* replay only: a fixed layer height, 0 to fit them all */
    bool at_fit;   /* untouched since the last fit, so a resize refits */

    int mx, my;
    int pressed;            /* button held down */
    bool down, panning;     /* mouse held in the canvas */
    int down_x, down_y;
    double down_panx;
    int down_scrolly;
} App;

static bool in_rect(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static int round_u(int px, int u) { return px / u * u; }

/* 12345 -> "12,345" */
static const char *commas(long long n, char *buf) {
    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%lld", n), o = 0;
    for (int i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = tmp[i];
    }
    buf[o] = 0;
    return buf;
}

static const char *value_name(const SvGame *gm, int v) {
    static char buf[32];
    if (v == SV_UNKNOWN) return "Unknown";
    if (gm->players == 1) return v == 0 ? "Solvable" : "Dead end";
    if (v == SV_NOBODY) return "Draw";
    snprintf(buf, sizeof buf, "%s wins", gm->player_names[v]);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Graph. */

static void path_changed(App *a);

static void build(App *a) {
    sv_free(&a->g);
    if (!sv_init(&a->g, a->ent->game, a->fold)) {
        fprintf(stderr, "statespace: out of memory\n");
        exit(1);
    }
    a->complete = a->ent->game->goals ? sv_explore_back(&a->g, MAX_NODES) : sv_explore(&a->g, MAX_NODES);
    sv_solve(&a->g);
    sv_order(&a->g, a->group);
    a->maxw = 1;
    for (int d = 0; d < a->g.nlayers; d++) a->maxw = SDL_max(a->maxw, sv_layer_len(&a->g, d));
    a->need_fit = true;
}

static void rebuild(App *a) {
    build(a);
    sv_path_refresh(&a->g, &a->path);
    a->redo = a->path.n;
    path_changed(a);
}

/* ------------------------------------------------------------------ */
/* Layout. */

static Layout layout(const App *a) {
    Layout L = {0};
    int w, h;
    SDL_GetRenderOutputSize(a->ren, &w, &h);
    int u = L.u = tiny_unit(a->win, a->ren);
    int M = TINY_MARGIN * u, P = TINY_PAD * u, G = TINY_GAP * u;
    L.head_y = M, L.head_h = 22 * u;
    L.foot_y = h - M - 8 * u;
    int top = L.head_y + L.head_h + P, bottom = L.foot_y - P;

    int pw = 176 * u;
    L.panel = (Rect){w - M - pw, top, pw, bottom - top};
    L.canvas = (Rect){M, top, L.panel.x - P - M, bottom - top};
    L.graph = (Rect){L.canvas.x + 14 * u, L.canvas.y, L.canvas.w - 14 * u - 46 * u, L.canvas.h};
    int bs = pw - 2 * P;
    L.board = (Rect){L.panel.x + P, top + P + 14 * u + P, bs, bs};
    int bw = (bs - G) / 2, by = L.board.y + bs + P;
    L.btn[BTN_BACK] = (Rect){L.board.x, by, bw, 20 * u};
    L.btn[BTN_START] = (Rect){L.board.x + bw + G, by, bs - bw - G, 20 * u};
    L.text_y = by + 20 * u + P + 2 * u;
    L.list_y = L.text_y + 3 * tiny_line_height(u) + tiny_line_height(TINY_BODY * u) + P;

    /* Header buttons, right to left. */
    const int order[] = {BTN_FIT, BTN_GROUP, BTN_FOLD};
    static const char *const labels[] = {"Fit", "Group", "Fold"};
    int x = w - M;
    for (int i = 0; i < 3; i++) {
        if (order[i] == BTN_FOLD && !a->ent->game->canonical) continue;
        int bw2 = SDL_max(tiny_text_width(labels[i], TINY_BODY * u, TINY_BOLD) + 2 * P, 24 * u);
        x -= bw2;
        L.btn[order[i]] = (Rect){x, L.head_y + 1 * u, bw2, 20 * u};
        x -= G;
    }

    int nl = SDL_max(a->g.nlayers, 1);
    L.row_h = a->row_h ? a->row_h : SDL_max(L.canvas.h / nl, 28 * u);
    L.thumb_max = SDL_clamp(round_u(L.row_h - 14 * u, u), 9 * u, 48 * u);
    return L;
}

static double fit_zoom(const App *a, const Layout *L) { return (double)L->graph.w / a->maxw; }
static double max_zoom(const App *a, const Layout *L) {
    return SDL_max(fit_zoom(a, L), (double)(L->thumb_max + 8 * L->u));
}

static void clamp_view(App *a, const Layout *L) {
    a->zoom = SDL_clamp(a->zoom, fit_zoom(a, L), max_zoom(a, L));
    double half = a->maxw / 2.0 - (L->graph.w / 2.0) / a->zoom;
    if (half < 0) half = 0;
    a->panx = SDL_clamp(a->panx, -half, half);
    int over = SDL_max(a->g.nlayers * L->row_h - L->canvas.h, 0);
    a->scrolly = SDL_clamp(a->scrolly, 0, over);
}

/* Where a node is drawn: its box, and whether as a thumbnail. */
typedef struct {
    int cx, cy;  /* center */
    int size;    /* thumbnail or dot size */
    bool thumb;
    Rect box;    /* everything drawn for it, value strip included */
} Spot;

static int layer_y(const App *a, const Layout *L, int d) { return L->canvas.y + d * L->row_h + L->row_h / 2 - a->scrolly; }

static Spot spot(const App *a, const Layout *L, int n) {
    int u = L->u, d = a->g.nodes[n].depth;
    double pitch = a->maxw * a->zoom / sv_layer_len(&a->g, d); /* pixels between neighbors */
    double slot = (sv_layer_x(&a->g, n) - 0.5) * a->maxw;
    Spot s = {.cx = L->graph.x + L->graph.w / 2 + (int)lround((slot - a->panx) * a->zoom), .cy = layer_y(a, L, d)};
    int t = SDL_min(L->thumb_max, round_u((int)(pitch - 4 * u), u));
    if (t >= 9 * u) {
        s.thumb = true, s.size = t;
        s.box = (Rect){s.cx - t / 2, s.cy - (t + 4 * u) / 2, t, t + 4 * u};
    } else {
        s.size = SDL_clamp(round_u((int)(pitch * 0.7), u), 2 * u, 8 * u);
        s.box = (Rect){s.cx - s.size / 2, s.cy - s.size / 2, s.size, s.size};
    }
    return s;
}

/* The node under a pixel, or -1. */
static int node_at(const App *a, const Layout *L, int x, int y) {
    if (!in_rect(L->canvas, x, y) || !a->g.nlayers) return -1;
    int d = (y - L->canvas.y + a->scrolly) / L->row_h;
    if (d < 0 || d >= a->g.nlayers) return -1;
    int len = sv_layer_len(&a->g, d);
    double slot = a->panx + (double)(x - L->canvas.x - L->canvas.w / 2) / a->zoom;
    int pos = (int)floor((slot / a->maxw + 0.5) * len);
    int best = -1, bestd = 0;
    for (int p = SDL_max(pos - 1, 0); p <= SDL_min(pos + 1, len - 1); p++) {
        int n = a->g.layer[a->g.layer_start[d] + p];
        Spot s = spot(a, L, n);
        int pad = 2 * L->u;
        if (x < s.box.x - pad || x >= s.box.x + s.box.w + pad || y < s.box.y - pad || y >= s.box.y + s.box.h + pad)
            continue;
        int dist = abs(x - s.cx);
        if (best < 0 || dist < bestd) best = n, bestd = dist;
    }
    return best;
}

static void fit(App *a, const Layout *L) {
    a->zoom = fit_zoom(a, L), a->panx = 0, a->scrolly = 0;
    a->need_fit = false, a->at_fit = true;
    clamp_view(a, L);
}

/* Zooms about a canvas pixel x. */
static void zoom_by(App *a, const Layout *L, double f, int x) {
    double off = x - (L->graph.x + L->graph.w / 2), slot = a->panx + off / a->zoom;
    a->at_fit = false;
    a->zoom *= f;
    clamp_view(a, L);
    a->panx = slot - off / a->zoom;
    clamp_view(a, L);
}

/* Pans so the path's end is on screen. */
static void reveal_end(App *a) {
    if (a->need_fit) return; /* not drawn yet */
    Layout L = layout(a);
    int n = a->path.node[a->path.n];
    if (n < 0) return;
    Spot s = spot(a, &L, n);
    if (s.cx < L.canvas.x + L.canvas.w / 8 || s.cx > L.canvas.x + L.canvas.w * 7 / 8)
        a->panx += (double)(s.cx - (L.graph.x + L.graph.w / 2)) / a->zoom;
    int d = a->g.nodes[n].depth, y = d * L.row_h;
    if (y < a->scrolly) a->scrolly = y;
    if (y + L.row_h > a->scrolly + L.canvas.h) a->scrolly = y + L.row_h - L.canvas.h;
    clamp_view(a, &L);
}

/* ------------------------------------------------------------------ */
/* The path. */

static void path_changed(App *a) {
    SvGraph *g = &a->g;
    int end = a->path.node[a->path.n];
    if (end >= 0 && !g->nodes[end].expanded) { /* off the explored graph: grow it */
        sv_expand(g, end);
        sv_solve(g);
        sv_order(g, a->group);
    }
    const void *s = sv_path_end(&a->path);
    a->nmoves = g->game->outcome(s) == SV_PLAYING ? g->game->moves(s, a->moves) : 0;
    for (int i = 0; i < a->nmoves; i++) {
        uint8_t t[SV_MAX_STATE] = {0};
        g->game->apply(s, a->moves[i], t);
        a->move_value[i] = (int8_t)sv_state_value(g, t);
    }
    a->focused = false;
}

static void play(App *a, SvMove m) {
    bool same = a->path.n < a->redo && a->path.move[a->path.n] == m;
    if (!sv_path_push(&a->g, &a->path, m)) return;
    if (!same) a->redo = a->path.n;
    path_changed(a);
    reveal_end(a);
}

static void back(App *a) {
    if (sv_path_pop(&a->path)) path_changed(a), reveal_end(a);
}

static void forward(App *a) {
    if (a->path.n < a->redo) play(a, a->path.move[a->path.n]);
}

static void restart(App *a) {
    a->path.n = 0;
    path_changed(a);
    reveal_end(a);
}

static void jump(App *a, int n) {
    if (!sv_path_to(&a->g, &a->path, n)) return;
    a->redo = a->path.n;
    path_changed(a);
}

/* The moves through a pixel of the side board; returns the count. */
static int hits(const App *a, const Layout *L, int x, int y, int *out) {
    if (!in_rect(L->board, x, y)) return 0;
    int n = 0;
    for (int i = 0; i < a->nmoves; i++)
        if (a->ent->view->move_hit(sv_path_end(&a->path), a->moves[i], L->board.w, x - L->board.x, y - L->board.y))
            out[n++] = i;
    return n;
}

static const App *sort_app;
static int cmp_cand(const void *x, const void *y) {
    const App *a = sort_app;
    int i = *(const int *)x, j = *(const int *)y, p = a->g.game->to_move(sv_path_end(&a->path));
    int vi = sv_value_for(a->move_value[i], p), vj = sv_value_for(a->move_value[j], p);
    return vi != vj ? vj - vi : i - j;
}

static void set_focus(App *a, const Layout *L, int x, int y) {
    a->ncand = hits(a, L, x, y, a->cand);
    a->focused = true, a->fx = x - L->board.x, a->fy = y - L->board.y;
    sort_app = a;
    qsort(a->cand, a->ncand, sizeof *a->cand, cmp_cand);
}

/* The grid of candidate moves under the side board: 4 across. */
static int cand_size(const Layout *L) { return round_u((L->board.w - 3 * TINY_GAP * L->u) / 4, L->u); }
static int cand_slots(const Layout *L) {
    int t = cand_size(L), top = L->list_y + tiny_line_height(L->u);
    return SDL_max((L->panel.y + L->panel.h - TINY_PAD * L->u - top + TINY_GAP * L->u) / (t + 4 * L->u + TINY_GAP * L->u), 0) * 4;
}
static Rect cand_rect(const Layout *L, int i) {
    int t = cand_size(L), G = TINY_GAP * L->u;
    return (Rect){L->board.x + i % 4 * (t + G), L->list_y + tiny_line_height(L->u) + i / 4 * (t + 4 * L->u + G), t,
                  t + 4 * L->u};
}
static int cand_at(const App *a, const Layout *L, int x, int y) {
    if (!a->focused) return -1;
    for (int i = 0; i < a->ncand && i < cand_slots(L); i++)
        if (in_rect(cand_rect(L, i), x, y)) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Drawing. */

static void thick_line(SDL_Renderer *r, int x1, int y1, int x2, int y2, int width) {
    for (int k = 0; k < width; k++) SDL_RenderLine(r, (float)(x1 + k), (float)y1, (float)(x2 + k), (float)y2);
}

static void draw_edge(App *a, const Layout *L, int from, int to, int width) {
    Spot s = spot(a, L, from), t = spot(a, L, to);
    int lo = L->canvas.x, hi = L->canvas.x + L->canvas.w;
    if ((s.cx < lo && t.cx < lo) || (s.cx > hi && t.cx > hi)) return;
    thick_line(a->ren, s.cx - width / 2, s.box.y + s.box.h, t.cx - width / 2, t.box.y, width);
}

static void draw_node(App *a, const Layout *L, int n) {
    SDL_Renderer *r = a->ren;
    int u = L->u;
    Spot s = spot(a, L, n);
    if (s.box.x + s.box.w < L->canvas.x || s.box.x > L->canvas.x + L->canvas.w) return;
    int v = a->g.nodes[n].value;
    if (s.thumb) {
        a->ent->view->draw(r, &a->font, sv_state(&a->g, n), s.box.x, s.box.y, s.size, u);
        sv_value_fill(r, s.box.x, s.box.y + s.size + u, s.size, 3 * u, v, u);
    } else {
        sv_value_fill(r, s.box.x, s.box.y, s.size, s.size, v, u);
    }
}

/* A ring around a box: single for the path, double for its end. */
static void ring_box(App *a, const Layout *L, Rect b, bool end) {
    SDL_Renderer *r = a->ren;
    int u = L->u;
    int o = 2 * u;
    tiny_set_color(r, TINY_INK);
    for (int k = 0; k < (end ? 2 : 1); k++, o += 2 * u) {
        int x = b.x - o, y = b.y - o, w = b.w + 2 * o, h = b.h + 2 * o;
        tiny_fill_rect(r, x, y, w, u);
        tiny_fill_rect(r, x, y + h - u, w, u);
        tiny_fill_rect(r, x, y, u, h);
        tiny_fill_rect(r, x + w - u, y, u, h);
    }
}

static void ring(App *a, const Layout *L, int n, bool end) { ring_box(a, L, spot(a, L, n).box, end); }

/* Where the path's i-th state is drawn when it's off a closed graph (a dead end): a
   thumbnail in its layer, straight below the last state on the graph. */
static Rect dead_box(const App *a, const Layout *L, int i, int last) {
    int t = L->thumb_max, u = L->u;
    return (Rect){spot(a, L, last).cx - t / 2, layer_y(a, L, i) - (t + 4 * u) / 2, t, t + 4 * u};
}

/* The path past the graph's edge, in gray: each dead-end state's board and its value
   strip, joined by the path's line. */
static void draw_dead_end(App *a, const Layout *L) {
    SDL_Renderer *r = a->ren;
    int u = L->u, last = -1, k = 0;
    while (k <= a->path.n && a->path.node[k] >= 0) last = a->path.node[k++];
    if (k > a->path.n || last < 0) return;
    Rect prev = spot(a, L, last).box;
    for (int i = k; i <= a->path.n; i++) {
        Rect b = dead_box(a, L, i, last);
        tiny_set_color(r, TINY_GRAY);
        thick_line(r, prev.x + prev.w / 2 - u, prev.y + prev.h, b.x + b.w / 2 - u, b.y, 2 * u);
        a->ent->view->draw(r, &a->font, a->path.state[i], b.x, b.y, b.w, u);
        sv_value_fill(r, b.x, b.y + b.w + u, b.w, 3 * u, SV_NOBODY, u);
        ring_box(a, L, b, i == a->path.n);
        prev = b;
    }
}

static void draw_graph(App *a, const Layout *L, int hover) {
    SDL_Renderer *r = a->ren;
    SvGraph *g = &a->g;
    int u = L->u;
    Rect c = L->canvas;
    tiny_frame_round(r, c.x - u, c.y - u, c.w + 2 * u, c.h + 2 * u, 0, u, 0, TINY_PAPER, TINY_INK);
    SDL_Rect clip = {c.x, c.y, c.w, c.h};
    SDL_SetRenderClipRect(r, &clip);

    /* Layer labels: depth on the left, state count on the right. */
    for (int d = 0; d < g->nlayers; d++) {
        char buf[32];
        int y = layer_y(a, L, d) - tiny_text_height(u) / 2;
        tiny_set_color(r, TINY_DIM);
        snprintf(buf, sizeof buf, "%d", d);
        tiny_text(&a->font, buf, c.x + 2 * u, y, u, TINY_PLAIN);
        commas(sv_layer_len(g, d), buf);
        tiny_text(&a->font, buf, c.x + c.w - 2 * u - tiny_text_width(buf, u, TINY_PLAIN), y, u, TINY_PLAIN);
    }

    /* Every edge, faintly (unless there are too many to read); then the
       end's moves and the path in ink. */
    if (g->nedges <= MAX_FAINT_EDGES) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_Color ink = tiny_color(TINY_INK);
        int alpha = g->nedges > 20000 ? 18 : g->nedges > 4000 ? 30 : 48;
        SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, (Uint8)alpha);
        for (int e = 0; e < g->nedges; e++) draw_edge(a, L, g->edges[e].from, g->edges[e].to, u);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
    int end = a->path.node[a->path.n];
    tiny_set_color(r, TINY_GRAY);
    if (end >= 0)
        for (int e = g->nodes[end].first_out; e >= 0; e = g->edges[e].next_out) draw_edge(a, L, end, g->edges[e].to, u);
    if (hover >= 0) {
        tiny_set_color(r, TINY_DIM);
        for (int e = g->nodes[hover].first_out; e >= 0; e = g->edges[e].next_out) draw_edge(a, L, hover, g->edges[e].to, u);
        for (int e = g->nodes[hover].first_in; e >= 0; e = g->edges[e].next_in) draw_edge(a, L, g->edges[e].from, hover, u);
    }
    tiny_set_color(r, TINY_INK);
    for (int i = 0; i < a->path.n; i++)
        if (a->path.node[i] >= 0 && a->path.node[i + 1] >= 0) draw_edge(a, L, a->path.node[i], a->path.node[i + 1], 2 * u);

    /* Dense layers put many dots on one pixel: draw each value once there. */
    for (int d = 0; d < g->nlayers; d++) {
        int last_x = -1, last_v = 0;
        for (int i = g->layer_start[d]; i < g->layer_start[d + 1]; i++) {
            int n = g->layer[i];
            Spot sp = spot(a, L, n);
            if (!sp.thumb && sp.cx == last_x && g->nodes[n].value == last_v) continue;
            last_x = sp.cx, last_v = g->nodes[n].value;
            draw_node(a, L, n);
        }
    }
    if (hover >= 0) {
        Spot s = spot(a, L, hover);
        tiny_set_color(r, TINY_GRAY);
        Rect b = s.box;
        int o = 2 * u;
        tiny_fill_rect(r, b.x - o, b.y - o, b.w + 2 * o, u);
        tiny_fill_rect(r, b.x - o, b.y + b.h + o - u, b.w + 2 * o, u);
        tiny_fill_rect(r, b.x - o, b.y - o, u, b.h + 2 * o);
        tiny_fill_rect(r, b.x + b.w + o - u, b.y - o, u, b.h + 2 * o);
    }
    for (int i = 0; i <= a->path.n; i++)
        if (a->path.node[i] >= 0) ring(a, L, a->path.node[i], i == a->path.n);
    draw_dead_end(a, L);
    SDL_SetRenderClipRect(r, NULL);
}

/* A board picture and value for a hovered dot, next to the mouse. */
static void draw_tooltip(App *a, const Layout *L, int n) {
    SDL_Renderer *r = a->ren;
    int u = L->u, P = TINY_PAD * u, size = 48 * u;
    const char *label = value_name(a->g.game, a->g.nodes[n].value);
    int w = SDL_max(size, tiny_text_width(label, u, TINY_PLAIN)) + 2 * P;
    int h = size + P + tiny_text_height(u) + 2 * P;
    int x = a->mx + 12 * u, y = a->my + 12 * u;
    int ww, wh;
    SDL_GetRenderOutputSize(r, &ww, &wh);
    if (x + w > ww - u) x = a->mx - 12 * u - w;
    if (y + h > wh - u) y = a->my - 12 * u - h;
    tiny_panel(r, x, y, w, h, u);
    a->ent->view->draw(r, &a->font, sv_state(&a->g, n), x + (w - size) / 2, y + P, size, u);
    tiny_set_color(r, TINY_INK);
    tiny_text_centered(&a->font, label, x + w / 2, y + P + size + P, u, TINY_PLAIN);
}

static TinyState button_state(const App *a, const Layout *L, int b, bool on) {
    if (on || a->pressed == b) return TINY_PRESSED;
    return in_rect(L->btn[b], a->mx, a->my) ? TINY_HOVER : TINY_NORMAL;
}

static void draw_button(App *a, const Layout *L, int b, const char *label, bool on, bool enabled) {
    Rect r = L->btn[b];
    if (!r.w) return;
    int u = L->u;
    TinyColor fg;
    if (enabled) {
        fg = tiny_button(a->ren, r.x, r.y, r.w, r.h, u, button_state(a, L, b, on));
    } else {
        tiny_frame_round(a->ren, r.x, r.y, r.w, r.h, TINY_RADIUS, u, TINY_CORNERS, TINY_PAPER, TINY_GRAY);
        fg = TINY_GRAY;
    }
    tiny_set_color(a->ren, fg);
    tiny_text_centered(&a->font, label, r.x + r.w / 2, r.y + (r.h - tiny_text_height(TINY_BODY * u)) / 2,
                       TINY_BODY * u, TINY_BOLD);
}

static void draw_panel(App *a, const Layout *L) {
    SDL_Renderer *r = a->ren;
    const SvGame *gm = a->g.game;
    int u = L->u, P = TINY_PAD * u, body = TINY_BODY * u;
    Rect pn = L->panel;
    tiny_panel(r, pn.x, pn.y, pn.w, pn.h, u);
    const void *s = sv_path_end(&a->path);
    char buf[96];

    /* Heading: how far in, and whose turn or how it ended. */
    int oc = gm->outcome(s), mover = gm->to_move(s);
    if (oc != SV_PLAYING || !a->nmoves) {
        if (gm->players == 1) snprintf(buf, sizeof buf, "%s", oc == 0 ? "Solved" : "Stuck");
        else if (oc == SV_NOBODY) snprintf(buf, sizeof buf, "Draw");
        else snprintf(buf, sizeof buf, "%s won", gm->player_names[oc >= 0 ? oc : 1 - mover]);
    } else if (gm->players == 1) {
        snprintf(buf, sizeof buf, "Your move");
    } else {
        snprintf(buf, sizeof buf, "%s to play", gm->player_names[mover]);
    }
    tiny_set_color(r, TINY_INK);
    tiny_text(&a->font, buf, pn.x + P, pn.y + P, body, TINY_BOLD);

    /* The board with the game's marks; hovering a candidate previews it. */
    Rect b = L->board;
    tiny_frame_round(r, b.x - u, b.y - u, b.w + 2 * u, b.h + 2 * u, 0, u, 0, TINY_PAPER, TINY_INK);
    int hc = cand_at(a, L, a->mx, a->my);
    if (hc >= 0) {
        uint8_t t[SV_MAX_STATE] = {0};
        gm->apply(s, a->moves[a->cand[hc]], t);
        a->ent->view->draw(r, &a->font, t, b.x, b.y, b.w, u);
    } else {
        a->ent->view->draw(r, &a->font, s, b.x, b.y, b.w, u);
        int fx = -1, fy = -1;
        if (a->focused) fx = a->fx, fy = a->fy;
        else if (in_rect(b, a->mx, a->my)) fx = a->mx - b.x, fy = a->my - b.y;
        if (a->ent->view->annotate)
            a->ent->view->annotate(r, &a->font, s, a->moves, a->move_value, a->nmoves, b.x, b.y, b.w, u, fx, fy);
    }

    draw_button(a, L, BTN_BACK, "Back", false, a->path.n > 0);
    draw_button(a, L, BTN_START, "Start", false, a->path.n > 0);

    /* Value under perfect play, and how the moves split. */
    int y = L->text_y, x = pn.x + P;
    int v = sv_state_value(&a->g, s);
    tiny_set_color(r, TINY_DIM);
    tiny_text(&a->font, "Best play", x, y, u, TINY_PLAIN);
    y += tiny_line_height(u);
    tiny_set_color(r, TINY_INK);
    tiny_text(&a->font, value_name(gm, v), x, y, body, TINY_BOLD);
    y += tiny_line_height(body);
    int win = 0, draw = 0, lose = 0, unk = 0;
    for (int i = 0; i < a->nmoves; i++) {
        int sv = sv_value_for(a->move_value[i], mover);
        win += sv == 1, draw += sv == 0, lose += sv == -1, unk += sv == -2;
    }
    if (a->nmoves) {
        tiny_set_color(r, TINY_DIM);
        if (gm->players == 1) snprintf(buf, sizeof buf, "%d of %d moves stay solvable", win, a->nmoves);
        else snprintf(buf, sizeof buf, "%d win · %d draw · %d lose", win, draw, lose);
        tiny_text(&a->font, buf, x, y, u, TINY_PLAIN);
        y += tiny_line_height(u);
        if (unk) {
            snprintf(buf, sizeof buf, "%d not worked out", unk);
            tiny_text(&a->font, buf, x, y, u, TINY_PLAIN);
        }
    }
    y = L->list_y;

    /* The moves through the focused point, best first, as boards to pick. */
    if (a->focused) {
        int slots = cand_slots(L), good = 0;
        for (int i = 0; i < a->ncand; i++) good += sv_value_for(a->move_value[a->cand[i]], mover) == 1;
        if (gm->players == 1) snprintf(buf, sizeof buf, "%d moves here · %d solvable", a->ncand, good);
        else snprintf(buf, sizeof buf, "%d moves here · %d win", a->ncand, good);
        if (a->ncand > slots) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " · first %d", slots);
        tiny_set_color(r, TINY_DIM);
        tiny_text(&a->font, buf, x, y, u, TINY_PLAIN);
        for (int i = 0; i < a->ncand && i < slots; i++) {
            Rect c = cand_rect(L, i);
            uint8_t t[SV_MAX_STATE] = {0};
            gm->apply(s, a->moves[a->cand[i]], t);
            a->ent->view->draw(r, &a->font, t, c.x, c.y, c.w, u);
            sv_value_fill(r, c.x, c.y + c.w + u, c.w, 3 * u, a->move_value[a->cand[i]], u);
            if (i == hc) {
                tiny_set_color(r, TINY_INK);
                tiny_fill_rect(r, c.x - 2 * u, c.y - 2 * u, c.w + 4 * u, u);
                tiny_fill_rect(r, c.x - 2 * u, c.y + c.h + u, c.w + 4 * u, u);
                tiny_fill_rect(r, c.x - 2 * u, c.y - 2 * u, u, c.h + 4 * u);
                tiny_fill_rect(r, c.x + c.w + u, c.y - 2 * u, u, c.h + 4 * u);
            }
        }
        return;
    }

    /* The moves played, newest last; ones that can be redone in gray. */
    int lh = tiny_line_height(body) * 3 / 4, room = (pn.y + pn.h - P - y) / lh;
    int total = a->redo, first = SDL_max(0, SDL_min(a->path.n + 1, total) - room);
    for (int i = first; i < total && i < first + room; i++) {
        char name[48];
        gm->move_name(a->path.state[i], a->path.move[i], name, sizeof name);
        snprintf(buf, sizeof buf, "%d. %s", i + 1, name);
        tiny_set_color(r, i < a->path.n ? TINY_INK : TINY_GRAY);
        tiny_text(&a->font, buf, x, y, body, TINY_PLAIN);
        y += lh;
    }
}

static void draw_frame(App *a) {
    SDL_Renderer *r = a->ren;
    Layout L = layout(a);
    if (a->need_fit) fit(a, &L);
    clamp_view(a, &L);
    int u = L.u, body = TINY_BODY * u;
    char buf[160], n1[32], n2[32];

    int w, h;
    SDL_GetRenderOutputSize(r, &w, &h);
    tiny_set_color(r, TINY_PAPER);
    SDL_RenderClear(r);

    /* Header: the game and the graph's size. */
    tiny_set_color(r, TINY_INK);
    int x = TINY_MARGIN * u, ty = L.head_y + (L.head_h - tiny_text_height(TINY_TITLE * u)) / 2;
    x += tiny_text(&a->font, a->g.game->name, x, ty, TINY_TITLE * u, TINY_BOLD) + TINY_PAD * u;
    snprintf(buf, sizeof buf, "%s %sstates · %s moves%s", commas(a->g.nnodes, n1), a->g.closed ? "solvable " : "",
             commas(a->g.nedges, n2), a->complete ? "" : " · partial");
    int room = L.btn[BTN_FOLD].w ? L.btn[BTN_FOLD].x : L.btn[BTN_GROUP].x;
    int scale = x + tiny_text_width(buf, body, TINY_PLAIN) + TINY_PAD * u <= room ? body : u;
    tiny_set_color(r, TINY_DIM);
    if (x + tiny_text_width(buf, scale, TINY_PLAIN) + TINY_PAD * u <= room)
        tiny_text(&a->font, buf, x, L.head_y + (L.head_h - tiny_text_height(scale)) / 2, scale, TINY_PLAIN);
    draw_button(a, &L, BTN_FOLD, "Fold", a->fold, true);
    draw_button(a, &L, BTN_GROUP, "Group", a->group, true);
    draw_button(a, &L, BTN_FIT, "Fit", false, true);

    int hover = a->panning || a->pressed ? -1 : node_at(a, &L, a->mx, a->my);
    draw_graph(a, &L, hover);
    draw_panel(a, &L);
    if (hover >= 0 && !spot(a, &L, hover).thumb) draw_tooltip(a, &L, hover);

    /* Footer: legend, then hints. */
    const SvGame *gm = a->g.game;
    int fy = L.foot_y, sw = 7 * u;
    x = TINY_MARGIN * u;
    int vals[4], nv = 0;
    vals[nv++] = 0;
    if (gm->players > 1) vals[nv++] = SV_NOBODY, vals[nv++] = 1;
    else vals[nv++] = SV_NOBODY;
    if (!a->complete) vals[nv++] = SV_UNKNOWN;
    for (int i = 0; i < nv; i++) {
        sv_value_fill(r, x, fy, sw, sw, vals[i], u);
        x += sw + TINY_GAP * u;
        tiny_set_color(r, TINY_INK);
        x += tiny_text(&a->font, value_name(gm, vals[i]), x, fy, u, TINY_PLAIN) + TINY_PAD * u * 2;
    }
    tiny_set_color(r, TINY_DIM);
    const char *hint = "Click a state to go there · Backspace back · Scroll to pan · Pinch or ⌘-scroll to zoom";
    tiny_text(&a->font, hint, SDL_max(x, w - TINY_MARGIN * u - tiny_text_width(hint, u, TINY_PLAIN)), fy, u,
              TINY_PLAIN);
}

static void render(App *a) {
    draw_frame(a);
    tiny_present(a->ren);
}

/* ------------------------------------------------------------------ */
/* Input. */

static int button_at(const Layout *L, int x, int y) {
    for (int b = 1; b < BTN_COUNT; b++)
        if (L->btn[b].w && in_rect(L->btn[b], x, y)) return b;
    return BTN_NONE;
}

static void press(App *a, int b) {
    switch (b) {
    case BTN_FOLD: a->fold = !a->fold, rebuild(a); break;
    case BTN_GROUP: a->group = !a->group, sv_order(&a->g, a->group), reveal_end(a); break;
    case BTN_FIT: {
        Layout L = layout(a);
        fit(a, &L);
        break;
    }
    case BTN_BACK: back(a); break;
    case BTN_START: restart(a); break;
    }
}

static void mouse_down(App *a) {
    Layout L = layout(a);
    int b = button_at(&L, a->mx, a->my);
    if (b) {
        a->pressed = b;
        return;
    }
    int c = cand_at(a, &L, a->mx, a->my);
    if (c >= 0) {
        play(a, a->moves[a->cand[c]]);
        return;
    }
    if (in_rect(L.board, a->mx, a->my)) {
        static int h[SV_MAX_MOVES];
        int n = hits(a, &L, a->mx, a->my, h);
        if (n == 1) play(a, a->moves[h[0]]);
        else if (n > 1) set_focus(a, &L, a->mx, a->my);
        else a->focused = false;
        return;
    }
    if (in_rect(L.canvas, a->mx, a->my)) {
        a->down = true, a->panning = false;
        a->down_x = a->mx, a->down_y = a->my, a->down_panx = a->panx, a->down_scrolly = a->scrolly;
    }
}

static void mouse_motion(App *a) {
    if (!a->down) return;
    Layout L = layout(a);
    int dx = a->mx - a->down_x, dy = a->my - a->down_y;
    if (!a->panning && abs(dx) + abs(dy) > 3 * L.u) a->panning = true;
    if (a->panning) {
        a->at_fit = false;
        a->panx = a->down_panx - dx / a->zoom;
        a->scrolly = a->down_scrolly - dy;
        clamp_view(a, &L);
    }
}

static void mouse_up(App *a) {
    Layout L = layout(a);
    if (a->pressed) {
        if (button_at(&L, a->mx, a->my) == a->pressed) press(a, a->pressed);
        a->pressed = BTN_NONE;
    }
    if (a->down && !a->panning) {
        int n = node_at(a, &L, a->mx, a->my);
        if (n >= 0) jump(a, n);
    }
    a->down = a->panning = false;
}

static void wheel(App *a, const SDL_MouseWheelEvent *w) {
    Layout L = layout(a);
    float dx = w->x, dy = w->y;
    if (w->direction == SDL_MOUSEWHEEL_FLIPPED) dx = -dx, dy = -dy;
    if (SDL_GetModState() & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
        zoom_by(a, &L, pow(1.15, dy), a->mx);
        return;
    }
    double step = 24.0 * L.u;
    bool tall = a->g.nlayers * L.row_h > L.canvas.h;
    if (tall) a->scrolly -= (int)(dy * step);
    else dx -= dy; /* a plain mouse wheel pans sideways */
    a->panx += dx * step / a->zoom;
    a->at_fit = false;
    clamp_view(a, &L);
}

static void key_down(App *a, const SDL_KeyboardEvent *k) {
    Layout L = layout(a);
    bool repeatable = k->key == SDLK_LEFT || k->key == SDLK_RIGHT || k->key == SDLK_BACKSPACE ||
                      k->key == SDLK_EQUALS || k->key == SDLK_MINUS;
    if (k->repeat && !repeatable) return;
    switch (k->key) {
    case SDLK_BACKSPACE: case SDLK_LEFT: back(a); break;
    case SDLK_RIGHT: forward(a); break;
    case SDLK_ESCAPE:
        if (a->down) a->panx = a->down_panx, a->scrolly = a->down_scrolly, a->down = a->panning = false;
        else if (a->focused) a->focused = false;
        else restart(a);
        break;
    case SDLK_F: press(a, BTN_FOLD); break;
    case SDLK_G: press(a, BTN_GROUP); break;
    case SDLK_0: fit(a, &L); break;
    case SDLK_EQUALS: zoom_by(a, &L, 1.5, L.graph.x + L.graph.w / 2); break;
    case SDLK_MINUS: zoom_by(a, &L, 1 / 1.5, L.graph.x + L.graph.w / 2); break;
    }
}

/* Whether any move passes through the mouse on the side board. */
static bool board_moves(const App *a, const Layout *L) {
    static int h[SV_MAX_MOVES];
    return hits(a, L, a->mx, a->my, h) > 0;
}

static void update_cursor(App *a) {
    Layout L = layout(a);
    SDL_Cursor *c = a->arrow;
    if (a->panning) c = a->grab;
    else if (button_at(&L, a->mx, a->my) || cand_at(a, &L, a->mx, a->my) >= 0 || node_at(a, &L, a->mx, a->my) >= 0 ||
             board_moves(a, &L))
        c = a->hand;
    SDL_SetCursor(c);
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
            if (e->key.key == SDLK_Z) back(a);
            break;
        }
        key_down(a, &e->key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        a->mx = (int)(e->motion.x * dens), a->my = (int)(e->motion.y * dens);
        mouse_motion(a);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        a->mx = (int)(e->button.x * dens), a->my = (int)(e->button.y * dens);
        if (e->button.button == SDL_BUTTON_LEFT) mouse_down(a);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        a->mx = (int)(e->button.x * dens), a->my = (int)(e->button.y * dens);
        if (e->button.button == SDL_BUTTON_LEFT) mouse_up(a);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        a->mx = (int)(e->wheel.mouse_x * dens), a->my = (int)(e->wheel.mouse_y * dens);
        wheel(a, &e->wheel);
        break;
    case SDL_EVENT_PINCH_UPDATE: {
        Layout L = layout(a);
        zoom_by(a, &L, e->pinch.scale, a->mx);
        break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (a->at_fit) a->need_fit = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        a->pressed = BTN_NONE, a->down = a->panning = false;
        break;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Replay, for video overlays (puzzle only): one frame per state line of a
   file. A state line lists the pieces on the board in the order they were
   placed, each as its cells' labels joined by commas ("15,22,23,24,29
   jan,feb,jul,1,8"); a blank line is the empty board. "view <zoom> <panx>
   [<row_h> <scrolly>]" sets the camera for the lines after it (zoom in pixels
   per slot of the widest layer, panx the slot at the graph's centre, as in
   App; a layer height, which sizes the thumbnails, and the scroll), otherwise
   each frame fits and reveals the path's end. Writes NNNN.bmp per frame and
   layout.txt (the rects an overlay crops, the zoom range, and per frame where
   the path's end sits) to outdir; with outdir "-", streams frames to stdout
   as raw RGB24 instead and writes nothing else. A piece the rules don't allow
   (covering the date, say) still goes on, last and off the graph: a dead end, as
   on the real board. Renders in a hidden window. */

static uint64_t label_mask(char *cells) {
    uint64_t m = 0;
    char *save = NULL;
    for (char *t = SDL_strtok_r(cells, ",", &save); t; t = SDL_strtok_r(NULL, ",", &save)) {
        uint64_t bit = 0;
        for (int r = 0; r < PZ_BOARD && !bit; r++)
            for (int c = 0; c < PZ_BOARD && !bit; c++)
                if (pz_label(r, c) && !SDL_strcasecmp(pz_label(r, c), t)) bit = 1ull << (r * PZ_BOARD + c);
        if (!bit) return 0;
        m |= bit;
    }
    return m;
}

/* Appends a placement the rules don't allow from the path's end (one covering the date,
   say): what's on the real board is still shown, as a dead end off the graph. */
static bool force_push(App *a, uint64_t m) {
    SvPath *p = &a->path;
    for (int k = 0, n = sv_puzzle_placements(); k < n && p->n < SV_MAX_DEPTH; k++)
        if (sv_puzzle_mask((SvMove)k) == m) {
            memset(p->state[p->n + 1], 0, SV_MAX_STATE);
            a->g.game->apply(p->state[p->n], (SvMove)k, p->state[p->n + 1]);
            p->move[p->n] = (SvMove)k;
            p->node[++p->n] = -1;
            return true;
        }
    return false;
}

static void write_rect(FILE *f, const char *name, Rect r) { fprintf(f, "%s %d %d %d %d\n", name, r.x, r.y, r.w, r.h); }

/* The path's deepest node on the graph: its end, or before a dead end the last
   state that was still solvable. */
static int path_last_node(const App *a) {
    for (int i = a->path.n; i >= 0; i--)
        if (a->path.node[i] >= 0) return a->path.node[i];
    return 0;
}

static int replay(App *a, const char *file, const char *dir, int w, int h) {
    FILE *in = fopen(file, "r");
    if (!in) {
        fprintf(stderr, "statespace: can't read %s\n", file);
        return 1;
    }
    bool raw = !strcmp(dir, "-");
    a->win = SDL_CreateWindow("State Space", w, h, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    a->ren = a->win ? SDL_CreateRenderer(a->win, NULL) : NULL;
    if (!a->ren || !tiny_font_init(&a->font, a->ren)) {
        fprintf(stderr, "statespace: %s\n", SDL_GetError());
        return 1;
    }
    float dens = tiny_pixel_density(a->win, a->ren);
    if (dens > 1) SDL_SetWindowSize(a->win, (int)(w / dens), (int)(h / dens)); /* w x h output pixels */
    /* Draw into a texture the window's size, not the window: the same pixels without waiting on the
       window's drawable, ~25% faster. */
    int tw, th;
    SDL_GetRenderOutputSize(a->ren, &tw, &th);
    SDL_Texture *target = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
    if (!target || !SDL_SetRenderTarget(a->ren, target)) {
        fprintf(stderr, "statespace: %s\n", SDL_GetError());
        return 1;
    }
    a->mx = a->my = -10000; /* no hover */
    draw_frame(a);          /* fits the view */
    Layout L = layout(a);

    static SvMove moves[SV_MAX_MOVES];
    static char line[8192], prev[8192];
    char out[1024];
    /* per frame: depth, slot and layer size of the path's last node on the graph, whether that's the
       end, and the path's length */
    enum { MAX_INFO = 100000 };
    static int info_depth[MAX_INFO], info_len[MAX_INFO], info_end[MAX_INFO], info_n[MAX_INFO];
    static double info_slot[MAX_INFO];
    bool view = false;
    double vzoom = 0, vpan = 0;
    int vrow = 0, vscroll = 0;
    int frame = 0, lineno = 0;
    prev[0] = 1; /* matches no line */
    while (fgets(line, sizeof line, in)) {
        lineno++;
        if (!strncmp(line, "view ", 5)) {
            vrow = vscroll = 0;
            view = sscanf(line + 5, "%lf %lf %d %d", &vzoom, &vpan, &vrow, &vscroll) >= 2;
            continue;
        }
        if (strcmp(line, prev)) { /* a new state: rebuild the path */
            SDL_strlcpy(prev, line, sizeof prev);
            sv_path_reset(&a->g, &a->path);
            char *save = NULL;
            uint64_t later[PZ_PIECES];
            int nlater = 0;
            for (char *t = SDL_strtok_r(line, " \t\r\n", &save); t; t = SDL_strtok_r(NULL, " \t\r\n", &save)) {
                char cells[256];
                SDL_strlcpy(cells, t, sizeof cells);
                uint64_t m = label_mask(cells);
                const void *s = sv_path_end(&a->path);
                int n = m ? a->g.game->moves(s, moves) : 0, i = 0;
                while (i < n && sv_puzzle_mask(moves[i]) != m) i++;
                if (i < n && sv_path_push(&a->g, &a->path, moves[i])) continue;
                if (m && nlater < PZ_PIECES) later[nlater++] = m; /* illegal here: after the legal ones */
                else fprintf(stderr, "statespace: line %d: no placement covers %s, skipped\n", lineno, t);
            }
            for (int i = 0; i < nlater; i++)
                if (!force_push(a, later[i])) fprintf(stderr, "statespace: line %d: can't place a piece, skipped\n", lineno);
            a->redo = a->path.n;
            path_changed(a);
        }
        if (view) {
            a->row_h = vrow;
            L = layout(a);
            a->zoom = vzoom, a->panx = vpan, a->scrolly = vscroll, a->at_fit = false;
            clamp_view(a, &L);
        } else {
            reveal_end(a);
        }
        draw_frame(a);
        if (frame < MAX_INFO) {
            int n = path_last_node(a);
            info_depth[frame] = a->g.nodes[n].depth;
            info_len[frame] = sv_layer_len(&a->g, a->g.nodes[n].depth);
            info_slot[frame] = (sv_layer_x(&a->g, n) - 0.5) * a->maxw;
            info_end[frame] = a->path.node[a->path.n] >= 0;
            info_n[frame] = a->path.n;
        }
        SDL_Surface *surf = SDL_RenderReadPixels(a->ren, NULL);
        if (raw) {
            SDL_Surface *rgb = surf ? SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGB24) : NULL;
            if (!rgb) {
                fprintf(stderr, "statespace: %s\n", SDL_GetError());
                return 1;
            }
            for (int y = 0; y < rgb->h; y++) fwrite((Uint8 *)rgb->pixels + (size_t)y * rgb->pitch, 3, (size_t)rgb->w, stdout);
            SDL_DestroySurface(rgb);
        } else {
            snprintf(out, sizeof out, "%s/%04d.bmp", dir, frame);
            if (!surf || !SDL_SaveBMP(surf, out)) fprintf(stderr, "statespace: %s: %s\n", out, SDL_GetError());
        }
        SDL_DestroySurface(surf);
        frame++;
    }
    fclose(in);
    fflush(stdout);

    int ow, oh;
    SDL_GetRenderOutputSize(a->ren, &ow, &oh);
    if (!raw) {
        snprintf(out, sizeof out, "%s/layout.txt", dir);
        FILE *f = fopen(out, "w");
        if (f) {
            fprintf(f, "frames %d\nu %d\nsize %d %d\n", frame, L.u, ow, oh);
            write_rect(f, "header", (Rect){TINY_MARGIN * L.u, L.head_y, ow - 2 * TINY_MARGIN * L.u, L.head_h});
            write_rect(f, "canvas", L.canvas);
            write_rect(f, "graph", L.graph);
            write_rect(f, "panel", L.panel);
            write_rect(f, "board", L.board);
            fprintf(f, "text_y %d\nlist_y %d\nfoot_y %d\n", L.text_y, L.list_y, L.foot_y);
            fprintf(f, "maxw %d\nfit_zoom %.6f\nmax_zoom %.6f\nthumb_pitch %d\n", a->maxw, fit_zoom(a, &L),
                    max_zoom(a, &L), L.thumb_max + 8 * L.u);
            for (int i = 0; i < frame && i < MAX_INFO; i++)
                fprintf(f, "state %d %d %.4f %d %d %d\n", i, info_depth[i], info_slot[i], info_len[i], info_end[i],
                        info_n[i]);
            fclose(f);
        }
    }
    fprintf(stderr, "%d frames at %dx%d (unit %d)\n", frame, ow, oh, L.u);
    SDL_DestroyTexture(target);
    tiny_font_free(&a->font);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    return 0;
}

int main(int argc, char **argv) {
    static App app;
    App *a = &app;
    a->ent = &GAMES[0];
    a->fold = true;
    const char *replay_file = NULL, *replay_dir = NULL;
    int replay_w = 1600, replay_h = 900;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--replay")) {
            if (i + 2 >= argc || (i + 3 < argc && sscanf(argv[i + 3], "%dx%d", &replay_w, &replay_h) != 2) ||
                i + 4 < argc)
                goto usage;
            replay_file = argv[i + 1], replay_dir = argv[i + 2];
            argc = i;
        }
    if (argc > 3) goto usage;
    if (argc >= 2) {
        a->ent = NULL;
        for (size_t i = 0; i < SDL_arraysize(GAMES); i++)
            if (!strcmp(argv[1], GAMES[i].id)) a->ent = &GAMES[i];
        if (!a->ent) goto usage;
    }
    if (a->ent->configure ? !a->ent->configure(argc > 2 ? argv[2] : NULL) : argc > 2) goto usage;

    if (replay_file && strncmp(a->ent->id, "puzzle", 6)) goto usage;

    build(a);
    sv_path_reset(&a->g, &a->path);
    path_changed(a);

    if (replay_file) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        int rc = replay(a, replay_file, replay_dir, replay_w, replay_h);
        sv_free(&a->g);
        SDL_Quit();
        return rc;
    }

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!tiny_create_window("State Space", 1100, 720, 640, 480, &a->win, &a->ren) ||
        !tiny_font_init(&a->font, a->ren)) {
        SDL_Quit();
        return 1;
    }
    a->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    a->hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    a->grab = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);

    bool running = true;
    render(a);
    while (running) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) break;
        running = handle_event(a, &e);
        while (running && SDL_PollEvent(&e)) running = handle_event(a, &e);
        if (running) {
            update_cursor(a);
            render(a);
        }
    }

    SDL_DestroyCursor(a->arrow);
    SDL_DestroyCursor(a->hand);
    SDL_DestroyCursor(a->grab);
    sv_free(&a->g);
    tiny_font_free(&a->font);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    return 0;

usage:
    fprintf(stderr, "usage: statespace [game] [--replay states.txt outdir [WxH]]\ngames:\n");
    for (size_t i = 0; i < SDL_arraysize(GAMES); i++) fprintf(stderr, "  %s%s\n", GAMES[i].id, GAMES[i].args);
    return 2;
}
