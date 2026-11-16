/*
 * Game loop timing (ADR 0011, C2, G1): the fixed-step accumulator and the
 * frame pacer. Pure arithmetic on times passed in, so tests drive them with
 * made-up clocks instead of sleeping.
 */
#ifndef RB_APP_FRAME_TIMING_H
#define RB_APP_FRAME_TIMING_H

#include <reborn/defines.h>

/* ============================================================================
 * Fixed-step accumulator
 *
 *   rb_step_clock_advance(&clock, rb_time_now());
 *   while (rb_step_clock_consume(&clock)) { update(step); }
 *   render(rb_step_clock_alpha(&clock));
 * ========================================================================== */

typedef struct rb_step_clock {
    u64 step_ns;      /* one simulation step */
    u64 max_delta_ns; /* frame deltas above this are clamped */
    u64 last_ns;      /* time of the last advance */
    u64 accumulator_ns;
} rb_step_clock;

/* Starts with one full step accumulated, so the first frame updates at once. */
void rb_step_clock_init(rb_step_clock *clock, u64 step_ns, u64 max_delta_ns, u64 now_ns);

/* Adds the time since the last advance, at most `max_delta_ns`. */
void rb_step_clock_advance(rb_step_clock *clock, u64 now_ns);

/* Takes one step from the accumulator; false when less than a step is left. */
bool rb_step_clock_consume(rb_step_clock *clock);

/* Leftover as a fraction of a step, in [0, 1): how far render is past the last step. */
f64 rb_step_clock_alpha(const rb_step_clock *clock);

/* When the accumulator will hold a full step again, on the clock passed to advance. */
u64 rb_step_clock_next_step_at(const rb_step_clock *clock);

/* ============================================================================
 * Frame pacer
 *
 * Deadlines advance by one period from the previous deadline, not from when
 * the frame really ended: sleep overshoot is absorbed by the next wait, so the
 * average rate is the cap.
 * ========================================================================== */

typedef struct rb_frame_pacer {
    u64 period_ns; /* 0: uncapped */
    u64 deadline_ns;
} rb_frame_pacer;

void rb_frame_pacer_init(rb_frame_pacer *pacer, u64 period_ns, u64 now_ns);

/*
 * Nanoseconds to sleep before the next frame, then moves the deadline on. A
 * frame late by a period or more restarts the schedule from now: catching up
 * would run a burst of frames without sleeping.
 */
u64 rb_frame_pacer_wait(rb_frame_pacer *pacer, u64 now_ns);

#endif /* RB_APP_FRAME_TIMING_H */
