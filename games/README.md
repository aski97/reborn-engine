# Games

Our own small games, simplest first: each one decides which engine pieces come next (ADR 0014).
Each folder has a `README.md` with how the game plays and how to build and run it.

| Game | Folder | Since | Status | In short |
|---|---|---|---|---|
| Blink | [blink/](blink/README.md) | v0.2.0 | Playable | Click the square before it vanishes; it speeds up |

Planned, in order (see `TODO.md`): Orbit, Seedling, Hollow, Swarm, Gallery, Archipelago.

## Building all of them

Games are built with the engine (`REBORN_BUILD_GAMES`, on by default), from the repository root:

```sh
cmake --preset debug && cmake --build --preset debug
ctest --preset debug        # the engine's tests and every game's
```

Executables land in `build/<preset>/bin/`. Run them from the repository root: each writes its
`<game>.log.jsonl` in the current directory.

## Structure of a game

Every game is built in the same layers (ADR 0014), each depending only on those before it:

```
input (events → actions) ──► rules (state, fixed step, events out) ──► view (draws, reports)
                     <game>_game.c: the engine callbacks that connect them; main.c: the entry point
```

The rules and the input have no window: their tests in `<game>/tests/` run in `ctest`, on Linux
in Docker too.
