# 0009 — Monotonic clock and sleep

- **Status:** Accepted
- **Date:** 2026-11-16

## Context
The game loop measures frame durations and must not spin a CPU core. The logger
measures `t` on the wall clock (ADR 0008, H1), which jumps when the system time changes. Both need
a clock that never goes back, with sub-millisecond resolution, from the first line of `main()`
(the logger runs before any startup). There is no allocator and no threading yet. Done before the
rest of the game loop and before the allocators.

## Options
**A. Clock source**
1. `QueryPerformanceCounter` (Windows), `clock_gettime(CLOCK_MONOTONIC)` (Linux) — the
   high-resolution monotonic clocks of each OS; below a microsecond.
2. As A1 with `CLOCK_MONOTONIC_RAW` — not rate-adjusted by NTP; the adjustment is parts per
   million, irrelevant for a game.
3. Standard C only (`timespec_get`) — no platform code; in C17 it is the wall clock, the problem
   to solve. `clock()` measures CPU time, not elapsed time.

**B. Unit of the API**
1. Raw ticks plus a frequency query — exact; every caller converts, two concepts.
2. Nanoseconds in `u64` — one unit, exact integer differences, 584 years of range; converting
   QPC ticks needs care to avoid overflow.
3. Seconds in `f64` — convenient; inexact, errors accumulate, comparisons are delicate.

**C. State**
1. Stateless public function; Windows reads the frequency on every call — usable before any
   startup and from any thread; a few nanoseconds per call.
2. Frequency cached in a lazily set `static` — faster; a data race (undefined behavior) once
   threads exist, unless atomic.
3. Inside `rb_platform_state` — consistent with ADR 0006; the logger could not use it before
   platform startup.

**D. Sleep** (Windows `Sleep` wakes on the 15.6 ms system tick; Linux `clock_nanosleep` is precise)
1. Plain `Sleep` / `nanosleep` — `Sleep(1)` can last a whole 60 Hz frame.
2. `timeBeginPeriod(1)` plus `Sleep` — ~1 ms; changes the timer for the whole process, more
   power, links `winmm`.
3. High-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, Windows 10 1803) and
   `clock_nanosleep` — sub-millisecond, no global setting.
4. Short sleep then busy-wait to the exact instant — most precise; burns CPU.

**E. Origin**
1. The OS origin (usually boot) — only differences mean something.
2. Engine start — readable values; needs a stored start, i.e. state.

## Decision
**A1, B2, C1, D3, E1.**
- **API** (`reborn/clock.h`): `rb_time_now()` (ns), `rb_time_sleep(ns)`, `rb_time_to_seconds(ns)`,
  `RB_NS_PER_US/MS/S`.
- **B2 conversion:** `ticks * 1e9 / frequency` overflows `u64` after 30 minutes of uptime at
  10 MHz. Whole seconds and the remainder are converted apart; the result equals the exact
  rounded-down value, so the clock stays monotonic. Exact up to 18 GHz. `rb_time_to_seconds`
  splits the same way, so long durations keep their nanoseconds in a `f64`.
- **D3 contract:** "at least `ns`". The Windows timer is created per call (stateless, a few
  microseconds) and the wait repeats until the deadline has passed on `rb_time_now`, since timers
  may fire slightly early; if the timer cannot be created (before 1803) it falls back to `Sleep`.
  Linux sleeps to an absolute deadline, so a signal (`EINTR`) just waits again, without drift.
  Measured on the dev machine, 1 ms asked: median 1.6 ms on Windows (plain `Sleep(1)`: 15.7 ms),
  1.1 ms on Linux.
- **Logger:** `t` moves to `rb_time_now` (the switch ADR 0008, H1 planned). The anchor line and
  `ts` stay on the wall clock, read at the same instant as `t = 0`; if the system time changes
  while running, `ts` follows it and `t` keeps measuring real durations.
- **Files:** portable conversions in `platform/clock.c` (`clock_internal.h` exposes the tick
  conversion to tests), backends in `platform/win32/win32_clock.c` and
  `platform/linux/linux_clock.c`.
- The sandbox sleeps 1 ms per iteration until the game loop paces frames (CPU: 100% → 3% of a core).

## Consequences
- Frame timing, profiling and timeouts have one clock and one unit.
- A sleep on Windows costs a kernel object per call; if the game loop sleeps often enough for it
  to show in the profiler, the timer can move into a per-thread or per-loop state.
- Precise frame pacing beyond ~0.5 ms still needs D4 in the game loop.
- Suspend: whether the clock counts time asleep differs per OS; the game loop must clamp large
  deltas anyway.
