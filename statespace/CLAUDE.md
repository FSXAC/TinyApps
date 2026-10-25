# statespace

A state-space visualizer. A game's positions are nodes and its moves are edges, drawn in layers by how many moves have been played (start at the top). Every node is marked with its value under perfect play: player 0 (X) wins as solid ink, a draw (or a puzzle's dead end) as gray, player 1 (O) wins as a hollow square, and unknown as shade. Sparse layers show board thumbnails, and dense ones show dots; hovering a dot shows its board.

Play on the side board or click any node to jump there. Clicking a point on the side board plays the one move through it, or, if several moves pass through it (a puzzle piece covers many cells), lists them under the board as boards to pick from, best first; hovering one previews it. The path is drawn in ink, with a double ring on the current state. `Backspace`/`←`/`⌘Z` step back, `→` steps forward again, and `Esc` returns to the start. **Fold** (`F`) merges symmetric positions, **Group** (`G`) sorts each layer by value, and **Fit** (`0`) resets the view. Scroll to pan, and pinch, `⌘`-scroll or `=`/`-` to zoom. `statespace [game]` picks a game: `ttt`, `puzzle [MM-DD]` or `puzzle-lowest [MM-DD]` (the date defaults to today).

## Games

- **`ttt`**: tic-tac-toe. Each open square on the side board carries the mark of the value it leads to.
- **`puzzle`**: the Whole Year Puzzle (`../puzzle`, through `puzzlecore`'s public API) for one date, placing any piece anywhere. The full space is billions of states, so the graph holds only the solvable ones (every subset of a solution's pieces, e.g. 20,617 for Oct 5 and 300,440 for Jan 28, the largest), found backwards from the solutions. A move off the graph leads to a dead end; the path follows it there (its node is -1). On the side board, each empty cell shows how many moves covering it keep the puzzle solvable, and a cell nothing solvable can cover is dithered: the cells with 1 are the forced ones.
- **`puzzle-lowest`**: the same, but each move must cover the lowest empty cell (the solver's rule). That space is small (54,017 states for Oct 5, at most 485,111), so it's explored forwards, dead ends included, which shows how much of the solver's search is wasted.

## Strategy measurements

`./build/bin/sv_insights pieces|cells|first [date]` (`tools/insights.c`, not part of the app) searches the puzzle's strategy trees to exhaustion and counts nodes, alive (in some solution) versus dead. `pieces` compares piece orders, `cells` compares which empty cell to fill next (`cells all` takes about 15 minutes), `first` measures which first placements stay solvable. These are raw trees with no pruning of stranded regions.

## Commands

Built from the repo root (see `../CLAUDE.md`): `cmake --build build --target statespace`, run `./build/bin/statespace` (launcher `statespace`), test with `./build/bin/sv_tests`.

## Architecture

- **`src/sv.{c,h}` is the SDL-free core** (`svcore`, with the game rules). A game is an `SvGame` of function pointers: initial state, whose move it is, outcome, moves, apply, an optional `canonical` (symmetry folding), and move names. States are fixed-size byte blobs, hashed and compared as bytes, so the same position reached in different orders is one node. Games must guarantee that every path to a state has the same length; the graph is layered by that depth.
- **Backwards exploration** (`sv_explore_back`) is for one-player puzzles too big to walk forwards. A game gives its solved states (`goals`) and how to undo a move (`unmoves`/`unapply`); the core walks parents from the goals, then sets depths by walking forwards from the start. The graph is then *closed*: everything in it is solvable, and `sv_state_value` calls any state missing from it a dead end.
- **Values come from the core, not the games.** `sv_solve` works backwards from the deepest layer: the mover takes a win if any child is one, otherwise a draw, otherwise a loss. A node with no moves that isn't over is lost by the mover (for a one-player puzzle, a dead end). Unexplored nodes stay `SV_UNKNOWN`, so a partial graph (`sv_explore` stops at a node budget) still gets every value that is certain.
- **`SvPath` is the game as played.** It keeps the real (unfolded) states, so the side board never jumps to a symmetric twin, and maps each onto its node. Jumping to a node keeps as much of the current path as lies on the way to it. Moving off the explored graph expands the end node on demand.
- **Layout (`sv_order`):** each layer spreads across the full width. Nodes are sorted by the mean position of their parents (as fractions of their layers), optionally grouped by value first. Zoom is horizontal only, and rows always fill the canvas height unless there are too many layers, which then scroll.
- **Adding a game:** rules in `games/<game>.c` (declared in `games/games.h`, added to `svcore`), drawing in `games/<game>_view.c` as an `SvView` (`draw` a square board at any size from about 9 units, `move_hit` to say which moves pass through a point, and an optional `annotate` for marks over the side board), and an entry in `GAMES` in `src/main.c` (with `configure` if it takes an argument).
- **Big graphs:** dots that land on the same pixel are drawn once, and the faint all-edges layer is skipped above `MAX_FAINT_EDGES` (20,000); the path, the current state's moves and the hovered node's edges are always drawn.
- `sv_test` checks the known tic-tac-toe counts (5,478 positions, 765 folded, 958 finished games, a draw from the start) and, for the puzzle on Oct 5, the 20,617 solvable states and 44 solutions, dead-end paths, and that both rules agree on which states are solvable.
