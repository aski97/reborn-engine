/*
 * Blink's rules (ADR 0014): pure, deterministic for a seed and a sequence of
 * actions and steps.
 */
#include "blink_rules.h"

#include <reborn/assert.h>

const blink_tuning blink_default_tuning = {
    .field = {16.0f, 9.0f},
    .square_side = 1.5f,
    .end_scale = 0.4f,
    .first_lifetime = 1.5f,
    .lifetime_factor = 0.95f,
    /* Reaction plus the mouse's travel across the field take about half a second. */
    .min_lifetime = 0.6f,
    /* What the frame takes to reach the screen: the input read before vsync, the queued images. */
    .hit_grace = 0.05f,
    .ready_time = 0.6f,
    .lives = 3,
    .restart_delay = 1.0f,
};

/* Anywhere the whole square fits inside the field. */
static void blink_spawn(blink_state *state) {
    f32 half = state->tuning.square_side * 0.5f;
    state->square.center.x = rb_rng_range_f32(&state->rng, half, state->tuning.field.x - half);
    state->square.center.y = rb_rng_range_f32(&state->rng, half, state->tuning.field.y - half);
    state->square.color = rb_rng_range_u32(&state->rng, 0, BLINK_COLOR_COUNT);
    state->square.lifetime = state->lifetime;
    state->square.remaining = state->lifetime;
}

/* The first square comes after READY: blink_advance spawns it. */
static void blink_new_game(blink_state *state) {
    state->phase = BLINK_PHASE_READY;
    state->score = 0;
    state->lives = state->tuning.lives;
    state->lifetime = state->tuning.first_lifetime;
    state->phase_time = 0.0f;
}

void blink_start(blink_state *state, const blink_tuning *tuning, u64 seed) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(tuning != NULL);
    /* A field too small for the square would make rb_rng_range_f32 assert on a reversed range. */
    RB_ASSERT(tuning->field.x >= tuning->square_side && tuning->field.y >= tuning->square_side);
    RB_ASSERT(tuning->end_scale >= 0.0f && tuning->end_scale <= 1.0f);
    RB_ASSERT(tuning->first_lifetime > 0.0f && tuning->min_lifetime > 0.0f);
    RB_ASSERT(tuning->hit_grace >= 0.0f && tuning->ready_time >= 0.0f);
    RB_ASSERT(tuning->lives > 0);
    *state =
        (blink_state){.tuning = *tuning, .rng = rb_rng_create(seed), .phase = BLINK_PHASE_TITLE};
}

/* ============================================================================
 * The square's geometry
 * ========================================================================== */

static rb_rect blink_square_rect_sized(const blink_state *state, f32 side) {
    rb_vec2 center = state->square.center;
    return (rb_rect){center.x - side * 0.5f, center.y - side * 0.5f, side, side};
}

/* Full side with all its time left, end_scale of it just before the end, nothing after. */
static rb_rect blink_square_rect_at(const blink_state *state, f32 remaining) {
    const blink_square *square = &state->square;
    f32                 side = 0.0f;
    if (remaining > 0.0f) {
        f32 left = remaining < square->lifetime ? remaining / square->lifetime : 1.0f;
        f32 end = state->tuning.end_scale;
        side = state->tuning.square_side * (end + (1.0f - end) * left);
    }
    return blink_square_rect_sized(state, side);
}

rb_rect blink_square_rect(const blink_state *state) {
    RB_ASSERT(state != NULL);
    return blink_square_rect_at(state, state->square.remaining);
}

/*
 * As the player saw it: hit_grace earlier. Capped at the full side, since a
 * square younger than the grace was not on screen yet: the previous one was.
 */
static rb_rect blink_square_rect_seen(const blink_state *state) {
    return blink_square_rect_at(state, state->square.remaining + state->tuning.hit_grace);
}

/* Edges included: a click on the border counts, as the eye expects. An empty one has none. */
static bool rect_contains(rb_rect rect, rb_vec2 point) {
    return rect.width > 0.0f && point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

/* How far `value` lies outside [lo, hi]: 0 within. */
static f32 outside_by(f32 value, f32 lo, f32 hi) {
    return value < lo ? lo - value : value > hi ? value - hi : 0.0f;
}

/* From the nearest point of the rectangle: 0 inside. */
static f32 rect_distance(rb_rect rect, rb_vec2 point) {
    rb_vec2 gap = {outside_by(point.x, rect.x, rect.x + rect.width),
                   outside_by(point.y, rect.y, rect.y + rect.height)};
    return rb_vec2_length(gap);
}

/* ============================================================================
 * Step
 * ========================================================================== */

/* BLINK_MAX_EVENTS covers every step blink_step accepts: running out means a bug here. */
static void blink_emit(blink_events *events, blink_event event) {
    RB_ASSERT(events->count < BLINK_MAX_EVENTS);
    events->items[events->count++] = event;
}

static void blink_apply(blink_state *state, const blink_action *action, blink_events *events) {
    if (state->phase == BLINK_PHASE_READY) {
        return;
    }
    if (state->phase == BLINK_PHASE_PLAYING) {
        if (action->type != BLINK_ACTION_CLICK) {
            return;
        }
        rb_rect seen = blink_square_rect_seen(state);
        if (!rect_contains(seen, action->point)) {
            blink_emit(events, (blink_event){.type = BLINK_EVENT_CLICK_OUTSIDE,
                                             .point = action->point,
                                             .square = seen,
                                             .distance = rect_distance(seen, action->point)});
            return;
        }
        blink_emit(events, (blink_event){
                               .type = BLINK_EVENT_HIT,
                               .point = action->point,
                               .square = seen,
                               .color = state->square.color,
                               .graced = !rect_contains(blink_square_rect(state), action->point)});
        ++state->score;
        f32 next = state->lifetime * state->tuning.lifetime_factor;
        state->lifetime = next > state->tuning.min_lifetime ? next : state->tuning.min_lifetime;
        blink_spawn(state);
        return;
    }
    /*
     * After a game over, the delay keeps the clicks a player was still firing
     * at the last square from starting the next game unseen; Space is
     * deliberate. On the title nothing was being clicked: any click starts.
     */
    bool start = action->type == BLINK_ACTION_START || state->phase == BLINK_PHASE_TITLE ||
                 state->phase_time >= state->tuning.restart_delay;
    if (start) {
        blink_new_game(state);
        blink_emit(events, (blink_event){.type = BLINK_EVENT_STARTED});
    }
}

static void blink_advance(blink_state *state, f32 dt, blink_events *events) {
    state->phase_time += dt;
    if (state->phase == BLINK_PHASE_READY) {
        /* At full time even if the step ran past ready_time: less than a step at 60 per second. */
        if (state->phase_time >= state->tuning.ready_time) {
            state->phase = BLINK_PHASE_PLAYING;
            state->phase_time = 0.0f;
            blink_spawn(state);
        }
        return;
    }
    if (state->phase != BLINK_PHASE_PLAYING) {
        return;
    }
    state->square.remaining -= dt;
    /* Gone from the rules' now, still on the player's screen: clickable for the grace. */
    if (state->square.remaining > -state->tuning.hit_grace) {
        return;
    }
    /* At the size it vanished at, its last on screen. */
    f32 last_side = state->tuning.square_side * state->tuning.end_scale;
    blink_emit(events, (blink_event){.type = BLINK_EVENT_EXPIRED,
                                     .square = blink_square_rect_sized(state, last_side),
                                     .color = state->square.color});
    --state->lives;
    if (state->lives > 0) {
        blink_spawn(state);
        return;
    }
    state->phase = BLINK_PHASE_GAME_OVER;
    state->phase_time = 0.0f;
    state->best = state->score > state->best ? state->score : state->best;
    blink_emit(events, (blink_event){.type = BLINK_EVENT_GAME_OVER});
}

void blink_step(blink_state *state, const blink_action *actions, u32 count, f32 dt,
                blink_events *events) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(actions != NULL || count == 0);
    RB_ASSERT(count <= BLINK_MAX_ACTIONS);
    RB_ASSERT(events != NULL);
    events->count = 0;
    for (u32 i = 0; i < count; ++i) {
        blink_apply(state, &actions[i], events);
    }
    blink_advance(state, dt, events);
}
