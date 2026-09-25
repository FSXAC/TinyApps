# TinyPainter

A tiny SDL3 app for painting 32x32 black & white pixel art.

## Build & run

Part of the `tinyapps` repo; from the repo root:

```sh
brew install sdl3
cmake -S . -B build && cmake --build build --target tinypainter
./build/bin/tinypainter   # or the `tpt` launcher
```

## Usage

The paintable area is 32x32. It sits inside a gray border so you can see it in context.
Shapes can start or extend into the border, but only the part inside the canvas is kept.

| Tool / action | Button  | Key                    |
|---------------|---------|------------------------|
| Pencil (default) | PENCIL  | `P` / `B`           |
| Line          | LINE    | `L`                    |
| Ellipse       | ELLIPSE | `O`                    |
| Fill          | FILL    | `F`                    |
| Previous / next fill pattern | swatches | `[` / `]`     |
| Eraser        | ERASER  | `E` / `X`              |
| Select (rectangle) | SELECT | `M` / `R`; `Cmd/Ctrl+A` selects all |
| Undo          | UNDO    | `Cmd/Ctrl+Z`           |
| Clear canvas  | CLEAR   | `Cmd/Ctrl+N`, `Delete` (with no selection) |
| Save as bitmap| SAVE    | `Cmd/Ctrl+S`           |
| Toggle grid   |         | `G`                    |

- Left mouse paints black and right mouse paints white. The eraser swaps the two.
- Hold `Shift` to snap lines to 0/45/90 degrees and to draw perfect circles.
- Press `Esc` while dragging to cancel a shape.
- **Fill patterns:** a MacPaint-style palette of 16 8x8 patterns sits under the canvas.
  The top half is a dithered gray ramp (100% to 0% black). The rest are textures: lines,
  diagonals, grid, bricks, crosshatch and dots. Left-click with Fill uses the selected
  pattern; right-click fills solid white. Patterns line up with the canvas grid, so
  neighboring fills match up. Picking a swatch also switches to the Fill tool, and the
  choice is remembered between launches.
- **Select:** drag to select a rectangle, then use the arrow keys (`Shift` = 4 px) or drag
  inside it to move the pixels; the spot they came from turns white. Moved pixels can
  go past the edge and come back. `Enter`, `Esc`, clicking outside the selection, or
  switching tools places them. `Delete` erases the selection. Undo reverts the whole move.
- Clearing doesn't ask for confirmation; use Undo if you didn't mean it.
- **Auto-save:** the canvas is saved after every edit and restored on launch
  (macOS: `~/Library/Application Support/TinyPainter/TinyPainter/canvas.bin`),
  along with the selected fill pattern.
- **Save as bitmap** writes a 1-bit 32x32 BMP to `~/Pictures/TinyPainter-<timestamp>.bmp`.
  If that folder isn't available, it saves to your home folder instead.
- A small preview at about twice actual size (2 points per pixel) sits at the end of the toolbar. It moves beside or below
  the canvas when the toolbar is short, and hides in very small windows.
- The window is resizable. The canvas scales to integer pixel sizes and the toolbar moves
  to the left or top, whichever leaves more room for the canvas.
