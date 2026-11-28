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

static blink_events step(blink_state *state, const blink_action *actions, u32 count, f32 dt) {
    blink_events events;
    blink_step(state, actions, count, dt, &events);
    return events;
}

static blink_events wait(blink_state *state, f32 seconds) {
    return step(state, NULL, 0, seconds);
}

/* Exactly one event, of this type. */
static bool only(const blink_events *events, blink_event_type type) {
    return events->count == 1 && events->items[0].type == type;
}

/* A session past its title and READY: the first square on the field. */
static void begin(blink_state *state, const blink_tuning *tuning, u64 seed) {
    blink_start(state, tuning, seed);
    blink_action start = {.type = BLINK_ACTION_START};
    step(state, &start, 1, 0.0f);
    wait(state, tuning->ready_time);
}

/* A click on the current square's center: always a hit while playing. */
static blink_events hit(blink_state *state) {
    blink_action action = click_at(state->square.center);
    return step(state, &action, 1, 0.0f);
}

static bool square_inside_field(const blink_state *state) {
    rb_rect r = blink_square_rect(state);
    return r.x >= 0.0f && r.y >= 0.0f && r.x + r.width <= state->tuning.field.x &&
           r.y + r.height <= state->tuning.field.y;
}

/* The default numbers without the grace: what the screen shows is what counts. */
static blink_tuning no_grace(void) {
    blink_tuning tuning = blink_default_tuning;
    tuning.hit_grace = 0.0f;
    return tuning;
}

/* ============================================================================
 * Title and READY
 * ========================================================================== */

RB_TEST(blink_rules, a_session_starts_on_the_title) {
    blink_state state;
    blink_start(&state, &blink_default_tuning, 1);
    RB_EXPECT(state.phase == BLINK_PHASE_TITLE);
    RB_EXPECT_EQ_UINT(state.best, 0);
    /* Time passes without a game starting: the view blinks with it. */
    RB_EXPECT_EQ_UINT(wait(&state, 10.0f).count, 0);
    RB_EXPECT(state.phase == BLINK_PHASE_TITLE);
    EXPECT_F32_NEAR(state.phase_time, 10.0f);
}

/* No delay on the title: nothing was being clicked before it. */
RB_TEST(blink_rules, a_click_or_start_leaves_the_title) {
    blink_state  state;
    blink_action click = click_at((rb_vec2){1.0f, 1.0f});
    blink_start(&state, &blink_default_tuning, 1);
    blink_events events = step(&state, &click, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_STARTED));
    RB_EXPECT(state.phase == BLINK_PHASE_READY);

    blink_action start = {.type = BLINK_ACTION_START};
    blink_start(&state, &blink_default_tuning, 1);
    events = step(&state, &start, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_STARTED));
    RB_EXPECT(state.phase == BLINK_PHASE_READY);
}

RB_TEST(blink_rules, ready_comes_before_the_first_square) {
    blink_tuning tuning = blink_default_tuning;
    tuning.ready_time = 0.5f;
    blink_state  state;
    blink_action start = {.type = BLINK_ACTION_START};
    blink_start(&state, &tuning, 1);
    step(&state, &start, 1, 0.0f);
    /* Nothing to click yet: clicks and starts do nothing. */
    blink_action actions[] = {click_at((rb_vec2){8.0f, 4.5f}), start};
    RB_EXPECT_EQ_UINT(step(&state, actions, 2, 0.25f).count, 0);
    RB_EXPECT(state.phase == BLINK_PHASE_READY);
    RB_EXPECT_EQ_UINT(state.lives, 3);
    /* Then the first square, with all its time. */
    RB_EXPECT_EQ_UINT(wait(&state, 0.25f).count, 0);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);
    EXPECT_F32_NEAR(state.phase_time, 0.0f);
    EXPECT_F32_NEAR(state.square.remaining, 1.5f);
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
    rb_vec2      first = state.square.center;
    u32          color = state.square.color;
    rb_rect      square = blink_square_rect(&state);
    blink_events events = hit(&state);
    RB_EXPECT(only(&events, BLINK_EVENT_HIT));
    /* Where it happened, for the view: the click and the square as it was. */
    RB_EXPECT(events.items[0].point.x == first.x && events.items[0].point.y == first.y);
    EXPECT_F32_NEAR(events.items[0].square.x, square.x);
    EXPECT_F32_NEAR(events.items[0].square.width, square.width);
    RB_EXPECT_EQ_UINT(events.items[0].color, color);
    RB_EXPECT(!events.items[0].graced);
    RB_EXPECT_EQ_UINT(state.score, 1);
    EXPECT_F32_NEAR(state.lifetime, 1.5f * 0.95f);
    EXPECT_F32_NEAR(state.square.remaining, 1.5f * 0.95f);
    RB_EXPECT(state.square.center.x != first.x || state.square.center.y != first.y);
}

/* It costs nothing, but says how far it was: misses measured, not guessed. */
RB_TEST(blink_rules, a_click_outside_reports_its_distance) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    rb_rect square = blink_square_rect(&state);
    /* 0.3 left of the left edge and 0.4 above the top one: 0.5 from the corner. */
    blink_action action = click_at((rb_vec2){square.x - 0.3f, square.y - 0.4f});
    blink_events events = step(&state, &action, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_CLICK_OUTSIDE));
    RB_EXPECT_NEAR((f64)events.items[0].distance, 0.5, 1e-4);
    EXPECT_F32_NEAR(events.items[0].square.width, square.width);
    RB_EXPECT_EQ_UINT(state.score, 0);
    RB_EXPECT_EQ_UINT(state.lives, 3);
}

/* Half the time gone: halfway from the full side to end_scale of it, around the same center. */
RB_TEST(blink_rules, the_square_shrinks_to_its_end_size) {
    blink_tuning tuning = no_grace();
    blink_state  state;
    begin(&state, &tuning, 1);
    rb_rect full = blink_square_rect(&state);
    wait(&state, 0.75f);
    rb_rect half = blink_square_rect(&state);
    EXPECT_F32_NEAR(half.width, 1.5f * 0.7f);
    EXPECT_F32_NEAR(half.x + half.width * 0.5f, full.x + full.width * 0.5f);
    /* The full square's corner is now outside. */
    blink_action action = click_at((rb_vec2){full.x + 0.1f, full.y + 0.1f});
    blink_events events = step(&state, &action, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_CLICK_OUTSIDE));
    /* Just before the end it is still end_scale of the side: a target, not a dot. */
    wait(&state, 0.74f);
    EXPECT_F32_NEAR(blink_square_rect(&state).width, 1.5f * (0.4f + 0.6f * 0.01f / 1.5f));
}

/* The screen shows the rules as they were hit_grace ago: the click is tested against that. */
RB_TEST(blink_rules, a_click_is_tested_against_the_square_as_seen) {
    blink_tuning tuning = blink_default_tuning;
    tuning.hit_grace = 0.25f;
    blink_state state;
    begin(&state, &tuning, 1);
    wait(&state, 0.75f);
    /* Now 1.05 wide; 0.25 s ago, 1.2. A click 0.56 from the center is between the two. */
    rb_vec2      center = state.square.center;
    blink_action action = click_at((rb_vec2){center.x - 0.56f, center.y});
    blink_events events = step(&state, &action, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_HIT));
    RB_EXPECT(events.items[0].graced);
    EXPECT_F32_NEAR(events.items[0].square.width, 1.2f);
}

/* Its time is up in the rules, not yet on screen: still a hit, and only then a life lost. */
RB_TEST(blink_rules, a_square_stays_clickable_for_the_grace) {
    blink_tuning tuning = blink_default_tuning;
    tuning.hit_grace = 0.25f;
    blink_state state;
    begin(&state, &tuning, 1);
    RB_EXPECT_EQ_UINT(wait(&state, 1.5f).count, 0);
    RB_EXPECT_EQ_UINT(state.lives, 3);
    EXPECT_F32_NEAR(blink_square_rect(&state).width, 0.0f);
    blink_events events = hit(&state);
    RB_EXPECT(only(&events, BLINK_EVENT_HIT));
    RB_EXPECT(events.items[0].graced);

    begin(&state, &tuning, 1);
    wait(&state, 1.5f);
    blink_events expired = wait(&state, 0.25f);
    RB_EXPECT(only(&expired, BLINK_EVENT_EXPIRED));
    RB_EXPECT_EQ_UINT(state.lives, 2);
}

RB_TEST(blink_rules, lifetime_stops_at_the_minimum) {
    blink_tuning tuning = blink_default_tuning;
    tuning.lifetime_factor = 0.5f;
    blink_state state;
    begin(&state, &tuning, 1);
    hit(&state); /* 0.75 */
    hit(&state); /* 0.375, raised to 0.6 */
    EXPECT_F32_NEAR(state.lifetime, 0.6f);
    hit(&state);
    EXPECT_F32_NEAR(state.lifetime, 0.6f);
}

/* Each action gives its event, in order. */
RB_TEST(blink_rules, events_keep_the_order_of_the_actions) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    rb_rect      square = blink_square_rect(&state);
    blink_action actions[] = {click_at((rb_vec2){square.x - 1.0f, square.y}),
                              click_at(state.square.center)};
    blink_events events = step(&state, actions, 2, 0.0f);
    RB_EXPECT_EQ_UINT(events.count, 2);
    RB_EXPECT(events.items[0].type == BLINK_EVENT_CLICK_OUTSIDE);
    RB_EXPECT(events.items[1].type == BLINK_EVENT_HIT);
}

/* The most a step can give: every action missing, then the last life lost. */
RB_TEST(blink_rules, a_full_step_fits_its_events) {
    blink_tuning tuning = blink_default_tuning;
    tuning.lives = 1;
    blink_state state;
    begin(&state, &tuning, 1);
    rb_rect      square = blink_square_rect(&state);
    blink_action actions[BLINK_MAX_ACTIONS];
    for (u32 i = 0; i < BLINK_MAX_ACTIONS; ++i) {
        actions[i] = click_at((rb_vec2){square.x - 1.0f, square.y});
    }
    blink_events events = step(&state, actions, BLINK_MAX_ACTIONS, 2.0f);
    RB_EXPECT_EQ_UINT(events.count, BLINK_MAX_EVENTS);
    RB_EXPECT(events.items[BLINK_MAX_EVENTS - 2].type == BLINK_EVENT_EXPIRED);
    RB_EXPECT(events.items[BLINK_MAX_EVENTS - 1].type == BLINK_EVENT_GAME_OVER);
}

/* ============================================================================
 * Time and lives
 * ========================================================================== */

RB_TEST(blink_rules, an_expired_square_costs_a_life) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    rb_vec2 center = state.square.center;
    u32     color = state.square.color;
    RB_EXPECT_EQ_UINT(wait(&state, 1.0f).count, 0);
    /* Time's up, but still on screen for the grace. */
    RB_EXPECT_EQ_UINT(wait(&state, 0.5f).count, 0);
    blink_events events = wait(&state, 0.05f);
    RB_EXPECT(only(&events, BLINK_EVENT_EXPIRED));
    /* Where it vanished, at its last size: for the view's effect. */
    EXPECT_F32_NEAR(events.items[0].square.width, 1.5f * 0.4f);
    EXPECT_F32_NEAR(events.items[0].square.x, center.x - 0.3f);
    RB_EXPECT_EQ_UINT(events.items[0].color, color);
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
    blink_events events = wait(&state, 2.0f);
    RB_EXPECT_EQ_UINT(events.count, 2);
    RB_EXPECT(events.items[0].type == BLINK_EVENT_EXPIRED);
    RB_EXPECT(events.items[1].type == BLINK_EVENT_GAME_OVER);
    RB_EXPECT(state.phase == BLINK_PHASE_GAME_OVER);
    RB_EXPECT_EQ_UINT(state.lives, 0);
    RB_EXPECT_EQ_UINT(state.score, 2);
    RB_EXPECT_EQ_UINT(state.best, 2);
    EXPECT_F32_NEAR(state.phase_time, 0.0f);
    /* Over: time passes without anything else happening. */
    RB_EXPECT_EQ_UINT(wait(&state, 10.0f).count, 0);
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
    RB_EXPECT_EQ_UINT(step(&state, &action, 1, 0.5f).count, 0);
    RB_EXPECT(state.phase == BLINK_PHASE_GAME_OVER);
    wait(&state, 0.5f);
    blink_events events = step(&state, &action, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_STARTED));
    RB_EXPECT(state.phase == BLINK_PHASE_READY);
}

RB_TEST(blink_rules, restart_starts_a_fresh_game_and_keeps_the_best) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    hit(&state);
    hit(&state);
    hit(&state);
    play_until_over(&state);
    blink_action restart = {.type = BLINK_ACTION_START};
    blink_events events = step(&state, &restart, 1, 0.0f);
    RB_EXPECT(only(&events, BLINK_EVENT_STARTED));
    RB_EXPECT_EQ_UINT(state.score, 0);
    RB_EXPECT_EQ_UINT(state.lives, 3);
    RB_EXPECT_EQ_UINT(state.best, 3);
    EXPECT_F32_NEAR(state.lifetime, 1.5f);
    wait(&state, blink_default_tuning.ready_time);
    RB_EXPECT(state.phase == BLINK_PHASE_PLAYING);
    RB_EXPECT(square_inside_field(&state));
}

RB_TEST(blink_rules, restart_is_ignored_while_playing) {
    blink_state state;
    begin(&state, &blink_default_tuning, 1);
    hit(&state);
    blink_action restart = {.type = BLINK_ACTION_START};
    RB_EXPECT_EQ_UINT(step(&state, &restart, 1, 0.0f).count, 0);
    RB_EXPECT_EQ_UINT(state.score, 1);
}
