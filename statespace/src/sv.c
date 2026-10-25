#include "sv.h"

#include <stdlib.h>
#include <string.h>

static uint64_t hash(const void *s, int n) { /* FNV-1a */
    const uint8_t *b = s;
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static bool can_fold(const SvGraph *g) { return g->fold && g->game->canonical; }

static void normalize(const SvGraph *g, void *s) {
    if (can_fold(g)) g->game->canonical(s);
}

static int slot(const SvGraph *g, const void *s) {
    int sz = g->game->state_size;
    size_t i = hash(s, sz) & (size_t)(g->tcap - 1);
    while (g->table[i] >= 0 && memcmp(sv_state(g, g->table[i]), s, sz)) i = (i + 1) & (size_t)(g->tcap - 1);
    return (int)i;
}

static bool grow_table(SvGraph *g) {
    int cap = g->tcap ? g->tcap * 2 : 1024;
    int32_t *t = malloc(sizeof *t * cap);
    if (!t) return false;
    memset(t, -1, sizeof *t * cap);
    free(g->table);
    g->table = t, g->tcap = cap;
    for (int n = 0; n < g->nnodes; n++) g->table[slot(g, sv_state(g, n))] = n;
    return true;
}

/* Finds or adds the node for an already normalized state. */
static int intern(SvGraph *g, const void *s, int depth) {
    if (g->tcap) {
        int i = slot(g, s);
        if (g->table[i] >= 0) return g->table[i];
    }
    int sz = g->game->state_size;
    if (g->nnodes == g->ncap) {
        int cap = g->ncap ? g->ncap * 2 : 1024;
        SvNode *nodes = realloc(g->nodes, sizeof *nodes * cap);
        if (!nodes) return -1;
        g->nodes = nodes;
        uint8_t *states = realloc(g->states, (size_t)sz * cap);
        if (!states) return -1;
        g->states = states;
        int32_t *layer = realloc(g->layer, sizeof *layer * cap);
        if (!layer) return -1;
        g->layer = layer;
        g->ncap = cap;
    }
    if ((g->nnodes + 1) * 2 > g->tcap && !grow_table(g)) return -1;
    int n = g->nnodes++;
    memcpy(g->states + (size_t)n * sz, s, sz);
    g->table[slot(g, s)] = n;
    g->nodes[n] = (SvNode){.first_out = -1, .last_out = -1, .first_in = -1, .mark = -1, .depth = (int16_t)depth,
                           .mover = (int8_t)g->game->to_move(s), .outcome = (int8_t)g->game->outcome(s),
                           .value = SV_UNKNOWN};
    return n;
}

bool sv_init(SvGraph *g, const SvGame *game, bool fold) {
    memset(g, 0, sizeof *g);
    g->game = game, g->fold = fold;
    uint8_t s[SV_MAX_STATE] = {0};
    game->initial(s);
    normalize(g, s);
    if (intern(g, s, 0) < 0) {
        sv_free(g);
        return false;
    }
    return true;
}

void sv_free(SvGraph *g) {
    free(g->nodes), free(g->states), free(g->edges), free(g->table), free(g->layer);
    memset(g, 0, sizeof *g);
}

int sv_find(const SvGraph *g, const void *s) {
    uint8_t t[SV_MAX_STATE];
    memcpy(t, s, g->game->state_size);
    normalize(g, t);
    int i = slot(g, t);
    return g->table[i];
}

static bool add_edge(SvGraph *g, int from, int to, SvMove m) {
    if (g->nedges == g->ecap) {
        int cap = g->ecap ? g->ecap * 2 : 4096;
        SvEdge *e = realloc(g->edges, sizeof *e * cap);
        if (!e) return false;
        g->edges = e, g->ecap = cap;
    }
    int e = g->nedges++;
    SvNode *a = &g->nodes[from], *b = &g->nodes[to];
    g->edges[e] = (SvEdge){from, to, m, -1, b->first_in};
    if (a->last_out >= 0) g->edges[a->last_out].next_out = e;
    else a->first_out = e;
    a->last_out = e;
    b->first_in = e;
    a->nout++, b->nin++;
    return true;
}

bool sv_expand(SvGraph *g, int n) {
    if (g->nodes[n].expanded) return true;
    if (g->nodes[n].outcome == SV_PLAYING) {
        static SvMove moves[SV_MAX_MOVES];
        uint8_t s[SV_MAX_STATE], t[SV_MAX_STATE];
        memcpy(s, sv_state(g, n), g->game->state_size);
        int k = g->game->moves(s, moves);
        for (int i = 0; i < k; i++) {
            g->game->apply(s, moves[i], t);
            normalize(g, t);
            int c = intern(g, t, g->nodes[n].depth + 1);
            if (c < 0) return false;
            /* Symmetric moves can reach one folded state: link it once. */
            if (g->nodes[c].mark == n) continue;
            g->nodes[c].mark = n;
            if (!add_edge(g, n, c, moves[i])) return false;
        }
    }
    g->nodes[n].expanded = true;
    return true;
}

bool sv_explore(SvGraph *g, int max_nodes) {
    while (g->next_expand < g->nnodes) {
        if (g->nnodes >= max_nodes) return false;
        if (!sv_expand(g, g->next_expand)) return false;
        g->next_expand++;
    }
    return true;
}

/* Backwards from the goals: each state's unmoves give its parents, and
   every state reached this way can be solved. */

typedef struct {
    SvGraph *g;
    bool fail;
} GoalCtx;

static void add_goal(void *ctx, const void *s) {
    GoalCtx *c = ctx;
    uint8_t t[SV_MAX_STATE] = {0};
    memcpy(t, s, c->g->game->state_size);
    normalize(c->g, t);
    if (!c->fail && intern(c->g, t, 0) < 0) c->fail = true;
}

/* Depths, by walking forwards from the start over the edges found. */
static bool set_depths(SvGraph *g) {
    int32_t *queue = malloc(sizeof *queue * g->nnodes);
    if (!queue) return false;
    for (int n = 0; n < g->nnodes; n++) g->nodes[n].depth = -1;
    int head = 0, tail = 0;
    g->nodes[0].depth = 0;
    queue[tail++] = 0;
    while (head < tail) {
        int n = queue[head++];
        for (int e = g->nodes[n].first_out; e >= 0; e = g->edges[e].next_out) {
            SvNode *c = &g->nodes[g->edges[e].to];
            if (c->depth < 0) c->depth = (int16_t)(g->nodes[n].depth + 1), queue[tail++] = g->edges[e].to;
        }
    }
    free(queue);
    for (int n = 0; n < g->nnodes; n++)
        if (g->nodes[n].depth < 0) g->nodes[n].depth = 0; /* unreachable from the start */
    return true;
}

bool sv_explore_back(SvGraph *g, int max_nodes) {
    const SvGame *gm = g->game;
    if (!gm->goals || !gm->unmoves || !gm->unapply) return false;
    GoalCtx ctx = {g, false};
    gm->goals(add_goal, &ctx);
    if (ctx.fail) return false;
    static SvMove moves[SV_MAX_MOVES];
    uint8_t t[SV_MAX_STATE];
    for (int n = 0; n < g->nnodes; n++) {
        if (g->nnodes > max_nodes) return false;
        int k = gm->unmoves(sv_state(g, n), moves);
        for (int i = 0; i < k; i++) {
            memset(t, 0, sizeof t);
            gm->unapply(sv_state(g, n), moves[i], t);
            normalize(g, t);
            int p = intern(g, t, 0);
            if (p < 0) return false;
            if (g->nodes[p].mark == n) continue;
            g->nodes[p].mark = n;
            if (!add_edge(g, p, n, moves[i])) return false;
        }
    }
    for (int n = 0; n < g->nnodes; n++) g->nodes[n].expanded = true;
    g->next_expand = g->nnodes;
    g->closed = true;
    return set_depths(g);
}

int sv_state_value(const SvGraph *g, const void *s) {
    int n = sv_find(g, s);
    if (n >= 0) return g->nodes[n].value;
    return g->closed ? SV_NOBODY : SV_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* Values. */

int sv_value_for(int value, int player) {
    if (value == SV_UNKNOWN) return -2;
    if (value == SV_NOBODY) return 0;
    return value == player ? 1 : -1;
}

/* Sorts node indices by depth into g->layer (counting sort). */
static void bucket_layers(SvGraph *g) {
    int counts[SV_MAX_DEPTH + 1] = {0};
    g->nlayers = 0;
    for (int n = 0; n < g->nnodes; n++) {
        int d = g->nodes[n].depth;
        counts[d]++;
        if (d + 1 > g->nlayers) g->nlayers = d + 1;
    }
    g->layer_start[0] = 0;
    for (int d = 0; d < g->nlayers; d++) g->layer_start[d + 1] = g->layer_start[d] + counts[d];
    int fill[SV_MAX_DEPTH + 1];
    memcpy(fill, g->layer_start, sizeof fill);
    for (int n = 0; n < g->nnodes; n++) g->layer[fill[g->nodes[n].depth]++] = n;
}

static int8_t solve_node(const SvGraph *g, const SvNode *v) {
    if (v->outcome != SV_PLAYING) return v->outcome;
    if (!v->expanded) return SV_UNKNOWN;
    int p = v->mover, other = g->game->players > 1 ? 1 - p : SV_NOBODY;
    if (!v->nout) return (int8_t)other; /* no moves: the mover loses */
    /* Best for the mover: a win, then a draw (or dead end), then a loss. */
    int best = -3;
    bool unknown = false;
    for (int e = v->first_out; e >= 0; e = g->edges[e].next_out) {
        int cv = g->nodes[g->edges[e].to].value, s = sv_value_for(cv, p);
        if (s == 1) return (int8_t)p;
        if (s == -2) unknown = true;
        else if (s > best) best = s;
    }
    if (unknown) return SV_UNKNOWN;
    return (int8_t)(best == 0 ? SV_NOBODY : other);
}

void sv_solve(SvGraph *g) {
    bucket_layers(g);
    for (int i = g->nnodes - 1; i >= 0; i--) {
        SvNode *v = &g->nodes[g->layer[i]];
        v->value = solve_node(g, v);
    }
}

/* ------------------------------------------------------------------ */
/* Layer order. */

typedef struct {
    int32_t node;
    int bucket;
    double key;
} OrderItem;

static int cmp_items(const void *a, const void *b) {
    const OrderItem *x = a, *y = b;
    if (x->bucket != y->bucket) return x->bucket - y->bucket;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->node - y->node;
}

static int value_bucket(int v) { return v == 0 ? 0 : v == SV_NOBODY ? 1 : v == 1 ? 2 : 3; }

void sv_order(SvGraph *g, bool by_value) {
    bucket_layers(g);
    OrderItem *items = malloc(sizeof *items * (g->nnodes ? g->nnodes : 1));
    if (!items) return;
    for (int d = 0; d < g->nlayers; d++) {
        int a = g->layer_start[d], b = g->layer_start[d + 1];
        for (int i = a; i < b; i++) {
            int n = g->layer[i];
            /* Parents' positions as fractions of their layer's width, so
               layers of any size line up. */
            double sum = 0;
            int k = 0;
            for (int e = g->nodes[n].first_in; e >= 0; e = g->edges[e].next_in) {
                int pn = g->edges[e].from;
                sum += sv_layer_x(g, pn);
                k++;
            }
            items[i - a] = (OrderItem){n, by_value ? value_bucket(g->nodes[n].value) : 0, k ? sum / k : 0.5};
        }
        qsort(items, b - a, sizeof *items, cmp_items);
        for (int i = a; i < b; i++) {
            g->layer[i] = items[i - a].node;
            g->nodes[g->layer[i]].pos = i - a;
        }
    }
    free(items);
}

/* ------------------------------------------------------------------ */
/* Paths. */

void sv_path_reset(const SvGraph *g, SvPath *p) {
    p->n = 0;
    memset(p->state[0], 0, SV_MAX_STATE);
    g->game->initial(p->state[0]);
    p->node[0] = sv_find(g, p->state[0]);
}

static bool legal(const SvGraph *g, const void *s, SvMove m) {
    static SvMove moves[SV_MAX_MOVES];
    if (g->game->outcome(s) != SV_PLAYING) return false;
    int k = g->game->moves(s, moves);
    for (int i = 0; i < k; i++)
        if (moves[i] == m) return true;
    return false;
}

static int child(const SvGraph *g, const void *s, SvMove m) {
    uint8_t t[SV_MAX_STATE] = {0};
    g->game->apply(s, m, t);
    return sv_find(g, t);
}

bool sv_path_push(SvGraph *g, SvPath *p, SvMove m) {
    const void *s = sv_path_end(p);
    if (p->n >= SV_MAX_DEPTH || !legal(g, s, m)) return false;
    if (p->node[p->n] >= 0 && !sv_expand(g, p->node[p->n])) return false;
    int c = child(g, s, m);
    if (c < 0 && !g->closed) return false;
    memset(p->state[p->n + 1], 0, SV_MAX_STATE);
    g->game->apply(p->state[p->n], m, p->state[p->n + 1]);
    p->move[p->n] = m;
    p->node[++p->n] = c;
    return true;
}

bool sv_path_pop(SvPath *p) {
    if (!p->n) return false;
    p->n--;
    return true;
}

bool sv_path_to(SvGraph *g, SvPath *p, int n) {
    /* Walk up to the root, preferring parents already on the path. */
    int chain[SV_MAX_DEPTH + 1], d = g->nodes[n].depth;
    if (d > SV_MAX_DEPTH) return false;
    chain[d] = n;
    for (int i = d; i > 0; i--) {
        int v = chain[i], pick = -1;
        for (int e = g->nodes[v].first_in; e >= 0; e = g->edges[e].next_in) {
            int from = g->edges[e].from;
            if (pick < 0 || (i - 1 <= p->n && p->node[i - 1] == from)) pick = from;
        }
        if (pick < 0) return false;
        chain[i - 1] = pick;
    }
    /* Keep the shared prefix, then replay a move into each next node. */
    int keep = 0;
    while (keep < p->n && keep < d && p->node[keep + 1] == chain[keep + 1]) keep++;
    p->n = keep;
    static SvMove moves[SV_MAX_MOVES];
    while (p->n < d) {
        int k = g->game->moves(sv_path_end(p), moves), i = 0;
        while (i < k && child(g, sv_path_end(p), moves[i]) != chain[p->n + 1]) i++;
        if (i == k || !sv_path_push(g, p, moves[i])) return false;
    }
    return true;
}

void sv_path_refresh(SvGraph *g, SvPath *p) {
    for (int i = 0; i <= p->n; i++) {
        p->node[i] = sv_find(g, p->state[i]);
        if (p->node[i] < 0 && !g->closed) {
            p->n = i > 0 ? i - 1 : 0;
            return;
        }
    }
}
