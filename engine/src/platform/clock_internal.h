/*
 * Portable helpers of the clock (ADR 0009), exposed for the backends and for
 * tests, which feed them values no real clock reaches in a test run.
 */
#ifndef RB_CLOCK_INTERNAL_H
#define RB_CLOCK_INTERNAL_H

#include <reborn/defines.h>

/*
 * Converts a counter reading to nanoseconds, rounding down. `frequency` is in
 * ticks per second and must not be 0. Exact for frequencies up to 18 GHz and
 * results up to 584 years: no intermediate product overflows.
 */
u64 rb_time_ticks_to_ns(u64 ticks, u64 frequency);

#endif /* RB_CLOCK_INTERNAL_H */
