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
| Space or left click, title screen | First game |
| Left click on the square | One point; the next square appears at once |
| Space, game over | New game |
| Left click, game over | New game, one second after the game over |
| Closing the window | Quit |

- The square's size is its timer: it shrinks to nothing when its time is up.
- **Speed-up:** the first square lasts 1.5 s; each hit takes 5% off, down to 0.4 s. A miss does not
  change it.
- **Title screen:** "BLINK" made of squares, "PRESS SPACE" blinking. Shown once, at launch.
- **HUD:** the score in the top left corner, the lives as white pips in the top right one (a lost
  one fades). A click outside the square costs nothing.
- **Game over:** the field turns dark red; "GAME OVER", the score and the best score of the
  session in the middle, then "CLICK OR SPACE" once a click may restart.
- **Sharp at any window size:** the HUD is in window pixels, the builtin 8x8 font enlarged by a
  whole factor, one per 180 pixels of window height (x4 at 720p, x6 at 1080p): text never gets
  uneven pixels, and on common screens it keeps the same proportion (ADR 0021).
- **Log:** score and lives are logged at every change too (`Hit: score 4, next square lasts
  1.22 s`, `Missed: 1 life left`, `Game over: score 4, best 4`), for agents and replays.

## Replaying a session

The startup log prints `Seed: <number>`. Positions and colors come from one random number generator
seeded with it (ADR 0019), restarts included: the same seed and the same clicks give the same
session. `blink.restart <seed>` starts a session with a chosen one.

## Commands

Blink takes the engine's commands (`help`, `status`, `pause`, `resume`, `step`, `quit`, `wait`:
listed in the [main README](../../README.md#commands)) and two of its own, from a script or from
`reborn-cmd` (ADR 0022, [commands.md](../../docs/features/commands.md)):

| Command | Reply |
|---|---|
| `blink.state` | `phase` (`title`, `playing`, `game_over`), `score`, `lives`, `best`, `phase_time`; while playing, `square`: center `x`, `y` and `side` in world units, `remaining` seconds, and `pixels` (`x`, `y`, `width`, `height`, `center_x`, `center_y`) in window pixels |
| `blink.restart <seed>` | Back to the title, a new session from that seed |

```sh
./build/debug/bin/blink --listen 7777 &
./build/debug/bin/reborn-cmd 7777 "blink.restart 42" pause "blink.state"
```

Paused, the square waits: a click at `pixels.center_x`, `pixels.center_y` followed by `step 1`
scores (until input injection, the click comes from outside, e.g. `xdotool`).

## Code

| File | Layer (ADR 0014) |
|---|---|
| `src/blink_rules.c` | Rules: state, tuning (`blink_default_tuning`), `blink_start`, `blink_step(actions, dt)` → events, `blink_square_rect` |
| `src/blink_input.c` | Input: left press → click in world units, Space → start |
| `src/blink_view.c` | View: field and square in world units; title, HUD and game over in window pixels; a log line per event; the window icon |
| `src/blink_game.c` | Glue: the engine callbacks; actions gathered by `on_event`, used by the next `update`; the icon drawn in `configure`; the builtin font created in `init`, destroyed in `shutdown`; the `blink.*` commands |
| `src/main.c` | Entry point |

The field is 16x9 world units through a 2D camera: the whole field is always visible, with bars
when the window's aspect differs. The window icon (a dark rounded tile with an orange square) is
drawn in code at 16, 32 and 64 pixels and given to the window at creation (ADR 0020).

## Tests

`tests/` checks the rules (title, hits, speed-up, shrinking target, lives, game over, restart,
same seed same game), the input mapping, the HUD's scale and the icon, without a window:

```sh
ctest --preset debug -R blink
./build/debug/bin/blink_tests          # all of them in one process
```

On Linux the tests run in Docker with the engine's (`./scripts/linux-build.ps1 debug`); the game
itself needs a desktop (or Xvfb).
