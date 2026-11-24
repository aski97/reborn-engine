/*
 * Random number generator (ADR 0019). Expected values come from O'Neill's
 * reference algorithm (pcg32_srandom_r with the default increment, then
 * pcg32_random_r), run outside the engine; it reproduces the reference's own
 * published sequence for seed 42, stream 54.
 */
#include "rb_test.h"

#include <reborn/random.h>

/* ============================================================================
 * Sequence
 * ========================================================================== */

static void expect_sequence(u64 seed, const u32 expected[6]) {
    rb_rng rng = rb_rng_create(seed);
    for (u32 i = 0; i < 6; ++i) {
        RB_EXPECT_EQ_UINT(rb_rng_u32(&rng), expected[i]);
    }
}

RB_TEST(random, u32_matches_the_reference) {
    static const u32 seed_0[6] = {0xcecf5e73, 0x5e40274a, 0x604debce,
                                  0xcb667dd6, 0x1d215321, 0xb2389993};
    static const u32 seed_42[6] = {0xddaa6c75, 0x3237b41c, 0xe070ca56,
                                   0xc17a7979, 0xafd5d8ee, 0x58ad52b6};
    static const u32 seed_big[6] = {0x8dee7d1c, 0xd69bdeed, 0x07f61e67,
                                    0x036cad20, 0xaa9948da, 0x8da13595};
    expect_sequence(0, seed_0);
    expect_sequence(42, seed_42);
    expect_sequence(UINT64_C(0xdeadbeefcafef00d), seed_big);
}

/* The state is all there is: a copy goes on exactly where the original would. */
RB_TEST(random, copy_continues_the_same_sequence) {
    rb_rng a = rb_rng_create(1234);
    rb_rng_u32(&a);
    rb_rng b = a;
    for (u32 i = 0; i < 100; ++i) {
        RB_EXPECT_EQ_UINT(rb_rng_u32(&a), rb_rng_u32(&b));
    }
}

/* Neighboring seeds start far apart: no shared prefix. */
RB_TEST(random, close_seeds_give_different_sequences) {
    rb_rng a = rb_rng_create(100);
    rb_rng b = rb_rng_create(101);
    u32    same = 0;
    for (u32 i = 0; i < 100; ++i) {
        same += rb_rng_u32(&a) == rb_rng_u32(&b) ? 1u : 0u;
    }
    RB_EXPECT_EQ_UINT(same, 0);
}

/* ============================================================================
 * Integer range
 * ========================================================================== */

RB_TEST(random, range_u32_matches_the_reference) {
    static const u32 expected[8] = {11, 18, 14, 17, 12, 10, 12, 13};
    rb_rng           rng = rb_rng_create(42);
    for (u32 i = 0; i < 8; ++i) {
        RB_EXPECT_EQ_UINT(rb_rng_range_u32(&rng, 10, 20), expected[i]);
    }
}

/*
 * Range 3e9: draws below 2^32 mod 3e9 (1294967296) are rejected. For seed 7
 * the fifth raw draw, 0x2e8a2c0e, is one: the fifth value comes from the sixth.
 */
RB_TEST(random, range_u32_rejects_the_biased_draws) {
    static const u32 expected[5] = {1779599626, 1923228432, 2119517691, 909602099, 801790852};
    rb_rng           rng = rb_rng_create(7);
    for (u32 i = 0; i < 5; ++i) {
        RB_EXPECT_EQ_UINT(rb_rng_range_u32(&rng, 0, 3000000000u), expected[i]);
    }
}

RB_TEST(random, range_u32_of_one_value_returns_it) {
    rb_rng rng = rb_rng_create(5);
    RB_EXPECT_EQ_UINT(rb_rng_range_u32(&rng, 7, 8), 7);
    RB_EXPECT_EQ_UINT(rb_rng_range_u32(&rng, UINT32_MAX - 1, UINT32_MAX), UINT32_MAX - 1);
}

/*
 * Every bin is hit about equally: 16000 draws over 16 bins, 1000 expected,
 * the standard deviation about 31. A fixed seed makes it a fixed result, never
 * flaky; the margin only says the distribution is flat.
 */
RB_TEST(random, range_u32_is_flat) {
    u32    counts[16] = {0};
    rb_rng rng = rb_rng_create(1);
    for (u32 i = 0; i < 16000; ++i) {
        ++counts[rb_rng_range_u32(&rng, 0, 16)];
    }
    for (u32 i = 0; i < 16; ++i) {
        RB_EXPECT(counts[i] > 900 && counts[i] < 1100);
    }
}

/* ============================================================================
 * Float
 * ========================================================================== */

/* Exact: the top 24 bits over 2^24, so == is safe here. */
RB_TEST(random, f32_is_the_top_24_bits) {
    rb_rng rng = rb_rng_create(42);
    RB_EXPECT(rb_rng_f32(&rng) == (f32)(0xddaa6c75u >> 8) / 16777216.0f);
    RB_EXPECT(rb_rng_f32(&rng) == (f32)(0x3237b41cu >> 8) / 16777216.0f);
}

RB_TEST(random, f32_stays_below_one) {
    rb_rng rng = rb_rng_create(3);
    f32    sum = 0.0f;
    bool   inside = true;
    for (u32 i = 0; i < 10000; ++i) {
        f32 v = rb_rng_f32(&rng);
        inside = inside && v >= 0.0f && v < 1.0f;
        sum += v;
    }
    RB_EXPECT(inside);
    RB_EXPECT_NEAR((f64)(sum / 10000.0f), 0.5, 0.02);
}

RB_TEST(random, range_f32_stays_inside) {
    rb_rng rng = rb_rng_create(9);
    bool   inside = true;
    for (u32 i = 0; i < 10000; ++i) {
        f32 v = rb_rng_range_f32(&rng, -8.0f, 8.0f);
        inside = inside && v >= -8.0f && v <= 8.0f;
    }
    RB_EXPECT(inside);
}

RB_TEST(random, range_f32_of_an_empty_range_returns_min) {
    rb_rng rng = rb_rng_create(9);
    RB_EXPECT(rb_rng_range_f32(&rng, 2.5f, 2.5f) == 2.5f);
}
