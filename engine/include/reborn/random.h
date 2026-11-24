/*
 * Random number generator (ADR 0019): PCG32, seeded explicitly.
 *
 *   rb_rng rng = rb_rng_create(seed);
 *   f32 x = rb_rng_range_f32(&rng, 0.0f, 16.0f);
 *
 * The same seed gives the same numbers on every run, platform and build: log
 * the seed and a run can be repeated. Keep one generator per purpose (gameplay,
 * cosmetic effects), so drawing numbers for one never shifts the other.
 * A generator is not thread-safe: one per thread.
 */
#ifndef RB_RANDOM_H
#define RB_RANDOM_H

#include <reborn/defines.h>

/*
 * Public so a game can keep it by value in its state; change it only through
 * these functions. One fixed PCG stream, so the state is all there is.
 */
typedef struct rb_rng {
    u64 state;
} rb_rng;

/* A generator for `seed`: any value, 0 included. */
RB_API rb_rng rb_rng_create(u64 seed);

/* 32 random bits, every value equally likely. */
RB_API u32 rb_rng_u32(rb_rng *rng);

/* In [min, max), every value equally likely; min < max. */
RB_API u32 rb_rng_range_u32(rb_rng *rng, u32 min, u32 max);

/* In [0, 1), multiples of 2^-24 equally likely. */
RB_API f32 rb_rng_f32(rb_rng *rng);

/* In [min, max]: rounding can reach max. min <= max; equal returns min. */
RB_API f32 rb_rng_range_f32(rb_rng *rng, f32 min, f32 max);

#endif /* RB_RANDOM_H */
