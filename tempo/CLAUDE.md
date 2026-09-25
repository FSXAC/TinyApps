# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A tiny tap-tempo (BPM) counter. Tap any key or click anywhere in the window; the big number shows the rounded BPM, with the one-decimal value and tap count below it. After a pause longer than `TEMPO_RESET_MS` (2 s), the reading dims and the next tap starts a new count. `Esc`/`Backspace`/`Delete` reset manually; `Cmd/Ctrl+Q` or `Cmd/Ctrl+W` quit. It's a small utility, so function comes before polish. It's a sibling of `../TinyPainter` and follows the same conventions: C99, SDL2 (Homebrew `sdl2-compat`), a plain Makefile, and no SDL_ttf.

## Commands

```sh
make            # build ./tempo (needs sdl2-config on PATH)
make run        # build and run
make test       # build and run tests/tempo_test.c (no SDL needed)
make clean
ln -sfn "$PWD/tempo" ~/.local/bin/tempo   # `tempo` launcher (already set up; points at the repo binary)
```

Tests use a minimal `CHECK`/`CHECK_NEAR` harness with no framework. To add a test, write a `test_*` function in `tests/tempo_test.c` and call it from `main`.

## Architecture

- **`src/tempo.{c,h}` has no SDL dependency** and holds all tempo logic: a 16-entry ring buffer of tap times (`TEMPO_WINDOW`). BPM is `(n-1)` intervals over the span of the last `n` taps, so it follows tempo changes. The auto-reset happens inside `tempo_tap` when the previous run has expired. Times are `uint32_t` ms, and the math relies on unsigned subtraction so it survives wraparound.
- **Tap times come from SDL event timestamps** (`e->key.timestamp`, `e->button.timestamp`), not from when the event is handled. This keeps processing jitter out of the measurement. Expiry checks compare those timestamps against `SDL_GetTicks()`, which uses the same clock.
- **Rendering is event-driven.** The main loop blocks in `SDL_WaitEvent`/`SDL_WaitEventTimeout` and redraws only after an event or when `next_timeout()` says something is due (the end of the tap flash, or the auto-reset dimming). Any new time-based visual must add its deadline to `next_timeout()`, or it won't redraw.
- **Text is a built-in 5x7 bitmap font** (`FONT` in `main.c`) that contains only the glyphs the UI uses. Add a glyph before you use a new character, or it will be skipped without any warning. Everything is drawn in output pixels (`SDL_GetRendererOutputSize`, HiDPI window) and scaled from the window size. The big number is always sized for 3 digits so it doesn't jump around.
- Key repeats, bare modifier keys, and Cmd/Ctrl chords never count as taps. `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` is set so the click that focuses the window counts as a tap.
