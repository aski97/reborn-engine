/*
 * PCG32 (ADR 0019), as `pcg32_random_r` and `pcg32_srandom_r` in O'Neill's
 * reference implementation (pcg-random.org): the tests check its sequences.
 */
#include <reborn/assert.h>
#include <reborn/random.h>

/* The LCG's multiplier, from the reference. */
#define PCG_MULTIPLIER UINT64_C(6364136223846793005)

/*
 * The reference's default increment: odd, as an LCG modulo 2^64 needs to go
 * through every state. Another odd value would be another stream.
 */
#define PCG_INCREMENT UINT64_C(0xda3e39cb94b95bdb)

/*
 * Unsigned arithmetic wraps modulo 2^64 by definition: the LCG's "mod m" is
 * free. (Signed overflow would be undefined behavior.)
 */
static void rng_step(rb_rng *rng) {
    rng->state = rng->state * PCG_MULTIPLIER + PCG_INCREMENT;
}

rb_rng rb_rng_create(u64 seed) {
    /* Steps around the seed spread it, so close seeds start far apart. */
    rb_rng rng = {0};
    rng_step(&rng);
    rng.state += seed;
    rng_step(&rng);
    return rng;
}

/*
 * XSH RR: xorshift the high bits down, keep 32, rotate them by the top 5 bits.
 * The output comes from the state before the step, so the step's multiply can
 * run while the permutation does. The LCG's low bits are weak (the lowest
 * alternates); the permutation draws from the strong high ones.
 */
u32 rb_rng_u32(rb_rng *rng) {
    RB_ASSERT(rng != NULL);
    u64 old = rng->state;
    rng_step(rng);
    u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
    u32 rot = (u32)(old >> 59u);
    /* (32 - rot) & 31, written so rot == 0 never shifts by 32, which is undefined. */
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

/*
 * `r % range` alone favors the low values when 2^32 is not a multiple of
 * range. Rejecting the first 2^32 mod range values leaves a multiple of range,
 * each value hit equally often. At most half the draws are rejected (range
 * just above 2^31); for small ranges almost none.
 */
u32 rb_rng_range_u32(rb_rng *rng, u32 min, u32 max) {
    RB_ASSERT(rng != NULL);
    RB_ASSERT(min < max);
    u32 range = max - min;
    /* 2^32 mod range, in 32 bits: 0 - range is 2^32 - range. */
    u32 threshold = (0u - range) % range;
    for (;;) {
        u32 r = rb_rng_u32(rng);
        if (r >= threshold) {
            return min + r % range;
        }
    }
}

/*
 * The top 24 bits (an f32's precision) times 2^-24: exact, the largest is
 * 1 - 2^-24. Dividing all 32 bits by 2^32 would round up to 1.0.
 */
f32 rb_rng_f32(rb_rng *rng) {
    return (f32)(rb_rng_u32(rng) >> 8u) * (1.0f / 16777216.0f);
}

f32 rb_rng_range_f32(rb_rng *rng, f32 min, f32 max) {
    /* NaN fails it too. */
    RB_ASSERT(min <= max);
    /* max - min and the sum each round: the result can land on max, or one step past it. */
    f32 v = min + (max - min) * rb_rng_f32(rng);
    return v > max ? max : v;
}
