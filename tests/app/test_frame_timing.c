/* Fixed-step accumulator and frame pacer (ADR 0011, C2, G1), on made-up times. */
#include "rb_test.h"

#include "app/frame_timing.h"

/* ============================================================================
 * Fixed-step accumulator
 * ========================================================================== */

RB_TEST(step_clock, first_frame_runs_one_step_at_once) {
    rb_step_clock clock;
    rb_step_clock_init(&clock, 10, 1000, 500);
    rb_step_clock_advance(&clock, 500);
    RB_EXPECT(rb_step_clock_consume(&clock));
    RB_EXPECT(!rb_step_clock_consume(&clock));
}

RB_TEST(step_clock, runs_whole_steps_and_keeps_the_leftover) {
    rb_step_clock clock;
    rb_step_clock_init(&clock, 10, 1000, 0);
    RB_REQUIRE(rb_step_clock_consume(&clock));

    rb_step_clock_advance(&clock, 25);
    RB_EXPECT(rb_step_clock_consume(&clock));
    RB_EXPECT(rb_step_clock_consume(&clock));
    RB_EXPECT(!rb_step_clock_consume(&clock));
    RB_EXPECT_NEAR(rb_step_clock_alpha(&clock), 0.5, 1e-12);

    /* The leftover carries over: 5 + 5 is one more step, exactly. */
    rb_step_clock_advance(&clock, 30);
    RB_EXPECT(rb_step_clock_consume(&clock));
    RB_EXPECT(!rb_step_clock_consume(&clock));
    RB_EXPECT_NEAR(rb_step_clock_alpha(&clock), 0.0, 1e-12);
}

RB_TEST(step_clock, a_frame_shorter_than_a_step_runs_none) {
    rb_step_clock clock;
    rb_step_clock_init(&clock, 10, 1000, 0);
    RB_REQUIRE(rb_step_clock_consume(&clock));
    rb_step_clock_advance(&clock, 4);
    RB_EXPECT(!rb_step_clock_consume(&clock));
    RB_EXPECT_NEAR(rb_step_clock_alpha(&clock), 0.4, 1e-12);
}

RB_TEST(step_clock, long_gaps_are_clamped) {
    rb_step_clock clock;
    rb_step_clock_init(&clock, 10, 100, 0);
    RB_REQUIRE(rb_step_clock_consume(&clock));

    rb_step_clock_advance(&clock, 1000000);
    u32 steps = 0;
    while (rb_step_clock_consume(&clock)) {
        ++steps;
    }
    RB_EXPECT_EQ_UINT(steps, 10);
}

RB_TEST(step_clock, next_step_is_due_when_the_leftover_fills_a_step) {
    rb_step_clock clock;
    rb_step_clock_init(&clock, 10, 1000, 0);
    RB_EXPECT_EQ_UINT(rb_step_clock_next_step_at(&clock), 0);

    RB_REQUIRE(rb_step_clock_consume(&clock));
    rb_step_clock_advance(&clock, 23);
    RB_REQUIRE(rb_step_clock_consume(&clock));
    RB_REQUIRE(rb_step_clock_consume(&clock));
    RB_EXPECT_EQ_UINT(rb_step_clock_next_step_at(&clock), 30);
}

/* ============================================================================
 * Frame pacer
 * ========================================================================== */

RB_TEST(frame_pacer, uncapped_never_waits) {
    rb_frame_pacer pacer;
    rb_frame_pacer_init(&pacer, 0, 100);
    RB_EXPECT_EQ_UINT(rb_frame_pacer_wait(&pacer, 100), 0);
    RB_EXPECT_EQ_UINT(rb_frame_pacer_wait(&pacer, 100), 0);
}

RB_TEST(frame_pacer, waits_until_the_deadline) {
    rb_frame_pacer pacer;
    rb_frame_pacer_init(&pacer, 10, 0);
    RB_EXPECT_EQ_UINT(rb_frame_pacer_wait(&pacer, 3), 7);
    RB_EXPECT_EQ_UINT(pacer.deadline_ns, 20);
}

/* The sleep woke 2 late: the next wait is 2 shorter, the schedule stays on multiples of 10. */
RB_TEST(frame_pacer, absorbs_sleep_overshoot) {
    rb_frame_pacer pacer;
    rb_frame_pacer_init(&pacer, 10, 0);
    RB_REQUIRE_EQ_UINT(rb_frame_pacer_wait(&pacer, 3), 7);
    RB_EXPECT_EQ_UINT(rb_frame_pacer_wait(&pacer, 15), 5);
    RB_EXPECT_EQ_UINT(pacer.deadline_ns, 30);
}

RB_TEST(frame_pacer, slightly_late_frame_keeps_the_schedule) {
    rb_frame_pacer pacer;
    rb_frame_pacer_init(&pacer, 10, 0);
    RB_EXPECT_EQ_UINT(rb_frame_pacer_wait(&pacer, 14), 0);
    RB_EXPECT_EQ_UINT(pacer.deadline_ns, 20);
}

RB_TEST(frame_pacer, very_late_frame_restarts_the_schedule) {
    rb_frame_pacer pacer;
    rb_frame_pacer_init(&pacer, 10, 0);
    RB_EXPECT_EQ_UINT(rb_frame_pacer_wait(&pacer, 55), 0);
    RB_EXPECT_EQ_UINT(pacer.deadline_ns, 65);
}

/* The pause (ADR 0022): paused time is dropped, the leftover and the alpha kept. */
RB_TEST(step_clock, hold_lets_time_pass_unsimulated) {
    rb_step_clock clock;
    rb_step_clock_init(&clock, 10, 1000, 0);
    RB_REQUIRE(rb_step_clock_consume(&clock));
    rb_step_clock_advance(&clock, 14);
    RB_REQUIRE(rb_step_clock_consume(&clock));
    RB_REQUIRE_NEAR(rb_step_clock_alpha(&clock), 0.4, 1e-12);

    rb_step_clock_hold(&clock, 500);
    RB_EXPECT(!rb_step_clock_consume(&clock));
    RB_EXPECT_NEAR(rb_step_clock_alpha(&clock), 0.4, 1e-12);

    /* Resumed: only the 6 after the hold count, 4 + 6 is one step. */
    rb_step_clock_advance(&clock, 506);
    RB_EXPECT(rb_step_clock_consume(&clock));
    RB_EXPECT(!rb_step_clock_consume(&clock));
}
