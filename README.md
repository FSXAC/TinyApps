# TinyPainter

A tiny SDL2 app for painting 32x32 black & white pixel art.

## Build & run

```sh
brew install sdl2   # or your platform's SDL2 dev package
make run
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
| Eraser        | ERASER  | `E` / `X`              |
| Select (rectangle) | SELECT | `M` / `R`; `Cmd/Ctrl+A` selects all |
| Undo          | UNDO    | `Cmd/Ctrl+Z`           |
| Clear canvas  | CLEAR   | `Cmd/Ctrl+N`, `Delete` (with no selection) |
| Save as bitmap| SAVE    | `Cmd/Ctrl+S`           |
| Toggle grid   |         | `G`                    |

- Left mouse paints black and right mouse paints white. The eraser swaps the two.
- Hold `Shift` to snap lines to 0/45/90 degrees and to draw perfect circles.
- Press `Esc` while dragging to cancel a shape.
- **Select:** drag to select a rectangle, then use the arrow keys (`Shift` = 4 px) or drag
  inside it to move the pixels; the spot they came from turns white. Moved pixels can
  go past the edge and come back. `Enter`, `Esc`, clicking outside the selection, or
  switching tools places them. `Delete` erases the selection. Undo reverts the whole move.
- Clearing doesn't ask for confirmation; use Undo if you didn't mean it.
- **Auto-save:** the canvas is saved after every edit and restored on launch
  (macOS: `~/Library/Application Support/TinyPainter/TinyPainter/canvas.bin`).
- **Save as bitmap** writes a 1-bit 32x32 BMP to `~/Pictures/TinyPainter-<timestamp>.bmp`.
  If that folder isn't available, it saves to your home folder instead.
- The window is resizable. The canvas scales to integer pixel sizes and the toolbar moves
  to the left or top, whichever leaves more room for the canvas.
