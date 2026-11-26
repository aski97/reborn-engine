# 0014 — Games and sandbox

- **Status:** Accepted
- **Date:** 2026-11-17

## Context
After v0.1.0 the roadmap follows a ladder of small games of our own (`TODO.md`, its introduction): building
them is what decides which engine pieces come next. The repository has one application today,
`sandbox/`. We need to decide where the games live and what the sandbox becomes.

## Options
**A. Where the games live**
1. `games/<name>/`, one folder per game next to `sandbox/` — each game is self-contained and reads
   like a real engine user; one more top-level folder.
2. The sandbox turns into the first game, later games next to it — no new folder; the sandbox stops
   being a neutral test bed.
3. A separate repository per game — the cleanest engine/game split; engine changes driven by a game
   span two repositories, too heavy while the engine changes every day.

**B. The sandbox**
1. Kept as the engine's test bed: it exercises every engine feature, games only what they need — one
   place to see and try everything; it must be kept up to date with each feature.
2. Removed once games exist — less to maintain; features no game uses yet have nowhere to run.

**C. Build** (decided with Blink)
1. A `REBORN_BUILD_GAMES` option (ON) and `games/CMakeLists.txt` adding every game; one target
   per game, its executable in `build/<preset>/bin/` next to the sandbox.
2. One option per game — finer control; options multiply with the games.

**D. Assets** (decided with Blink)
1. Deferred to the first game that has any: Blink draws only colored quads.
2. Decide the convention now (`games/<name>/assets/`, copied next to the executable) — a guess
   without a file to load.

**E. A game's code**
1. One file with the callbacks, like the sandbox — less code; the rules can only be checked by
   playing.
2. Layers, each depending only on those before it: **rules** (the simulation: state, fixed step,
   no window or clock; actions in, events out; balance numbers in a tuning struct), **input**
   (platform events to the game's actions), **view** (reads the state as `const`, draws it and
   reacts to events), **glue** (the engine callbacks), `main` — the rules and the input are
   testable without a window; a test, a replay or an agent feeds the same actions. Patterns:
   simulation/presentation split, command (actions), events out, data-driven tuning, a phase
   enum as state machine. An entity component system is left to the games with many objects.

**F. Tests and Linux**
1. Each game's rules and input in its own test executable, `games/<name>/tests/`, on the engine's
   framework (`reborn_add_test_executable` in `cmake/RebornTesting.cmake`), registered in CTest:
   they run in the Linux container too. On screen, the game runs on a Linux desktop like the
   sandbox.
2. No tests: checked by hand.

## Decision
- **A1:** each game lives in `games/<name>/` and uses only the public API (`<reborn/...>`), like the
  sandbox.
- **B1:** the sandbox stays and exercises everything the engine offers; a new engine feature is
  shown there as well, even when a game brought it.
- **C1:** `REBORN_BUILD_GAMES`, one target per game.
- **D1:** assets deferred to the first game that has any (likely Orbit's sounds); the backlog keeps
  it with that trigger.
- **E2:** every game is built in those layers, one file each: `<game>_rules`, `<game>_input`,
  `<game>_view`, `<game>_game`, `main.c`. The rules hold one random number generator for the whole
  session (ADR 0019): the seed logged at startup repeats every game played in it.
- **F1:** `<game>_tests` next to `reborn_tests` in `ctest`. A game is launched from the repository
  root (`./build/debug/bin/<game>`), writes `<game>.log.jsonl` there, and its `README.md` says how
  it plays and how to build and run it; `games/README.md` lists the games.

## Consequences
- Games show the engine from a user's point of view: friction in the public API shows up there.
- Every feature has two consumers to keep working: the sandbox and the game that needed it.
- Rules tested without a window: determinism (same seed, same game) is checked by `ctest`.
- A small game pays for five files; the input file is short, and it is where injected input and
  agent commands will arrive.
