/*
 * Blink's rules (ADR 0014): pure, deterministic for a seed and a sequence of
 * actions and steps.
 */
#include "blink_rules.h"

#include <reborn/assert.h>

const blink_tuning blink_default_tuning = {
    .field = {16.0f, 9.0f},
    .square_side = 1.5f,
    .first_lifetime = 1.5f,
    .lifetime_factor = 0.95f,
    .min_lifetime = 0.4f,
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

static void blink_new_game(blink_state *state) {
    state->phase = BLINK_PHASE_PLAYING;
    state->score = 0;
    state->lives = state->tuning.lives;
    state->lifetime = state->tuning.first_lifetime;
    state->over_time = 0.0f;
    blink_spawn(state);
}

void blink_start(blink_state *state, const blink_tuning *tuning, u64 seed) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(tuning != NULL);
    /* A field too small for the square would make rb_rng_range_f32 assert on a reversed range. */
    RB_ASSERT(tuning->field.x >= tuning->square_side && tuning->field.y >= tuning->square_side);
    RB_ASSERT(tuning->first_lifetime > 0.0f && tuning->min_lifetime > 0.0f);
    RB_ASSERT(tuning->lives > 0);
    *state = (blink_state){.tuning = *tuning, .rng = rb_rng_create(seed)};
    blink_new_game(state);
}

rb_rect blink_square_rect(const blink_state *state) {
    RB_ASSERT(state != NULL);
    const blink_square *square = &state->square;
    f32                 side = state->tuning.square_side * (square->remaining / square->lifetime);
    return (rb_rect){square->center.x - side * 0.5f, square->center.y - side * 0.5f, side, side};
}

/* Edges included: a click on the border counts, which is what the eye expects. */
static bool rect_contains(rb_rect rect, rb_vec2 point) {
    return point.x >= rect.x && point.x <= rect.x + rect.width && point.y >= rect.y &&
           point.y <= rect.y + rect.height;
}

static blink_events blink_apply(blink_state *state, const blink_action *action) {
    if (state->phase == BLINK_PHASE_PLAYING) {
        if (action->type == BLINK_ACTION_CLICK &&
            rect_contains(blink_square_rect(state), action->point)) {
            ++state->score;
            f32 next = state->lifetime * state->tuning.lifetime_factor;
            state->lifetime = next > state->tuning.min_lifetime ? next : state->tuning.min_lifetime;
            blink_spawn(state);
            return BLINK_EVENT_HIT;
        }
        return 0;
    }
    /*
     * The delay keeps the clicks a player was still firing at the last square
     * from starting the next game unseen; a restart is deliberate.
     */
    bool restart =
        action->type == BLINK_ACTION_RESTART ||
        (action->type == BLINK_ACTION_CLICK && state->over_time >= state->tuning.restart_delay);
    if (restart) {
        blink_new_game(state);
        return BLINK_EVENT_STARTED;
    }
    return 0;
}

static blink_events blink_advance(blink_state *state, f32 dt) {
    if (state->phase == BLINK_PHASE_GAME_OVER) {
        state->over_time += dt;
        return 0;
    }
    state->square.remaining -= dt;
    if (state->square.remaining > 0.0f) {
        return 0;
    }
    --state->lives;
    if (state->lives > 0) {
        blink_spawn(state);
        return BLINK_EVENT_EXPIRED;
    }
    state->phase = BLINK_PHASE_GAME_OVER;
    state->over_time = 0.0f;
    state->square.remaining = 0.0f;
    state->best = state->score > state->best ? state->score : state->best;
    return BLINK_EVENT_EXPIRED | BLINK_EVENT_GAME_OVER;
}

blink_events blink_step(blink_state *state, const blink_action *actions, u32 count, f32 dt) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(actions != NULL || count == 0);
    blink_events events = 0;
    for (u32 i = 0; i < count; ++i) {
        events |= blink_apply(state, &actions[i]);
    }
    return events | blink_advance(state, dt);
}
