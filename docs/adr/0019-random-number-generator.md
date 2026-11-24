# 0019 — Random number generator

- **Status:** Accepted
- **Date:** 2026-11-24

## Context
Blink puts its square at a random spot. Games need randomness that can be **repeated**: the same
seed must give the same sequence on every run and every platform, so a bug, a test, a replay or an
agent's check can be reproduced from one number. A pseudo-random number generator (PRNG) does
this: a small state, advanced by a formula, starting from a seed.

The C library's `rand()` does not fit: its formula is implementation-defined (the Windows CRT and
glibc give different sequences for the same seed), `RAND_MAX` is 32767 on Windows, and its state
is one hidden global shared by every caller, so a cosmetic effect drawing numbers would move the
gameplay's sequence and break a replay. No external libraries.

## Options
**A. Algorithm**
1. 64-bit LCG (`state = a * state + c`) — one line; its low bits are weak (the lowest alternates).
2. xorshift32 — shifts and xors, 4 bytes; fails some statistical tests, state 0 is stuck.
3. PCG32 (O'Neill, 2014) — a 64-bit LCG whose output is permuted (xorshift, then a rotation chosen
   by the top bits), hiding the weak bits; good quality, ~10 lines, integer only, published
   reference values.
4. xoshiro128** (Blackman, Vigna, 2018) — as good and faster; needs a second generator to seed it.
5. Mersenne Twister — 2.5 KB of state, slow to seed, fails some tests.

**B. State**
1. Explicit `rb_rng` only: the caller owns it and passes it, one per purpose (gameplay, effects).
2. Explicit plus an engine-wide global for convenience — brings back the coupling of `rand()`.

**C. Seed when a game wants a different run each time**
1. The game picks it (e.g. from `rb_time_now()`) and logs it; a run is repeated with that number.
2. The engine reads OS entropy (`BCryptGenRandom`, `getrandom`) — platform code on two backends
   for a need a clock reading already covers.

**D. Where the code lives**
1. Header-only, `static inline`, like `reborn/math.h` — no call cost; the implementation and its
   helpers become public, every includer recompiles on a change, and with a DLL engine
   (`REBORN_SHARED`, ADR 0002) the inlined copy in the game would not follow the DLL.
2. `RB_API` functions in a `.c`, the struct in the header (games embed it by value, so its size
   must be known; an opaque type would need an allocation for 8 bytes) — one copy, private
   helpers stay `static`, growth (OS entropy, saving the state for replays) fits; a call costs a
   few nanoseconds, and link-time optimization can inline across files if a profiler ever asks.

## Decision
**A3, B1, C1, D2.**
- **PCG32** (the `pcg32_random_r` of the reference implementation): 64-bit state, multiplier
  6364136223846793005, output 32 bits by `XSH RR`. One fixed stream: the reference's default
  increment `0xda3e39cb94b95bdb`, so `rb_rng` is only the 8-byte state. Independent sequences come
  from separate generators with their own seeds; streams can be exposed later without changing
  existing sequences.
- **Seeding** as the reference's `pcg32_srandom_r`: state 0, one step, add the seed, one step.
  Every `u64` is a valid seed, 0 included.
- **API** (`reborn/random.h`): `rb_rng_create(seed)` returns the generator by value;
  `rb_rng_u32`; `rb_rng_range_u32(rng, min, max)` in `[min, max)` without modulo bias (draws below
  `2^32 mod range` are rejected); `rb_rng_f32` in `[0, 1)`, exact (the top 24 bits times 2^-24);
  `rb_rng_range_f32(rng, min, max)` in `[min, max]` (rounding can reach `max`). `min < max` for
  integers and `min <= max` for floats are asserted: a reversed range is a bug in the caller.
- **Determinism:** integer operations and an exact float conversion, so a seed gives the same
  numbers on every platform and build. Changing the algorithm changes every sequence: saved seeds
  and replays would no longer match.
- **Not thread-safe per generator:** one generator per thread. Stateless otherwise.
- **Code:** `engine/src/math/random.c`. A general rule for the engine: **header-only only for
  minimal functions without state or dependencies, used inside expressions in hot code**; the
  rest goes in a `.c` with `RB_API`, and moves inline only when a profiler justifies it.

## Consequences
- Tests check the exact sequence for fixed seeds, computed with the reference algorithm.
- Games keep one generator per purpose in their state; a cosmetic effect never shifts gameplay.
- Record and replay needs only the seed and the inputs (backlog).
- Blink logs its seed at startup; setting it from outside comes with the command system.
