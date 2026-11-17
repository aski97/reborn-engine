# 0014 — Games and sandbox

- **Status:** Proposed
- **Date:** 2026-11-17

## Context
After v0.1.0 the roadmap follows a ladder of small games of our own (`TODO.md`, *Games*): building
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

## Decision
- **A1:** each game lives in `games/<name>/` and uses only the public API (`<reborn/...>`), like the
  sandbox.
- **B1:** the sandbox stays and exercises everything the engine offers; a new engine feature is
  shown there as well, even when a game brought it.
- To decide with the first game (Blink): CMake wiring (one target per game, build option), where a
  game's assets live, how the games are launched and tested on Linux.

## Consequences
- Games show the engine from a user's point of view: friction in the public API shows up there.
- Every feature has two consumers to keep working: the sandbox and the game that needed it.
