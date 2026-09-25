# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A monorepo of small, practical desktop tools built on SDL3. They favor function over polish. Each app has its own `CLAUDE.md` with app-specific notes.

| Dir | Binary / launcher | Lang | What |
|---|---|---|---|
| `tinypainter/` | `tinypainter` / `tpt` | C99 | 32x32 black & white pixel painter (see its README for controls) |
| `tempo/` | `tempo` / `tempo` | C99 | tap BPM counter |
| `musicnotes/` | `musicnotes` / `nts` | C++17 | on-screen piano that logs note/chord names; uses SDL3_ttf |
| `common/` | `tiny` static lib | C | shared window setup, drawing, 5x7 bitmap font, test macros |

## Commands

```sh
cmake -S . -B build                          # configure (needs Homebrew sdl3, sdl3_ttf)
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
- **Pixel coordinates:** the C apps lay out in renderer output pixels (`SDL_GetRenderOutputSize`, windows created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`) and scale UI by `tiny_pixel_density()`. `musicnotes` instead uses a fixed logical presentation.
- **`common/` holds only what is actually shared**; each app still owns its `main` and event loop. `tiny_text` draws in the renderer's *current draw color* (set the colour with `SDL_SetRenderDrawColor` first, as with rect fills). The font is a full printable-ASCII 5x7 table in `common/font5x7.c`. `0` and `O` are deliberately identical (unslashed).
- To add an app: create `<app>/CMakeLists.txt`, `add_subdirectory` it in the root `CMakeLists.txt`, link `tiny`, and add its launcher to the `links` target.
