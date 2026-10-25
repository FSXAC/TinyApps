# Chess

Offline chess against a bot, with a chess.com-style game review afterwards. Part of `tinyapps`; build from the repo root:

```sh
cmake -S . -B build && cmake --build build --target chess
./build/bin/chess          # or the `chess` launcher
./build/bin/chess --demo   # bots play each other, a new random pairing every game
./build/bin/chess --timer 5   # a 5-minute clock for you (the bot has none); 0.5 = 30 seconds
```

Each game starts with a picker: choose the bot and its strength.

| Bot | Plays like |
|---|---|
| Tiny | a real engine, in Reduced, Normal and Advanced |
| Greedy | takes the biggest capture it sees, even if it loses the piece back |
| Mirror | copies your moves, reflected (1.e4 e5 2.Nf3 Nf6 …) until it can't |
| Queenie | brings her queen out early and checks with it whenever it's safe |

With `--timer`, your clock runs on your turn only; when it hits zero you lose on time (or draw, if the bot has only its king left). Takebacks don't give time back. In review, opening moves from the built-in book are graded **Book** and the opening is named.

The game, clock included, is saved as you play and when you quit, so the next launch picks up where you left off (or back in the review). `--demo` games aren't saved.

| Action | How |
|---|---|
| Move | click a piece then a square, or drag it |
| Mark a square / draw an arrow | right-click / right-drag (again to remove); left-click clears |
| Browse moves | `←` `→` `Home` `End`, the ‹ › buttons, the scroll wheel, or click a move |
| New game | `⌘N` opens the picker (`↑↓` bot, `←→` strength, `Return` plays); your color is random |
| Take back | `⌘Z` / `Backspace` |
| Flip board | `F` |
| Resign | Resign button |
| Game review (after the game) | Review button or `R` |
| Reveal the best move (review) | Show best button or `B` |
| Play out a line (review) | move any piece of the side to move; `←` `→` step through it, `Esc` or Back to game returns |
