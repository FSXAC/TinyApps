# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A monorepo of small, practical desktop tools built on SDL3. They favor function over polish. Each app has its own `CLAUDE.md` with app-specific notes.

| Dir | Binary / launcher | Lang | What |
|---|---|---|---|
| `tinypainter/` | `tinypainter` / `tpt` | C99 | 32x32 black & white pixel painter (see its README for controls) |
| `tempo/` | `tempo` / `tempo` | C99 | tap BPM counter |
| `musicnotes/` | `musicnotes` / `nts` | C++17 | on-screen piano that logs note/chord names |
| `common/` | `tiny` static lib | C | shared window setup, design language (`DESIGN.md`), 5x7 bitmap font, test macros |

## Commands

```sh
cmake -S . -B build                          # configure (needs Homebrew sdl3)
cmake --build build -j                       # build everything; binaries land in build/bin/
cmake --build build --target tempo           # build one app
cmake --build build --target links           # (re)create ~/.local/bin/{tpt,tempo,nts} -> build/bin/*
ctest --test-dir build --output-on-failure   # all tests
./build/bin/tempo_tests                      # one test binary directly (also musiccore_tests)
```

The launchers are symlinks into `build/bin`, so rebuilding updates them. There's no install step.

## Conventions

- **SDL3 only.** The Homebrew `sdl2` is actually `sdl2-compat`; don't add SDL2 code. In SDL3, most functions return `bool` (true = success), event timestamps are `uint64` nanoseconds, and render rects are `SDL_FRect`.
- **Each app keeps its logic SDL-free** so it can be unit tested: `tempo/src/tempo.c` → `tempocore`, and `musicnotes`' `musiccore`. Tests use tiny homemade harnesses with no framework: `common/check.h` for C (end `main` with `return check_report();`) and `musicnotes`' own `CHECK` for C++.
- **Rendering is event-driven** in every app: block in `SDL_WaitEvent`/`SDL_WaitEventTimeout` and redraw only after events. Anything animated or time-based needs a wake-up timeout, or it won't redraw.
- **Pixel coordinates:** the C apps lay out in renderer output pixels (`SDL_GetRenderOutputSize`, windows created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`) and size UI in design units (`tiny_unit()`). `musicnotes` instead uses a fixed logical presentation.
- **All UI follows `common/DESIGN.md`**, a classic-Mac design language: ink/paper palette tokens, whole-unit sizes, bitmap type roles, stepped rounded corners, and inversion for pressed or selected states. Use the `tiny_*` helpers and `TinyColor` tokens, never raw RGB or TTF fonts. Read it before any UI work.
- **Checking visuals:** `TINY_SCREENSHOT=out.bmp ./build/bin/<app>` saves the first frame and quits (convert with `sips -s format png`). Don't send synthetic keystrokes to check the UI.
- **`common/` holds only what is actually shared**; each app still owns its `main` and event loop. `tiny_text` draws in the renderer's *current draw color* (`tiny_set_color` first) and returns the width drawn. `0` and `O` are deliberately identical (unslashed).
- To add an app: create `<app>/CMakeLists.txt`, `add_subdirectory` it in the root `CMakeLists.txt`, link `tiny`, and add its launcher to the `links` target.
