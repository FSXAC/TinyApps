# tinyapps design language

The standard look for every app in this repo, inspired by the classic (System 6/7) Macintosh: black ink on white paper, 1-point lines, bitmap type, stepped rounded corners. Everything below is implemented in `common/tiny.{h,c}`. Use those helpers rather than raw colours or sizes, so the apps stay consistent.

## Unit

- **1 unit = 1 point**, the size of one classic Mac pixel. The apps draw in renderer pixels, so they use `u = tiny_unit(win, ren)` (2 on Retina). `musicnotes` draws in logical points, so its unit is 1.
- Every line, frame, gap, radius and text scale is a whole number of units. No hairlines, and no fractional or anti-aliased edges.
- Spacing: `TINY_GAP` (4u) between related controls, `TINY_PAD` (8u) inside panels and between groups, `TINY_MARGIN` (16u) from the window edge.

## Palette

| Token | Use |
|---|---|
| `TINY_INK` (black) | text, frames, "on" pixels, pressed/selected fills |
| `TINY_PAPER` (white) | window background, button and panel faces |
| `TINY_SHADE` (light gray) | hover fill, brief feedback flashes |
| `TINY_GRAY` (mid gray) | disabled/inactive, out-of-bounds areas |
| `TINY_DIM` (dark gray) | secondary text |

There is **no accent colour.** State is shown by inverting ink and paper, just as classic Mac showed selection. Content colours are exempt (a future app may still show user colours), but chrome never uses them. Translucent `TINY_GRAY` is fine for overlays such as grid lines.

## Type

- One font: the 5x7 bitmap in `font5x7.c`, covering ASCII plus `⌘ ← → ‹ › · • – — … ▾ ↩ ↓`, with 2-pixel descenders. Text is UTF-8. To add a symbol, append it to `tiny_font_extras`.
- Sizes are roles times the unit: **caption** `TINY_CAPTION` (1u, hints and help lines), **body** `TINY_BODY` (2u, labels and values), **title** `TINY_TITLE` (3u, primary content such as the note log). A single dominant readout (Tempo's BPM) may be a larger, fitted "display" size.
- **Bold** (`TINY_BOLD`, QuickDraw-style double strike) is for button labels, headings and emphasis (e.g. the C of each octave). Everything else is plain.
- Write text in sentence case ("Tap a key or click"). Button labels are single words where possible. Button labels can be ALL CAPS only when they sit under icons at caption size (TinyPainter's toolbar).
- Show shortcuts with the Mac symbols: `⌘C`, `Esc`, `Tab`.

## Shapes

- **Rounded:** buttons and panels (`TINY_RADIUS`, 4u), and keys and chips (`TINY_RADIUS_SMALL`, 2u). Corners step in whole units (`tiny_fill_round`) and can be rounded one by one (e.g. only the bottom corners of piano keys).
- **Square:** anything that shows pixels as content (canvases, pattern swatches, previews).
- **Frames** are 1u of ink (`tiny_frame_round`). **Panels** (`tiny_panel`) add a 1u drop shadow at the bottom right, like a classic window or dialog.
- **Patterns:** `tiny_fill_gray50` is the classic 50% dither, aligned to the window grid.

## Components and states

| Component | Normal | Hover | Pressed / selected |
|---|---|---|---|
| Button (`tiny_button`) | paper face, ink frame, ink label | shade face | inverted: ink face, paper label |
| Swatch / pixel tile | 1u ink frame | 2u ink frame | double frame (ink, paper gap, ink) |
| Key / toggle | its own colour | — | inverted |
| Text selection / pending item | — | — | inverted chip (`TINY_RADIUS_SMALL`) |
| Readout that's no longer current | ink | — | `TINY_GRAY` |

- Use `tiny_button`'s returned colour for the label or icon, so inversion is handled for you.
- A confirmation (e.g. "Copied") shows the control in its pressed state for about a second. A per-action flash (e.g. Tempo's tap) briefly fills with `TINY_SHADE`, for about 90 ms. Never flash the whole window between black and white.
- Marquees and outlines on pixel content alternate ink and paper, so they show up on both black and white.

## Behaviour

- `⌘Q` / `⌘W` quit. `Esc` cancels the current gesture, or resets or clears. `⌘Z` / `Backspace` undo. `Delete` clears. Key repeat never repeats an action unless the action is naturally continuous.
- The click that focuses a window counts (`SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH`).
- Redraw only on events or on a pending deadline, never continuously.

## Checking visuals

Run any app with `TINY_SCREENSHOT=/path/out.bmp`. `tiny_present` saves the first frame there and the app quits. This lets you review a style change without driving the UI.
