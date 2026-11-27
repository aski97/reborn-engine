#include "rb_test.h"

#include <reborn/clock.h>

#include "platform/clock_internal.h"

/* ============================================================================
 * Conversions
 * ========================================================================== */

/* Expected values computed with arbitrary-precision integers: floor(ticks * 1e9 / frequency). */
RB_TEST(clock, ticks_to_ns_whole_seconds) {
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(0, 10000000), 0);
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(10000000, 10000000), RB_NS_PER_S);
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(1, 10000000), 100);
}

/* 1e15 ticks at 10 MHz (3 years of uptime): ticks * 1e9 alone would be 1e24, past 2^64. */
RB_TEST(clock, ticks_to_ns_past_naive_overflow) {
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(UINT64_C(1000000000000000), 10000000),
                      UINT64_C(100000000000000000));
}

/* Frequencies that do not divide 1e9: the ACPI timer old Windows used, a 24 MHz crystal. */
RB_TEST(clock, ticks_to_ns_rounds_down_at_odd_frequencies) {
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns((UINT64_C(1) << 50) + 7, 3579545),
                      UINT64_C(314537156773453329));
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(UINT64_C(1000000000012345), 24000000),
                      UINT64_C(41666666667181041));
    /* A third of a second is 333333333.33... ns: the fraction is dropped. */
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(1, 3), 333333333);
}

/* The documented limit: the remainder product (frequency - 1) * 1e9 still fits. */
RB_TEST(clock, ticks_to_ns_at_18_ghz) {
    RB_EXPECT_EQ_UINT(rb_time_ticks_to_ns(UINT64_MAX, UINT64_C(18000000000)),
                      UINT64_C(1024819115206086200));
}

RB_TEST(clock, to_seconds) {
    RB_EXPECT_NEAR(rb_time_to_seconds(0), 0.0, 0.0);
    RB_EXPECT_NEAR(rb_time_to_seconds(1500 * RB_NS_PER_MS), 1.5, 0.0);
    /* A million seconds and one nanosecond: the nanosecond survives the conversion. */
    f64 s = rb_time_to_seconds(1000000 * RB_NS_PER_S + 1);
    RB_EXPECT(s > 1000000.0);
    RB_EXPECT_NEAR(s, 1000000.000000001, 1e-9);
}

/* ============================================================================
 * Clock
 * ========================================================================== */

RB_TEST(clock, now_never_goes_back) {
    u64 previous = rb_time_now();
    for (int i = 0; i < 100000; ++i) {
        u64 now = rb_time_now();
        RB_REQUIRE(now >= previous);
        previous = now;
    }
}

/*
 * The clock ticks well below a millisecond. A coarse clock (GetTickCount:
 * 15.6 ms) would make the first change a jump of milliseconds.
 */
RB_TEST(clock, resolution_is_below_a_millisecond) {
    u64 start = rb_time_now();
    u64 now = start;
    for (u32 i = 0; i < 100000000 && now == start; ++i) {
        now = rb_time_now();
    }
    RB_REQUIRE(now > start);
    RB_EXPECT(now - start < RB_NS_PER_MS);
}

/* ============================================================================
 * Sleep
 * ========================================================================== */

RB_TEST(clock, sleep_zero_returns_at_once) {
    u64 start = rb_time_now();
    rb_time_sleep(0);
    /* Generous: only a preempted test thread gets anywhere near it. */
    RB_EXPECT(rb_time_now() - start < 50 * RB_NS_PER_MS);
}

/* The upper bound is loose: a loaded machine (CI, parallel ctest) may oversleep. */
RB_TEST(clock, sleep_waits_at_least_the_time_asked) {
    u64 start = rb_time_now();
    rb_time_sleep(10 * RB_NS_PER_MS);
    u64 elapsed = rb_time_now() - start;
    RB_EXPECT(elapsed >= 10 * RB_NS_PER_MS);
    RB_EXPECT(elapsed < RB_NS_PER_S);
}

/*
 * The reason for the high-resolution timer (ADR 0009, D3): a plain Win32
 * Sleep(1) lasts up to 15.6 ms. With every core busy (the parallel test run) a
 * thread woken on time still waits for a core, at times for seconds in a row:
 * a fixed number of tries can all land in such a spell, so the tries stop at
 * the first short sleep or after 10 s.
 */
RB_TEST(clock, short_sleep_is_far_below_a_frame) {
    u64 give_up = rb_time_now() + 10 * RB_NS_PER_S;
    u64 shortest = UINT64_MAX;
    while (shortest >= 8 * RB_NS_PER_MS && rb_time_now() < give_up) {
        u64 start = rb_time_now();
        rb_time_sleep(RB_NS_PER_MS);
        u64 elapsed = rb_time_now() - start;
        RB_EXPECT(elapsed >= RB_NS_PER_MS);
        shortest = elapsed < shortest ? elapsed : shortest;
    }
    RB_EXPECT(shortest < 8 * RB_NS_PER_MS);
}
