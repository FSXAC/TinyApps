/*
 * State-space core: a game-agnostic graph of game states (nodes) joined by
 * moves (edges). Games plug in through SvGame; the core explores the graph,
 * merges states reached by different move orders, and works out each state's
 * value under perfect play. SDL-free so it can be unit tested.
 *
 * Contract for games:
 *  - A state is `state_size` raw bytes, compared and hashed as bytes, so a
 *    game must write every byte (no stray padding).
 *  - Every path to a state has the same length (a move adds one mark, disc or
 *    piece). The graph is then layered by depth, which the core relies on.
 */
#ifndef SV_H
#define SV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SV_MAX_MOVES 1024 /* moves from one state */
#define SV_MAX_DEPTH 64   /* moves in one game */
#define SV_MAX_STATE 256  /* bytes per state */

typedef uint32_t SvMove;

/* Outcomes and values: a player index (>= 0) wins, or one of these. For a
   one-player puzzle "player 0 wins" means solved and SV_NOBODY means stuck. */
enum {
    SV_PLAYING = -3, /* outcome: the game goes on */
    SV_UNKNOWN = -2, /* value: not worked out (unexplored below) */
    SV_NOBODY = -1,  /* a draw, or a dead end */
};

typedef struct {
    const char *name; /* "Tic-tac-toe" */
    int players;      /* 1 for puzzles, 2 for games */
    int state_size;   /* bytes, at most SV_MAX_STATE */
    void (*initial)(void *s);
    int (*to_move)(const void *s);
    /* SV_PLAYING, SV_NOBODY or the winner. A state that is still playing but
       has no moves is lost by the player to move. */
    int (*outcome)(const void *s);
    /* Fills out[] (up to SV_MAX_MOVES) and returns the count. */
    int (*moves)(const void *s, SvMove *out);
    void (*apply)(const void *s, SvMove m, void *out);
    /* Optional: rewrites a state as its symmetry class's representative, so
       symmetric states fold into one node. */
    void (*canonical)(void *s);
    /* Short name of a move played from s, e.g. "X b2". */
    void (*move_name)(const void *s, SvMove m, char *buf, int n);
    const char *player_names[2]; /* "X", "O"; a puzzle names only its solver */

    /* Optional, for one-player puzzles whose full space is too big to walk
       forwards: every solved state (passed to emit), and the moves that
       could have led to a state, undone. With these, sv_explore_back finds
       exactly the states from which a solution can still be reached. */
    void (*goals)(void (*emit)(void *ctx, const void *s), void *ctx);
    int (*unmoves)(const void *s, SvMove *out);
    void (*unapply)(const void *s, SvMove m, void *out);
} SvGame;

typedef struct {
    int32_t from, to;
    SvMove move; /* as played from `from`'s stored state */
    int32_t next_out, next_in;
} SvEdge;

typedef struct {
    int32_t first_out, last_out, first_in; /* edge lists, -1 if empty */
    int32_t nout, nin;
    int32_t pos;    /* index within its layer, after sv_order */
    int32_t mark;   /* scratch */
    int16_t depth;
    int8_t mover, outcome, value;
    bool expanded; /* all its moves have been generated */
} SvNode;

typedef struct {
    const SvGame *game;
    bool fold; /* merge symmetric states (when the game can) */

    int nnodes, ncap;
    SvNode *nodes;
    uint8_t *states; /* nnodes * state_size */
    int nedges, ecap;
    SvEdge *edges;
    int32_t *table; /* open-addressed hash of node indices, -1 = empty */
    int tcap;
    int next_expand; /* nodes before this are expanded (BFS cursor) */
    /* Built by sv_explore_back: the graph holds every state that can still
       be solved, so any state missing from it is a dead end. */
    bool closed;

    /* Filled by sv_order: nodes sorted by depth, then by position. */
    int nlayers;
    int32_t *layer;                     /* nnodes entries */
    int32_t layer_start[SV_MAX_DEPTH + 2];
} SvGraph;

/* Starts a graph holding just the initial state (node 0). */
bool sv_init(SvGraph *g, const SvGame *game, bool fold);
void sv_free(SvGraph *g);
static inline const void *sv_state(const SvGraph *g, int n) { return g->states + (size_t)n * g->game->state_size; }

/* The node for a state (canonicalized when folding), or -1. */
int sv_find(const SvGraph *g, const void *s);
/* Generates a node's moves, adding the states they lead to. Returns false
   when out of memory. */
bool sv_expand(SvGraph *g, int n);
/* Expands breadth-first until every node is expanded (returns true) or the
   graph holds `max_nodes`. */
bool sv_explore(SvGraph *g, int max_nodes);
/* Builds the graph backwards from the game's goals (see SvGame.goals) and
   closes it. Returns false when out of memory or past `max_nodes`. */
bool sv_explore_back(SvGraph *g, int max_nodes);
/* The value of any state: its node's, or for a state missing from a closed
   graph SV_NOBODY. */
int sv_state_value(const SvGraph *g, const void *s);
/* Works out every node's value under perfect play, deepest layer first. */
void sv_solve(SvGraph *g);
/* Orders each layer for drawing: by the mean position of each node's parents,
   so families stay together, optionally grouped by value first. */
void sv_order(SvGraph *g, bool by_value);
static inline int sv_layer_len(const SvGraph *g, int d) { return g->layer_start[d + 1] - g->layer_start[d]; }
/* A node's place across its layer, from 0 to 1 (its slot's center). */
static inline double sv_layer_x(const SvGraph *g, int n) {
    return (g->nodes[n].pos + 0.5) / sv_layer_len(g, g->nodes[n].depth);
}
/* The value seen from `player`: 1 a win, 0 a draw/dead end, -1 a loss,
   -2 unknown. */
int sv_value_for(int value, int player);

/* ------------------------------------------------------------------ */
/* A path through the graph: the game as actually played. It keeps the real
   (unfolded) states, so a board doesn't jump to a symmetric twin, and maps
   each onto its node. */

typedef struct {
    int n; /* moves played; states 0..n */
    /* node[i] is -1 for a state off a closed graph (a dead end). */
    SvMove move[SV_MAX_DEPTH];
    int32_t node[SV_MAX_DEPTH + 1];
    uint8_t state[SV_MAX_DEPTH + 1][SV_MAX_STATE];
} SvPath;

void sv_path_reset(const SvGraph *g, SvPath *p);
static inline const void *sv_path_end(const SvPath *p) { return p->state[p->n]; }
/* Plays a legal move from the path's end, expanding the end first if need
   be. Fails on an illegal move, or one that leaves an open graph. */
bool sv_path_push(SvGraph *g, SvPath *p, SvMove m);
bool sv_path_pop(SvPath *p);
/* Rebuilds the path to end at node `n`, keeping as much of the current path
   as is on the way. */
bool sv_path_to(SvGraph *g, SvPath *p, int n);
/* Re-finds every node after the graph changed (e.g. folding was toggled).
   Truncates at the first state that is no longer in the graph. */
void sv_path_refresh(SvGraph *g, SvPath *p);

#endif
