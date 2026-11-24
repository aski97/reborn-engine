/*
 * Blink's rules (ADR 0014): actions in, state and events out, no window. Time
 * is fed in whole steps chosen by the test, so every case is exact.
 */
#include "rb_test.h"

#include "blink_rules.h"

#define EXPECT_F32_NEAR(a, b) RB_EXPECT_NEAR((f64)(a), (f64)(b), 1e-5)

static blink_action click_at(rb_vec2 point) {
    return (blink_action){.type = BLINK_ACTION_CLICK, .point = point};
}

/* A session past its title: the first game under way. */
static void begin(blink_state *state, const blink_tuning *tuning, u64 seed) {
    blink_start(state, tuning, seed);
    blink_action start = {.type = BLINK_ACTION_START};
    blink_step(state, &start, 1, 0.0f);
}

/* A click on the current square's center: always a hit while playing. */
static blink_events hit(blink_state *state) {
    blink_action action = click_at(state->square.center);
    return blink_step(state, &action, 1, 0.0f);
}

static blink_events wait(blink_state *state, f32 seconds) {
    return blink_step(state, NULL, 0, seconds);
}

static bool square_inside_field(const blink_state *state) {
    rb_rect r = blink_square_rect(state);
    return r.x >= 0.0f && r.y >= 0.0f && r.x + r.width <= state->tuning.field.x &&
           r.y + r.height <= state->tuning.field.y;
}

/* ============================================================================
 * Title
 * ========================================================================== */

RB_TEST(blink_rules, a_session_starts_on_the_title) {
    blink_state state;
    blink_start(&state, &blink_default_tuning, 1);
    RB_EXPECT(state.phase == BLINK_PHASE_TITLE);
    RB_EXPECT_EQ_UINT(state.best, 0);
    /* Time passes without a game starting: the view blinks with it. */
    RB_EXPECT_EQ_UINT(wait(&state, 10.0f), 0);
    RB_EXPECT(state.phase == BLINK_PHASE_TITLE);
    EXPECT_F32_NEAR(state.phase_time, 10.0f);
}

/* No delay on the title: nothing was being clicked before it. */
RB_TEST(blink_rules, a_click_or_start_leaves_the_title) {
    blink_state  state;
    blink_action click = click_at((rb_vec2){1.0f, 1.0f});
    blink_start(&state, &blink_default_tuning, 1);
    RB_EXPECT_EQ_UINT(blink_step(&state, &click, 1, 0.0f), BLINK_EVENT_STARTED);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);

    blink_action start = {.type = BLINK_ACTION_START};
    blink_start(&state, &blink_default_tuning, 1);
    RB_EXPECT_EQ_UINT(blink_step(&state, &start, 1, 0.0f), BLINK_EVENT_STARTED);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);
}

/* ============================================================================
 * Start
 * ========================================================================== */

RB_TEST(blink_rules, start_places_the_first_square) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);
    RB_EXPECT_EQ_UINT(state.score, 0);
    RB_EXPECT_EQ_UINT(state.lives, 3);
    EXPECT_F32_NEAR(state.square.lifetime, 1.5f);
    EXPECT_F32_NEAR(state.square.remaining, 1.5f);
    RB_EXPECT(state.square.color < BLINK_COLOR_COUNT);
    RB_EXPECT(square_inside_field(&state));
}

/* The same seed and the same actions give the same game: what makes a replay possible. */
RB_TEST(blink_rules, same_seed_same_squares) {
    blink_state a;
    blink_state b;
    begin(&a, &blink_default_tuning, 77);
    begin(&b, &blink_default_tuning, 77);
    for (u32 i = 0; i < 20; ++i) {
        RB_EXPECT(a.square.center.x == b.square.center.x);
        RB_EXPECT(a.square.center.y == b.square.center.y);
        RB_EXPECT_EQ_UINT(a.square.color, b.square.color);
        hit(&a);
        hit(&b);
    }
}

RB_TEST(blink_rules, squares_always_fit_the_field) {
    blink_state state;
    begin(&state, &blink_default_tuning, 3);
    bool inside = true;
    for (u32 i = 0; i < 1000; ++i) {
        inside = inside && square_inside_field(&state);
        hit(&state);
    }
    RB_EXPECT(inside);
}

/* ============================================================================
 * Clicks
 * ========================================================================== */

RB_TEST(blink_rules, a_hit_scores_and_speeds_up) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    rb_vec2 first = state.square.center;
    RB_EXPECT_EQ_UINT(hit(&state), BLINK_EVENT_HIT);
    RB_EXPECT_EQ_UINT(state.score, 1);
    EXPECT_F32_NEAR(state.lifetime, 1.5f * 0.95f);
    EXPECT_F32_NEAR(state.square.remaining, 1.5f * 0.95f);
    RB_EXPECT(state.square.center.x != first.x || state.square.center.y != first.y);
}

RB_TEST(blink_rules, a_click_elsewhere_does_nothing) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    rb_rect      square = blink_square_rect(&state);
    blink_action action = click_at((rb_vec2){square.x - 0.01f, square.y});
    RB_EXPECT_EQ_UINT(blink_step(&state, &action, 1, 0.0f), 0);
    RB_EXPECT_EQ_UINT(state.score, 0);
    RB_EXPECT_EQ_UINT(state.lives, 3);
}

/* What the player sees is what counts: half the time gone, half the side. */
RB_TEST(blink_rules, the_square_shrinks_and_so_does_its_target) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    rb_rect full = blink_square_rect(&state);
    wait(&state, 0.75f);
    rb_rect half = blink_square_rect(&state);
    EXPECT_F32_NEAR(half.width, 0.75f);
    EXPECT_F32_NEAR(half.x + half.width * 0.5f, full.x + full.width * 0.5f);
    /* The full square's corner is now outside. */
    blink_action action = click_at((rb_vec2){full.x + 0.1f, full.y + 0.1f});
    RB_EXPECT_EQ_UINT(blink_step(&state, &action, 1, 0.0f), 0);
}

RB_TEST(blink_rules, lifetime_stops_at_the_minimum) {
    blink_tuning tuning = blink_default_tuning;
    tuning.lifetime_factor = 0.5f;
    blink_state state;
    begin(&state, &tuning, 1);
    hit(&state); /* 0.75 */
    hit(&state); /* 0.375, raised to 0.4 */
    EXPECT_F32_NEAR(state.lifetime, 0.4f);
    hit(&state);
    EXPECT_F32_NEAR(state.lifetime, 0.4f);
}

/* ============================================================================
 * Time and lives
 * ========================================================================== */

RB_TEST(blink_rules, an_expired_square_costs_a_life) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    RB_EXPECT_EQ_UINT(wait(&state, 1.0f), 0);
    RB_EXPECT_EQ_UINT(wait(&state, 0.5f), BLINK_EVENT_EXPIRED);
    RB_EXPECT_EQ_UINT(state.lives, 2);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);
    /* The next square comes with the same lifetime: a miss does not speed up. */
    EXPECT_F32_NEAR(state.square.remaining, 1.5f);
}

RB_TEST(blink_rules, the_last_life_ends_the_game) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    hit(&state);
    hit(&state);
    wait(&state, 2.0f);
    wait(&state, 2.0f);
    RB_EXPECT_EQ_UINT(wait(&state, 2.0f), BLINK_EVENT_EXPIRED | BLINK_EVENT_GAME_OVER);
    RB_EXPECT(state.phase == BLINK_PHASE_GAME_OVER);
    RB_EXPECT_EQ_UINT(state.lives, 0);
    RB_EXPECT_EQ_UINT(state.score, 2);
    RB_EXPECT_EQ_UINT(state.best, 2);
    EXPECT_F32_NEAR(state.phase_time, 0.0f);
    /* Over: time passes without anything else happening. */
    RB_EXPECT_EQ_UINT(wait(&state, 10.0f), 0);
}

/* ============================================================================
 * Restart
 * ========================================================================== */

static void play_until_over(blink_state *state) {
    for (u32 i = 0; i < state->tuning.lives; ++i) {
        wait(state, 2.0f);
    }
}

RB_TEST(blink_rules, a_click_restarts_only_after_the_delay) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    play_until_over(&state);
    blink_action action = click_at((rb_vec2){1.0f, 1.0f});
    RB_EXPECT_EQ_UINT(blink_step(&state, &action, 1, 0.5f), 0);
    RB_EXPECT(state.phase == BLINK_PHASE_GAME_OVER);
    wait(&state, 0.5f);
    RB_EXPECT_EQ_UINT(blink_step(&state, &action, 1, 0.0f), BLINK_EVENT_STARTED);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);
}

RB_TEST(blink_rules, restart_starts_a_fresh_game_and_keeps_the_best) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    hit(&state);
    hit(&state);
    hit(&state);
    play_until_over(&state);
    blink_action restart = {.type = BLINK_ACTION_START};
    RB_EXPECT_EQ_UINT(blink_step(&state, &restart, 1, 0.0f), BLINK_EVENT_STARTED);
    RB_EXPECT_EQ_UINT(state.score, 0);
    RB_EXPECT_EQ_UINT(state.lives, 3);
    RB_EXPECT_EQ_UINT(state.best, 3);
    EXPECT_F32_NEAR(state.lifetime, 1.5f);
    RB_EXPECT(square_inside_field(&state));
}

RB_TEST(blink_rules, restart_is_ignored_while_playing) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    hit(&state);
    blink_action restart = {.type = BLINK_ACTION_START};
    RB_EXPECT_EQ_UINT(blink_step(&state, &restart, 1, 0.0f), 0);
    RB_EXPECT_EQ_UINT(state.score, 1);
}
