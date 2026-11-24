/* Fixed-step accumulator and frame pacer (ADR 0011, C2, G1). */
#include "app/frame_timing.h"

#include <reborn/assert.h>

/* ============================================================================
 * Fixed-step accumulator
 * ========================================================================== */

void rb_step_clock_init(rb_step_clock *clock, u64 step_ns, u64 max_delta_ns, u64 now_ns) {
    RB_ASSERT(clock != NULL);
    RB_ASSERT(step_ns > 0);
    *clock = (rb_step_clock){
        .step_ns = step_ns,
        .max_delta_ns = max_delta_ns,
        .last_ns = now_ns,
        .accumulator_ns = step_ns,
    };
}

void rb_step_clock_advance(rb_step_clock *clock, u64 now_ns) {
    RB_ASSERT(clock != NULL);
    /* The clock is monotonic (ADR 0009): now_ns never goes below last_ns. */
    u64 delta = now_ns - clock->last_ns;
    clock->last_ns = now_ns;
    if (delta > clock->max_delta_ns) {
        delta = clock->max_delta_ns;
    }
    clock->accumulator_ns += delta;
}

void rb_step_clock_hold(rb_step_clock *clock, u64 now_ns) {
    RB_ASSERT(clock != NULL);
    clock->last_ns = now_ns;
}

bool rb_step_clock_consume(rb_step_clock *clock) {
    RB_ASSERT(clock != NULL);
    if (clock->accumulator_ns < clock->step_ns) {
        return false;
    }
    clock->accumulator_ns -= clock->step_ns;
    return true;
}

f64 rb_step_clock_alpha(const rb_step_clock *clock) {
    RB_ASSERT(clock != NULL);
    return (f64)clock->accumulator_ns / (f64)clock->step_ns;
}

u64 rb_step_clock_next_step_at(const rb_step_clock *clock) {
    RB_ASSERT(clock != NULL);
    if (clock->accumulator_ns >= clock->step_ns) {
        return clock->last_ns;
    }
    return clock->last_ns + (clock->step_ns - clock->accumulator_ns);
}

/* ============================================================================
 * Frame pacer
 * ========================================================================== */

void rb_frame_pacer_init(rb_frame_pacer *pacer, u64 period_ns, u64 now_ns) {
    RB_ASSERT(pacer != NULL);
    *pacer = (rb_frame_pacer){.period_ns = period_ns, .deadline_ns = now_ns + period_ns};
}

u64 rb_frame_pacer_wait(rb_frame_pacer *pacer, u64 now_ns) {
    RB_ASSERT(pacer != NULL);
    if (pacer->period_ns == 0) {
        return 0;
    }
    if (now_ns < pacer->deadline_ns) {
        u64 wait = pacer->deadline_ns - now_ns;
        pacer->deadline_ns += pacer->period_ns;
        return wait;
    }
    if (now_ns - pacer->deadline_ns >= pacer->period_ns) {
        pacer->deadline_ns = now_ns + pacer->period_ns;
    } else {
        pacer->deadline_ns += pacer->period_ns;
    }
    return 0;
}
