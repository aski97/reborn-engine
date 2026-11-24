# Blink

A colored square appears at a random spot and shrinks as its time runs out: click it before it
vanishes. Every hit makes the next square shorter-lived; every square that vanishes costs a life.

## Build and play

From the repository root:

```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/bin/blink               # Windows: .\build\debug\bin\blink.exe
```

Use `release` instead of `debug` for a build without validation layers and asserts. Only Blink:
`cmake --build --preset debug --target blink`.

The log goes to the console and to `blink.log.jsonl` in the current directory.

## How to play

| Input | Action |
|---|---|
| Left click on the square | One point; the next square appears at once |
| Space | New game, once the current one is over |
| Left click, game over | New game, one second after the game over |
| Closing the window | Quit |

- The square's size is its timer: it shrinks to nothing when its time is up.
- **Speed-up:** the first square lasts 1.5 s; each hit takes 5% off, down to 0.4 s. A miss does not
  change it.
- **Lives:** 3, the white pips in the top left corner (a lost one turns gray). A click outside the
  square costs nothing.
- **Game over:** the field turns dark red. The best score of the session is kept.
- **No text yet:** score and lives are logged at every change (`Hit: score 4, next square lasts
  1.22 s`, `Missed: 1 life left`, `Game over: score 4, best 4`); the HUD comes with text rendering.

## Replaying a session

The startup log prints `Seed: <number>`. Positions and colors come from one random number generator
seeded with it (ADR 0019), restarts included: the same seed and the same clicks give the same
session. Setting the seed from outside comes with the command system.

## Code

| File | Layer (ADR 0014) |
|---|---|
| `src/blink_rules.c` | Rules: state, tuning (`blink_default_tuning`), `blink_start`, `blink_step(actions, dt)` → events, `blink_square_rect` |
| `src/blink_input.c` | Input: left press → click in world units, Space → restart |
| `src/blink_view.c` | View: field, square, lives; a log line per event |
| `src/blink_game.c` | Glue: the engine callbacks; actions gathered by `on_event`, used by the next `update` |
| `src/main.c` | Entry point |

The field is 16x9 world units through a 2D camera: the whole field is always visible, with bars
when the window's aspect differs.

## Tests

`tests/` checks the rules (hits, speed-up, shrinking target, lives, game over, restart, same seed
same game) and the input mapping, without a window:

```sh
ctest --preset debug -R blink
./build/debug/bin/blink_tests          # all of them in one process
```

On Linux the tests run in Docker with the engine's (`./scripts/linux-build.ps1 debug`); the game
itself needs a desktop (or Xvfb).
