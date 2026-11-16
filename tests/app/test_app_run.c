/*
 * Full application runs (ADR 0011): each opens the main window for a few
 * milliseconds. Built on Windows only: the Linux test container has no X
 * server (the loop is checked there with the sandbox under Xvfb).
 */
#include "rb_test.h"

#include <string.h>

#include <reborn/app.h>

/* ============================================================================
 * Recorder
 *
 * Callbacks cannot return anything to the test, and the state dies with the
 * run: what they see is written here.
 * ========================================================================== */

#define TRACE_CAPACITY 16

typedef struct test_state {
    _Alignas(64) u64 value;
} test_state;

typedef struct recorder {
    bool  init_result;
    u32   quit_after_updates; /* 0: quit from init */
    char  trace[TRACE_CAPACITY + 1];
    usize trace_len;
    u32   updates;
    u32   renders;
    f64   dt;
    bool  state_was_zeroed;
    bool  state_was_aligned;
    bool  state_in_persistent_arena;
    /* Only render pushes: the position it finds is 0 if the arena is reset per frame. */
    usize max_frame_pos_at_render;
    bool  alpha_in_range;
} recorder;

static recorder rec;

static void reset_recorder(bool init_result, u32 quit_after_updates) {
    rec = (recorder){0};
    rec.init_result = init_result;
    rec.quit_after_updates = quit_after_updates;
    rec.alpha_in_range = true;
}

static void trace(char phase) {
    if (rec.trace_len < TRACE_CAPACITY) {
        rec.trace[rec.trace_len++] = phase;
    }
}

/* ============================================================================
 * Callbacks
 * ========================================================================== */

static bool test_configure(rb_app_config *config) {
    trace('c');
    config->window.title = "reborn_tests";
    config->window.width = 320;
    config->window.height = 200;
    config->update_rate = 1000;
    config->max_fps = RB_APP_FPS_UNCAPPED;
    RB_APP_STATE(config, test_state);
    return true;
}

static bool test_init(void *state, rb_app *app) {
    trace('i');
    test_state *s = state;
    rb_arena   *persistent = rb_app_persistent_arena(app);
    rec.state_was_zeroed = s->value == 0;
    rec.state_was_aligned = ((usize)state % 64) == 0;
    rec.state_in_persistent_arena =
        (u8 *)state >= persistent->base && (u8 *)state < persistent->base + persistent->pos;
    if (rec.quit_after_updates == 0) {
        rb_app_quit(app);
    }
    return rec.init_result;
}

static void test_update(void *state, rb_app *app, f64 dt) {
    (void)state;
    trace('u');
    rec.dt = dt;
    if (++rec.updates == rec.quit_after_updates) {
        rb_app_quit(app);
        rb_app_quit(app);
    }
}

static void test_render(void *state, rb_app *app, f64 alpha) {
    (void)state;
    ++rec.renders;
    if (alpha < 0.0 || alpha >= 1.0) {
        rec.alpha_in_range = false;
    }
    rb_arena *frame = rb_app_frame_arena(app);
    if (frame->pos > rec.max_frame_pos_at_render) {
        rec.max_frame_pos_at_render = frame->pos;
    }
    (void)rb_arena_push(frame, 64, 8);
}

static void test_shutdown(void *state, rb_app *app) {
    (void)state;
    (void)app;
    trace('s');
}

static const rb_app_callbacks test_callbacks = {
    .configure = test_configure,
    .init = test_init,
    .update = test_update,
    .render = test_render,
    .shutdown = test_shutdown,
};

/* ============================================================================
 * Tests
 * ========================================================================== */

RB_TEST(app_run, phases_run_in_order_until_quit) {
    reset_recorder(true, 3);
    RB_EXPECT_EQ_INT(rb_app_run(&test_callbacks), 0);
    RB_EXPECT_EQ_STR(rec.trace, "ciuuus");
    RB_EXPECT_EQ_UINT(rec.updates, 3);
    RB_EXPECT_NEAR(rec.dt, 0.001, 1e-12);
}

RB_TEST(app_run, state_is_zeroed_aligned_and_persistent) {
    reset_recorder(true, 1);
    RB_REQUIRE_EQ_INT(rb_app_run(&test_callbacks), 0);
    RB_EXPECT(rec.state_was_zeroed);
    RB_EXPECT(rec.state_was_aligned);
    RB_EXPECT(rec.state_in_persistent_arena);
}

RB_TEST(app_run, frame_arena_is_reset_every_frame) {
    reset_recorder(true, 50);
    RB_REQUIRE_EQ_INT(rb_app_run(&test_callbacks), 0);
    RB_REQUIRE(rec.renders > 1);
    RB_EXPECT_EQ_UINT(rec.max_frame_pos_at_render, 0);
    RB_EXPECT(rec.alpha_in_range);
}

RB_TEST(app_run, failed_init_skips_loop_and_shutdown) {
    reset_recorder(false, 3);
    RB_EXPECT_EQ_INT(rb_app_run(&test_callbacks), 1);
    RB_EXPECT_EQ_STR(rec.trace, "ci");
}

RB_TEST(app_run, quit_from_init_still_shuts_down) {
    reset_recorder(true, 0);
    RB_EXPECT_EQ_INT(rb_app_run(&test_callbacks), 0);
    RB_EXPECT_EQ_STR(rec.trace, "cis");
}

/* ============================================================================
 * Phase logging
 * ========================================================================== */

/*
 * Phase lines are DEBUG, the quit reason INFO: release builds compile DEBUG
 * out, engine and tests alike (same RB_LOG_MIN_LEVEL), and keep only the quit.
 * An expression, not #if: the levels are enum constants, unknown to the preprocessor.
 */
#define PHASES_LOGGED (RB_LOG_MIN_LEVEL <= RB_LOG_LEVEL_DEBUG)

/* The engine's phase lines, in order, one letter each; anything else is ignored. */
typedef struct phase_log {
    char  letters[TRACE_CAPACITY + 1];
    usize len;
} phase_log;

static void capture_phases(const rb_log_record *record, void *user) {
    static const struct {
        const char *message;
        char        letter;
    } phases[] = {{"Game init", 'i'},
                  {"Loop started", 'l'},
                  {"Quit: requested by the game", 'q'},
                  {"Quit: main window close requested", 'w'},
                  {"Game shutdown", 's'}};
    phase_log *log = user;
    for (usize i = 0; i < RB_ARRAY_COUNT(phases); ++i) {
        if (strcmp(record->message, phases[i].message) == 0 && log->len < TRACE_CAPACITY) {
            log->letters[log->len++] = phases[i].letter;
        }
    }
}

RB_TEST(app_run, phases_and_quit_reason_are_logged_once) {
    reset_recorder(true, 2);
    phase_log log = {0};
    RB_REQUIRE(rb_log_add_sink(capture_phases, &log, RB_LOG_LEVEL_TRACE));
    int code = rb_app_run(&test_callbacks);
    rb_log_remove_sink(capture_phases, &log);
    RB_EXPECT_EQ_INT(code, 0);
    RB_EXPECT_EQ_STR(log.letters, PHASES_LOGGED ? "ilqs" : "q");
}

RB_TEST(app_run, quit_from_init_is_logged_before_any_loop) {
    reset_recorder(true, 0);
    phase_log log = {0};
    RB_REQUIRE(rb_log_add_sink(capture_phases, &log, RB_LOG_LEVEL_TRACE));
    int code = rb_app_run(&test_callbacks);
    rb_log_remove_sink(capture_phases, &log);
    RB_EXPECT_EQ_INT(code, 0);
    RB_EXPECT_EQ_STR(log.letters, PHASES_LOGGED ? "iqs" : "q");
}
