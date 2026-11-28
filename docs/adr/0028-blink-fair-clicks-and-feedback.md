# 0028 — Blink: fair clicks and feedback

- **Status:** Proposed
- **Date:** 2026-11-28

## Context
Playing Blink felt unresponsive: "a click on the square does not always work". The engine loses no
click (no dropped events, no double-click messages, the press is used and mapped through the same
pixel field the renderer draws into). The log of a real session shows what happens instead:
- A player reaches a square 0.6–0.8 s after it appears. With lifetimes under 1 s the square was by
  then 25–40% of its side, once 8% (13 pixels): the timer and the target are the same number, so
  the target is smallest exactly when the mouse gets there.
- The frame on screen is older than the rules' now: the loop reads the input, then waits about
  16 ms for vsync before drawing, and the frame queues behind others (FIFO, 3 images), about
  35–65 ms in all. Meanwhile the square keeps shrinking, so the one in the rules is a few pixels
  smaller per edge than the one seen: a click on the visible edge misses.
- Nothing answers a click: a hit only moves the square, a click outside shows nothing. A player
  cannot tell a click that was not read from one that missed by three pixels.
- The first square appears the instant the game starts.
- The minimum lifetime, 0.4 s, is less than reaction plus the mouse's travel to a random spot.

The step's events are bits (`HIT`, `EXPIRED`...), without where they happened: the view cannot
place an effect, the log cannot say how far a click missed.

## Options
**A. Events with data**
1. A list of records `{type, point, square, color, distance, graced}`, filled by the step in order,
   like the actions coming in — two clicks outside in one step stay two events; a sound later can
   use the position. The tests change shape.
2. The bits, plus one detail per type — a small change; one event per type per step.
3. The view compares the state before and after the step — no change to the rules, but a click
   outside leaves no trace in the state.

**B. A forgiving hit test**
1. Latency compensation: the click is tested against the square as it was `hit_grace` seconds
   earlier, what the player saw; a square whose time is up stays clickable that long — fixes the
   edge exactly; pure, in the rules.
2. A margin around the square — simpler; helps on small squares too, but accepts clicks visibly
   outside.
3. Both.

**C. The timer apart from the target**
1. The square shrinks to `end_scale` of its side, then vanishes; the shortest lifetime goes to
   0.6 s — the game looks the same; one tuning field.
2. A fixed square with an outline draining around it — clearest, but a different look and a
   partial outline to draw.

**D. A moment to get ready**
1. READY over an empty field for 0.6 s at every new game, then the first square.
2. A 3-2-1 countdown — clearer, 1.5 s at every restart.
3. The first square at once, its time starting 0.5 s later.

## Decision
**A1, B1, C1, D1.**
- **Events:** `blink_step(state, actions, count, dt, &events)` fills `blink_events` (`items`,
  `count`) in the order things happened, the actions first. `BLINK_MAX_ACTIONS` (16) moves from the
  glue into the rules, and `BLINK_MAX_EVENTS` is one per action plus a lost square and the game
  over, so a step's events always fit. Types: `STARTED`, `HIT` (the click, the square as seen, its
  color, `graced` when only the grace made it a hit), `CLICK_OUTSIDE` (the click, the square as
  seen, the distance from it), `EXPIRED` (the square at its last size, its color), `GAME_OVER`.
- **Tuning:** `end_scale` 0.4, `min_lifetime` 0.6 s, `hit_grace` 0.05 s (the measured wait plus
  the queue, rounded), `ready_time` 0.6 s.
- **Phases:** `READY` between the start and the first square: clicks and starts do nothing, the HUD
  shows the score, the lives and READY. `blink.state` reports it as `ready`.
- **Effects:** the view keeps up to 32 (`blink_view`), started by the events and aged by each
  step's `dt`, so a paused game holds them and a replay repeats them; when full, the one nearest its
  end gives way. A hit: a white flash and the square's outline growing to 1.8 times (0.3 s). A click
  outside: a small white outline where it landed (0.25 s). A lost square: a red flash where it
  vanished (0.5 s). Drawn in world units under the square, as outlines of four quads.
- **Log:** a click outside at debug level with its distance and the square's side; a hit inside
  the grace at debug level. The info lines stay as they were.
- **Tests:** `games/blink/tests/` (rules: READY, the grace, the end size, the distance, the order
  and the most a step can give; view: the effects).

## Consequences
- A click lands on what the player sees, and the target is never smaller than 40% of its side.
- Every click and every lost square shows: a miss is told apart from a click not read.
- The grace is a constant for today's latency. When the engine reduces it (its own ADR, planned
  with these changes), the grace should shrink with it; a latency the engine reports could feed it.
- The same seed places the same squares as before (the random draws are unchanged), but the
  timing differs: a session from v0.2.0 does not replay the same.
- The view now has state of its own; the glue owns it and clears it on `blink.restart`.
