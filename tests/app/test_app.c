/*
 * Application configuration (ADR 0011): defaults, validation, and runs that
 * stop before any system starts, so no window is needed.
 */
#include "rb_test.h"

#include <stdio.h>
#include <string.h>

#include "app/app_internal.h"

/* ============================================================================
 * Validation
 * ========================================================================== */

RB_TEST(app, default_config_is_valid) {
    rb_app_config config = rb_app_default_config();
    RB_EXPECT(rb_app_config_validate(&config));
    RB_EXPECT_EQ_UINT(config.update_rate, 60);
    RB_EXPECT_EQ_UINT(config.max_fps, RB_APP_FPS_UNCAPPED);
    RB_EXPECT_EQ_UINT(config.state_size, 0);
    RB_EXPECT_EQ_INT(config.log.console_level, RB_LOG_LEVEL_INFO);
    RB_EXPECT(config.log.file_path == NULL);
    RB_EXPECT(config.renderer.vsync);
#if defined(RB_DEBUG)
    RB_EXPECT(config.renderer.validation);
#else
    RB_EXPECT(!config.renderer.validation);
#endif
}

RB_TEST(app, state_macro_takes_size_and_alignment_from_the_type) {
    typedef struct aligned_state {
        _Alignas(64) u8 bytes[100];
    } aligned_state;
    rb_app_config config = rb_app_default_config();
    RB_APP_STATE(&config, aligned_state);
    RB_EXPECT_EQ_UINT(config.state_size, sizeof(aligned_state));
    RB_EXPECT_EQ_UINT(config.state_align, 64);
    RB_EXPECT(rb_app_config_validate(&config));
}

RB_TEST(app, update_rate_must_be_in_range) {
    rb_app_config config = rb_app_default_config();
    config.update_rate = 0;
    RB_EXPECT(!rb_app_config_validate(&config));
    config.update_rate = 1001;
    RB_EXPECT(!rb_app_config_validate(&config));
    config.update_rate = 1000;
    RB_EXPECT(rb_app_config_validate(&config));
}

RB_TEST(app, state_alignment_must_be_a_power_of_two) {
    rb_app_config config = rb_app_default_config();
    config.state_align = 0;
    RB_EXPECT(!rb_app_config_validate(&config));
    config.state_align = 24;
    RB_EXPECT(!rb_app_config_validate(&config));
}

RB_TEST(app, arenas_and_window_need_sizes) {
    rb_app_config config = rb_app_default_config();
    config.frame_reserve = 0;
    RB_EXPECT(!rb_app_config_validate(&config));

    config = rb_app_default_config();
    config.window.title = NULL;
    RB_EXPECT(!rb_app_config_validate(&config));

    config = rb_app_default_config();
    config.window.height = 0;
    RB_EXPECT(!rb_app_config_validate(&config));
}

RB_TEST(app, uncapped_frame_rate_is_valid) {
    rb_app_config config = rb_app_default_config();
    config.max_fps = RB_APP_FPS_UNCAPPED;
    RB_EXPECT(rb_app_config_validate(&config));
}

/* ============================================================================
 * Runs that stop before startup
 * ========================================================================== */

static u32 configure_calls;
static u32 init_calls;

static bool count_init(void *state, rb_app *app) {
    (void)state;
    (void)app;
    ++init_calls;
    return true;
}

static void no_update(void *state, rb_app *app, f64 dt) {
    (void)state;
    (void)app;
    (void)dt;
}

static bool refuse_configure(rb_app_config *config) {
    (void)config;
    ++configure_calls;
    return false;
}

static bool break_configure(rb_app_config *config) {
    config->update_rate = 0;
    return true;
}

RB_TEST(app, failed_configure_stops_before_init) {
    init_calls = 0;
    int code = rb_app_run(
        &(rb_app_callbacks){.configure = refuse_configure, .init = count_init, .update = no_update},
        0, NULL);
    RB_EXPECT_EQ_INT(code, 1);
    RB_EXPECT_EQ_UINT(init_calls, 0);
}

RB_TEST(app, invalid_config_stops_before_init) {
    init_calls = 0;
    int code = rb_app_run(
        &(rb_app_callbacks){.configure = break_configure, .init = count_init, .update = no_update},
        0, NULL);
    RB_EXPECT_EQ_INT(code, 1);
    RB_EXPECT_EQ_UINT(init_calls, 0);
}

/* With other headers even the callbacks' layout may differ: nothing of the game may run. */
RB_TEST(app, version_mismatch_stops_before_configure) {
    configure_calls = 0;
    int code =
        rb_app_run_checked(&(rb_app_callbacks){.configure = refuse_configure, .update = no_update},
                           0, NULL, RB_VERSION_NUMBER + 1);
    RB_EXPECT_EQ_INT(code, 1);
    RB_EXPECT_EQ_UINT(configure_calls, 0);
}

#define TEST_LOG_FILE "test_app_run_log.jsonl"

static bool log_to_file_then_break(rb_app_config *config) {
    config->log.file_path = TEST_LOG_FILE;
    config->update_rate = 0;
    return true;
}

/*
 * The logger starts after configure, with its outputs; the configuration as
 * received is logged before validation, then the failure: all in the file.
 */
RB_TEST(app, run_logs_to_the_file_chosen_by_configure) {
    remove(TEST_LOG_FILE);
    int code = rb_app_run(
        &(rb_app_callbacks){.configure = log_to_file_then_break, .update = no_update}, 0, NULL);
    RB_EXPECT_EQ_INT(code, 1);

    FILE *file = fopen(TEST_LOG_FILE, "rb");
    RB_REQUIRE(file != NULL);
    static char text[8192];
    usize       size = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    remove(TEST_LOG_FILE);
    text[size] = '\0';
    const char *received = strstr(text, "Config: 0 updates/s");
    const char *refused = strstr(text, "App config is invalid");
    RB_EXPECT(received != NULL);
    RB_EXPECT(refused != NULL);
    RB_EXPECT(received < refused);
    RB_EXPECT(strstr(text, "Exiting with code 1") != NULL);
}

/* ============================================================================
 * Command line (ADR 0022)
 * ========================================================================== */

static rb_app_args_result parse(int argc, char **argv, rb_app_options *options) {
    return rb_app_parse_args(argc, argv, options);
}

RB_TEST(app_args, none_is_a_plain_run) {
    rb_app_options options;
    RB_EXPECT_EQ_INT(parse(0, NULL, &options), RB_APP_ARGS_RUN);
    RB_EXPECT(options.script_path == NULL);
    RB_EXPECT(!options.listen);
    char *argv[] = {"game"};
    RB_EXPECT_EQ_INT(parse(1, argv, &options), RB_APP_ARGS_RUN);
}

RB_TEST(app_args, script_and_listen) {
    rb_app_options options;
    char          *argv[] = {"game", "--listen", "7777", "--script", "a b.txt"};
    RB_REQUIRE_EQ_INT(parse(5, argv, &options), RB_APP_ARGS_RUN);
    RB_EXPECT_EQ_STR(options.script_path, "a b.txt");
    RB_EXPECT(options.listen);
    RB_EXPECT_EQ_UINT(options.port, 7777);

    char *any_port[] = {"game", "--listen", "0"};
    RB_REQUIRE_EQ_INT(parse(3, any_port, &options), RB_APP_ARGS_RUN);
    RB_EXPECT(options.listen);
    RB_EXPECT_EQ_UINT(options.port, 0);
}

RB_TEST(app_args, mistakes_are_refused) {
    rb_app_options options;
    char          *unknown[] = {"game", "--lsten", "7777"};
    RB_EXPECT_EQ_INT(parse(3, unknown, &options), RB_APP_ARGS_INVALID);
    char *no_file[] = {"game", "--script"};
    RB_EXPECT_EQ_INT(parse(2, no_file, &options), RB_APP_ARGS_INVALID);
    char *no_port[] = {"game", "--listen"};
    RB_EXPECT_EQ_INT(parse(2, no_port, &options), RB_APP_ARGS_INVALID);
    const char *bad_ports[] = {"65536", "-1", "77x", "", "123456"};
    for (u32 i = 0; i < RB_ARRAY_COUNT(bad_ports); ++i) {
        char *bad[] = {"game", "--listen", (char *)bad_ports[i]};
        RB_EXPECT_EQ_INT(parse(3, bad, &options), RB_APP_ARGS_INVALID);
    }
}

RB_TEST(app_args, help) {
    rb_app_options options;
    char          *argv[] = {"game", "--script", "s.txt", "--help"};
    RB_EXPECT_EQ_INT(parse(4, argv, &options), RB_APP_ARGS_HELP);
}

/* A bad option stops the run before configure: nothing of the game runs, exit code 1. */
RB_TEST(app, unknown_option_stops_before_configure) {
    configure_calls = 0;
    char *argv[] = {"game", "--nope"};
    int   code = rb_app_run(&(rb_app_callbacks){.configure = refuse_configure, .update = no_update},
                            2, argv);
    RB_EXPECT_EQ_INT(code, 1);
    RB_EXPECT_EQ_UINT(configure_calls, 0);
}
