# puzzle

The Whole Year Puzzle (Palmetto Puzzle Works), on screen. Nine pieces cover a 43-cell calendar board (Jan–Dec, 1–31). For each date, the goal is to leave only that month and day showing. The app opens on today's date (local time), and `--date YYYY-MM-DD` plays another. Drag pieces with the mouse. `Space`/`R` rotates the held or hovered piece clockwise, and `F` or right-click flips it. `Esc` drops a drag, leaves the solver, or puts every piece back in the tray. `T` jumps to today, and `⌘Q`/`⌘W` quit.

Solved dates are logged to `solved.txt` (one `YYYY-MM-DD` per line) in `SDL_GetPrefPath("tinyapps", "puzzle")`, i.e. `~/Library/Application Support/tinyapps/puzzle/`. The tracker under the table shows the selected date's year, with one square per day, and clicking a day plays it. Once a date has been solved, its **Solve** button unlocks. It enumerates every solution, which you step through with `‹ ›` or `←/→`.

## Commands

Built from the repo root (see `../CLAUDE.md`): `cmake --build build --target puzzle`, run `./build/bin/puzzle` (launcher `puzzle`), test with `./build/bin/puzzle_tests`.

## Architecture

- **`src/puzzle.{c,h}` is SDL-free** (`puzzlecore`): piece shapes, the board, placement rules, the solver and the solved-date log. Everything sits on one table grid (`PZ_TABLE_W` x `PZ_TABLE_H`). The board occupies rows/cols 0–6, and the rest is the tray. A `PzPlace` is an orientation plus the table cell of the bounding box's top-left. Orientation bits 0–1 count clockwise turns, and bit 2 mirrors first. Cell `i` of a piece is the same physical square in every orientation, which is how a held piece rotates around the cell under the cursor.
- **Piece shapes were read off a photo of the real board**, solved for Oct 2. `puzzle_test` checks that this arrangement is among the solver's solutions and that every date has at least one (11 to 734 per date).
- **Solver:** exact cover by backtracking over a 64-bit board mask. It always fills the lowest empty cell, with placements precomputed and indexed by their lowest cell, and symmetric orientations deduplicated so no solution is counted twice. A single date takes a few milliseconds. The app keeps the first `MAX_SOLUTIONS` (1024).
- **`src/main.c`** recomputes `layout()` from the window size on each use (cell size, header buttons, tracker), so drawing and hit-testing always agree. Drops snap to the nearest cell and are refused (the piece goes back) unless `pz_fits`. A legal landing spot is previewed in 50% gray. The only timed wake-up is local midnight, which moves "today".
