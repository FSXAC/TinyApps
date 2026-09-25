# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A small, utilitarian on-screen piano for going from a sound you hear to written note names. It is a tool; favor function over polish. Keep it pragmatic and fast (event-driven redraw, low-latency audio). Features: a clickable 2-octave keyboard (defaults to C3–B4, octave shift via buttons or `Z`/`X`), a computer-keyboard mapping (Tab row = white keys, number row = black keys; see `src/keymap.h`), and a log of played notes/chords (`C4 D4 Am7 C/E ...`) with undo/clear/copy-to-clipboard.

## Commands

```sh
cmake -S . -B build                         # configure (needs Homebrew sdl3, sdl3_ttf)
cmake --build build -j                      # build app + tests
./build/musicnotes                          # run the app
ln -sfn "$PWD/build/musicnotes" ~/.local/bin/nts  # `nts` launcher (already set up; points at build/)
ctest --test-dir build --output-on-failure  # run tests
./build/musiccore_tests                     # run tests directly (all checks print on failure)
```

There is a single test executable (`tests/core_tests.cpp`) with a tiny `CHECK`/`CHECK_EQ` harness and no framework; add test functions there and call them from `main`.

## Architecture

- **SDL3, not SDL2.** Although sdl2_mixer is installed, the app targets SDL3 directly. Tones are synthesized in an SDL3 audio stream callback (no mixer or samples); `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES=256` keeps latency low.
- **Text** uses SDL3_ttf with macOS system fonts (SF / SF Mono, falling back to Helvetica / Menlo) via `TextCache` (`src/text.*`): one texture per (string, face, size, color), rasterized at the output pixel scale so it's sharp on Retina. The renderer uses a fixed 1040x450 logical presentation; all layout constants in `main.cpp` are logical pixels, and mouse events go through `SDL_ConvertEventToRenderCoordinates`. Scale changes (resize / display change) flush the cache.
- **Rendering is event-driven**: `App::run` blocks in `SDL_WaitEventTimeout` and redraws only after events (or when the "Copied" flash expires). Anything time-animated must set a timeout there or it won't redraw.
- Rounded shapes use `App::fillRound` (triangle fan via `SDL_RenderGeometry`) with per-corner radii.
- **`musiccore` library is SDL-free** (`theory`, `note_log`, `synth`) so it's unit-testable. Only `main.cpp`, `keymap.cpp` and `text.cpp` touch SDL.
- **Note grouping (`NoteLog`)**: notes whose holds overlap in time form one group; the group becomes a single log entry only when *all* keys are released. While held, it's shown as a highlighted pending entry. Label rules live in `theory::groupLabel`: 1 note → `C4`; recognized chord → `Am7`; else `C4+E4`.
- **Chord detection (`theory::chordName`)**: pitch-class bitmask matched against interval templates for every candidate root. Root == bass always wins (so `C E G A` with C in bass is `C6`, with A in bass `Am7`); otherwise the earliest template in `kChords` wins and a slash bass is appended. Template order is therefore meaningful.
- **Threading**: `Synth` is not thread-safe. The audio callback runs with the SDL audio stream lock held; the main thread wraps `noteOn`/`noteOff` in `SDL_LockAudioStream`/`SDL_UnlockAudioStream` (see `App::press`/`App::release`).
- **Input sources share one path**: mouse and keyboard both call `App::press`/`App::release`, which ref-count per MIDI note (`holdCount_`) so the same note held by both doesn't double-trigger. Each computer key remembers the MIDI note it started (`keyNotes_`), so changing octave mid-hold releases the correct note.
- **Keymap** uses scancodes (physical positions), so it's layout-independent. Offsets 0–23 map from the current octave's C; 22 (A#) is unmapped because it would land on Backspace (undo), and `` ` `` has no note (no black key between B and C). Keys show note names prominently and the key hint dimmed above it.
