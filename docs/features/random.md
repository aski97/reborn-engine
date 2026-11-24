# Random numbers

| | |
|---|---|
| **Status** | In progress |
| **Since** | v0.2.0 (2026-11-24) |
| **Updated** | v0.2.0 (2026-11-24) |
| **Code** | `engine/include/reborn/random.h`, `engine/src/math/random.c` |
| **ADRs** | [0019](../adr/0019-random-number-generator.md) |

## Summary
Pseudo-random numbers from an explicit seed: the same seed gives the same sequence on every run,
platform and build, so a bug, a test or a replay can be reproduced from one logged number.

## Spec
- **Algorithm:** PCG32 (O'Neill's reference `pcg32_random_r`, default stream), 8 bytes of state.
  Any `u64` is a seed, 0 included; close seeds give unrelated sequences.
- **Integers:** `rb_rng_u32` (32 bits); `rb_rng_range_u32(rng, min, max)` in `[min, max)`,
  without modulo bias (biased draws are rejected and redrawn). `min >= max`: `RB_ASSERT`.
- **Floats:** `rb_rng_f32` in `[0, 1)`, exact multiples of 2^-24; `rb_rng_range_f32(rng, min,
  max)` in `[min, max]` (rounding can land on `max`). `min > max` or NaN: `RB_ASSERT`.
- **Determinism:** integer operations and exact conversions only. Changing the algorithm changes
  every sequence.
- **Threads:** a generator is plain state with no locking: one per thread.
- Not yet: other streams, other distributions (gaussian, weighted choice), shuffling, a seed from
  OS entropy.

## Design
- The state is a public struct, so a game keeps it by value in its state (an opaque type would
  need an allocation for 8 bytes); only the functions change it.
- No global generator: each purpose has its own (gameplay, cosmetic effects), so drawing for one
  never shifts the other's sequence and a replay holds.
- In a `.c` with `RB_API`, not header-only (ADR 0019, D2): the algorithm lives in one place and
  can grow (OS entropy needs platform code) without touching games.

## API
```c
#include <reborn/random.h>

/* Different every run, logged so the run can be repeated. */
u64 seed = rb_time_now();
RB_LOG_INFO("Seed: %llu", (unsigned long long)seed);
rb_rng gameplay = rb_rng_create(seed);

rb_vec2 spot = {rb_rng_range_f32(&gameplay, 1.0f, 15.0f), rb_rng_range_f32(&gameplay, 1.0f, 8.0f)};
u32 lane = rb_rng_range_u32(&gameplay, 0, 4); /* 0, 1, 2 or 3 */
```

## Decisions
- PCG32 (ADR 0019, A3), explicit state only (B1), the game picks and logs the seed (C1).
- `.c` + `RB_API` (D2); the engine-wide rule: header-only only for minimal, stateless functions
  used inside expressions in hot code.

## Future work
- Saving and restoring the state, with record and replay.
- A seed set from outside (command line, command system).
- More distributions when a game needs them.

## History
| Version | Date | Change |
|---|---|---|
| v0.2.0 | 2026-11-24 | First version: PCG32, integers and floats in a range |
