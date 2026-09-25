# tempo

A tiny tap-tempo (BPM) counter. Tap any key or click anywhere in the window; the big number shows the rounded BPM, with the one-decimal value and tap count below it. After a pause longer than `TEMPO_RESET_NS` (2 s), the reading dims and the next tap starts a new count. `Esc`/`Backspace`/`Delete` reset manually; `Cmd/Ctrl+Q` or `Cmd/Ctrl+W` quit.

## Commands

Built from the repo root (see `../CLAUDE.md`): `cmake --build build --target tempo`, run `./build/bin/tempo` (launcher `tempo`), test with `./build/bin/tempo_tests`. Tests live in `tests/tempo_test.c` (`common/check.h`); add a `test_*` function and call it from `main`.

## Architecture

- **`src/tempo.{c,h}` is SDL-free** (built as `tempocore`) and holds all tempo logic: a 16-entry ring buffer of tap times (`TEMPO_WINDOW`). BPM is `(n-1)` intervals over the span of the last `n` taps, so it follows tempo changes. The auto-reset happens inside `tempo_tap` when the previous run has expired. Times are `uint64_t` nanoseconds.
- **Tap times come from SDL event timestamps** (`e->key.timestamp`, `e->button.timestamp`), not from when the event is handled. This keeps processing jitter out of the measurement. Expiry checks compare those timestamps against `SDL_GetTicksNS()`, which uses the same clock.
- **Wake-ups:** `next_timeout()` returns the next deadline, either the end of the tap flash or the auto-reset dimming. Any new time-based visual must add its deadline there.
- **Text uses the shared `common/` bitmap font** (`tiny_text*`), drawn in output pixels and scaled from the window size. The big number is always sized for 3 digits so it doesn't jump around.
- Key repeats, bare modifier keys, and Cmd/Ctrl chords never count as taps. `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` is set so the click that focuses the window counts as a tap.
