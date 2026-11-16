# 0011 — Application layer and game loop

- **Status:** Accepted
- **Date:** 2026-11-16

## Context
Until now the sandbox drives everything: it reserves the engine arena, starts the platform, opens
the window, pumps events and sleeps 1 ms per iteration. Every game would repeat that code. Roadmap
§4 asks for an application interface, a game loop with per-frame memory and a timestep strategy.
The engine must allocate the game's state (ADR 0010) and keep the platform state out of hidden
globals (ADR 0006). Later goals shape the choice: game code hot-reloaded as a DLL, an LLM
debug API that steps the simulation, and Vulkan with vsync (§7).

## Options
**A. Who owns the loop**
1. The engine. The game's `main` calls `rb_app_run(&callbacks)` and the engine calls the game
   back. Timing, frame memory and event handling live in one place, and the engine can wrap them
   later (hot-reload, debug API). The game controls the order of phases less.
2. The game, with the engine as a library (as SDL). Most freedom; every game re-implements
   timing and frame memory, and hot-reload gets harder.
3. The engine also owns `main()`, and the game defines only a creation function. A `main` inside a
   static library is a linker trap, and the test runner has its own `main`.

**B. Lifecycle callbacks**
1. `init`, `update`, `render`, `shutdown`, with the configuration as static data. The fewest
   callbacks, but the configuration cannot be computed (read from a file, from the command line).
2. A `configure` callback before any system starts, then `init` once systems run, the loop
   callbacks and `shutdown`. Each phase knows what exists, and configuration can become dynamic
   without an API change. One callback more.
3. One callback per event type (`on_resize`, `on_key`...) instead of a generic event callback.
   Readable for common cases, but there are many pointers to fill and a new pointer for every new
   event.

**C. Timestep**
1. Variable: one `update(dt)` per frame with the measured time. Simplest; physics depends on the
   frame rate and is not deterministic.
2. Fixed, with an accumulator. Each frame adds the real time to an accumulator and runs
   `update(step)` while it holds a full step; `render(alpha)` gets the leftover fraction to
   interpolate. Deterministic (replays, networking, stepping from a debug API). Needs a clamp on
   huge deltas and a bound on steps per frame.
3. Lockstep: one fixed update per frame. Deterministic and simple, but the game slows down when
   frames do.
4. Hybrid: a fixed update for gameplay plus a variable update for camera and UI. Flexible; two
   update callbacks to understand.

**D. Input edges with fixed steps** (`rb_key_was_pressed` covers the last pump; a frame may run
0, 1 or more steps)
1. Pump once per step: the platform is pumped, events are delivered and `update` runs, once for
   every step. Render does not pump. No platform API change, and input is tied to the simulation
   tick (a replay records it per step). Window events wait at most one step.
2. Pump once per frame, and stop clearing edges in the pump: the app clears them explicitly after
   each update. Changes ADR 0006 (U1) and the contract for anyone using `platform.h` directly.
3. Keep the pump per frame and document it: two steps in one frame see the same press, zero steps
   lose it.

**E. How callbacks reach the engine**
1. An opaque `rb_app *` passed to every callback, with accessors (`rb_app_platform`,
   `rb_app_frame_arena`, `rb_app_quit`...). Explicit state, as ADR 0006 requires.
2. A hidden global singleton (`rb_key_is_down(RB_KEY_W)` without a state). Convenient; contradicts
   ADR 0006 and rules out several instances in tests.
3. A context struct with public fields. No accessors, but the game can overwrite them.

**F. Memory for the game's state**
1. The game declares a size and alignment in its configuration. The engine allocates the state
   (zeroed) from the persistent arena and passes it to every callback. The state lives outside the
   game's code (hot-reload), and no game repeats the allocation.
2. The game allocates its state in `init` from an allocator it receives. The size can depend on
   data, but every game repeats the same code.

**G. Frame pacing until vsync exists**
1. Sleep to an absolute deadline that advances by one frame period. Sleep overshoot does not
   accumulate, so the average rate is the cap. Precision is that of `rb_time_sleep` (~0.5 ms).
2. Sleep, then busy-wait the last part (ADR 0009, D4). Precise; burns CPU.
3. No cap. Spins a core until vsync arrives (§7).

**H. What the game's `main` does** (every game would repeat the process setup)
1. Starts the logger, checks the version, calls `rb_app_run`, stops the logger. Explicit; about
   twenty lines repeated by every game.
2. `rb_app_run` does all of it and `main` is one line. The log configuration moves into
   `rb_app_config`. The version check must compare the headers the *game* was compiled with, which
   the library cannot know on its own: `rb_app_run` is a `static inline` function in the header,
   compiled into the game, passing its `RB_VERSION_NUMBER` to `rb_app_run_checked`.
3. The engine also provides `main` in a header (`reborn/entry.h`, compiled into the game, so no
   linker trap as in A3); the game defines only a function returning its callbacks. Ready for
   `WinMain` (a Windows build without a console); the entry point becomes invisible.

## Decision
**A1, B2, C2, D1, E1, F1, G1, H2** (H3 in the backlog, on top of H2).
- **API** (`reborn/app.h`): `int rb_app_run(const rb_app_callbacks *)` returns the exit code for
  `main` (0, or 1 when the versions differ, or configure, startup or `init` failed). Callbacks: `configure(config)`,
  `init(state, app)`, `on_event(state, app, event)`, `update(state, app, dt)`,
  `render(state, app, alpha)`, `shutdown(state, app)`. Only `update` is required. The state comes
  first, as `this` would in a method.
- **Configure** receives an `rb_app_config` already holding the defaults (console log at INFO and
  no file, window 1280×720, 60 updates/s, 60 fps cap, 256 MiB persistent and 64 MiB frame
  reservations) and changes what it
  needs; `false` aborts before any system starts. `RB_APP_STATE(config, T)` sets the state size
  and alignment from a type. Invalid values (zero update rate, alignment not a power of two) are
  logged and abort, since they may come from a file one day.
- **Startup order:** version check (first: with other headers even the structs' layout may
  differ) → configure → logger → arenas → platform (from the persistent arena) → main window →
  state → `init`. If `init` returns `false` the engine tears down without calling `shutdown`:
  `init` cleans up its own partial work. `shutdown` runs while every system is still alive.
- **Memory:** a persistent arena (platform state, game state, and whatever the game pushes for
  its lifetime) and a frame arena reset at the start of every frame, so data made in a step lives
  until that frame's render.
- **Loop:** a frame adds the elapsed time (clamped to 250 ms: suspend, debugger, Win32 modal
  drag) to the accumulator and runs one step per full step held; the clamp bounds the steps per
  frame (15 at 60 Hz), so a slow update slows the game instead of spiralling. A step pumps, hands
  every event to `on_event` and runs `update(dt)` with the fixed `dt`. The first frame runs one
  step at once. `render(alpha)` follows, with `alpha` = leftover / step in [0, 1). Accumulator
  arithmetic is in integer nanoseconds: no float drift.
- **Events:** the engine sees each event after `on_event`. A close request on the main window
  ends the loop, and for now the game cannot veto it. `rb_app_quit` ends the loop at the next check:
  after the current step's events, or after the current frame.
- **Minimized:** steps keep running; render is skipped, and the loop sleeps until the next step
  is due even when uncapped.
- **Pacing (G1):** a deadline per frame. A frame late by less than one period keeps the schedule;
  one later than that restarts it from now, so no burst of frames follows a hitch.
- **Logger (H2):** started after `configure`, so the game chooses its outputs; logs written
  before (in `configure`, or a version mismatch) go to stderr only. A log file that cannot be
  opened is a warning, not a failure. Stopped last, after "Exiting with code N".
- **Phase logging:** every phase transition is logged once, nothing per frame or step. INFO:
  every configuration field as received, right after the logger starts and before validation
  (four `Config:` lines), so a refused or misread value is visible next to its error; `App
  started`; the quit reason (main window closed, or requested by the game; only the first
  request); the run's totals. DEBUG: `Game init`, `Loop started`, `Game shutdown`. A quit from
  `init` skips the loop and its line.
- **Unchanged:** `platform.h` stays public: the game queries input and windows through
  `rb_app_platform` (ADR 0006 left this open). The Win32 modal loop stays in the backlog; the clamp
  absorbs the frozen time.
- **Files:** `engine/src/app/app.c` (run, accessors), `engine/src/app/frame_timing.c/.h` (pure
  accumulator and pacer, unit-tested without a window).

## Consequences
- A game is a set of callbacks plus a configure function. The engine owns order, timing and
  memory lifetimes, and can wrap the callbacks later (hot-reload, debug stepping).
- Simulation is deterministic per step. Rendering at a different rate than updates needs `alpha`
  interpolation to look smooth: without it motion stutters when frames and steps beat.
- Input edges are exact per step. Window events and input reach the game only on step
  boundaries, so the update rate bounds input latency (16.7 ms at 60 Hz).
- With vsync (§7) the cap becomes optional; the swapchain present paces frames.
- The engine decides when the game ends on close; a "save before quitting?" prompt needs a veto
  later.
