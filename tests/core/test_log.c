#include "rb_test.h"

#include <stdio.h>
#include <string.h>

#include <reborn/log.h>

#include "core/log_internal.h"

/* ============================================================================
 * Helpers
 * ========================================================================== */

/*
 * The logger is global: every test removes the sinks it adds, so tests do not
 * depend on each other when the runner executes them in one process.
 */
typedef struct capture {
    u32          count;
    u32          errors;
    rb_log_level level;
    u32          line;
    f64          time;
    i64          unix_ms;
    char         file[512];
    char         message[RB_LOG_MESSAGE_MAX];
} capture;

static void capture_sink(const rb_log_record *record, void *user) {
    capture *c = user;
    ++c->count;
    if (record->level == RB_LOG_LEVEL_ERROR) {
        ++c->errors;
    }
    c->level = record->level;
    c->line = record->line;
    c->time = record->time;
    c->unix_ms = record->unix_ms;
    snprintf(c->file, sizeof(c->file), "%s", record->file);
    snprintf(c->message, sizeof(c->message), "%s", record->message);
}

static rb_log_record make_record(rb_log_level level, const char *message) {
    return (rb_log_record){
        .level = level,
        .time = 1.5,
        .unix_ms = 1763231530123,
        .file = "engine\\src\\core\\x.c", /* as __FILE__ looks on Windows */
        .line = 42,
        .func = "f",
        .message = message,
    };
}

/* ============================================================================
 * Delivery
 * ========================================================================== */

RB_TEST(log, sink_receives_formatted_message_and_location) {
    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));

    u32 expected_line = __LINE__ + 1;
    RB_LOG_INFO("Window %ux%u", 1280u, 720u);
    rb_log_remove_sink(capture_sink, &c);

    RB_EXPECT_EQ_UINT(c.count, 1);
    RB_EXPECT_EQ_INT(c.level, RB_LOG_LEVEL_INFO);
    RB_EXPECT_EQ_STR(c.message, "Window 1280x720");
    RB_EXPECT_EQ_UINT(c.line, expected_line);
    /* -fmacro-prefix-map made __FILE__ relative to the repository root. */
    RB_EXPECT(strncmp(c.file, "tests", 5) == 0);
    RB_EXPECT(strstr(c.file, "test_log.c") != NULL);
    RB_EXPECT(c.time >= 0.0);
}

RB_TEST(log, sink_skips_records_below_its_level) {
    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_WARN));

    RB_LOG_INFO("filtered");
    RB_EXPECT_EQ_UINT(c.count, 0);
    RB_LOG_ERROR("delivered");
    RB_EXPECT_EQ_UINT(c.count, 1);
    RB_EXPECT_EQ_INT(c.level, RB_LOG_LEVEL_ERROR);

    rb_log_remove_sink(capture_sink, &c);
}

RB_TEST(log, removed_sink_receives_nothing) {
    capture a = {0};
    capture b = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &a, RB_LOG_LEVEL_TRACE));
    RB_REQUIRE(rb_log_add_sink(capture_sink, &b, RB_LOG_LEVEL_TRACE));

    /* Same function, different `user`: only the exact pair is removed. */
    rb_log_remove_sink(capture_sink, &a);
    RB_LOG_INFO("only b");
    rb_log_remove_sink(capture_sink, &b);

    RB_EXPECT_EQ_UINT(a.count, 0);
    RB_EXPECT_EQ_UINT(b.count, 1);
}

RB_TEST(log, sink_slots_are_limited) {
    capture c[RB_LOG_MAX_SINKS + 1];
    u32     added = 0;
    while (added < RB_LOG_MAX_SINKS &&
           rb_log_add_sink(capture_sink, &c[added], RB_LOG_LEVEL_TRACE)) {
        ++added;
    }
    RB_EXPECT_EQ_UINT(added, RB_LOG_MAX_SINKS);
    RB_EXPECT(!rb_log_add_sink(capture_sink, &c[RB_LOG_MAX_SINKS], RB_LOG_LEVEL_TRACE));
    RB_EXPECT(!rb_log_add_sink(NULL, NULL, RB_LOG_LEVEL_TRACE));

    for (u32 i = 0; i < added; ++i) {
        rb_log_remove_sink(capture_sink, &c[i]);
    }
}

static u32 g_reentrant_calls;

static void reentrant_sink(const rb_log_record *record, void *user) {
    RB_UNUSED(record);
    RB_UNUSED(user);
    ++g_reentrant_calls;
    RB_LOG_INFO("logged from inside a sink");
}

RB_TEST(log, sink_that_logs_does_not_recurse) {
    g_reentrant_calls = 0;
    RB_REQUIRE(rb_log_add_sink(reentrant_sink, NULL, RB_LOG_LEVEL_TRACE));
    RB_LOG_INFO("outer");
    rb_log_remove_sink(reentrant_sink, NULL);
    RB_EXPECT_EQ_UINT(g_reentrant_calls, 1);
}

RB_TEST(log, compile_time_level_removes_calls_and_their_arguments) {
    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));
    int evaluated = 0;
    RB_LOG_TRACE("%d", ++evaluated);
    rb_log_remove_sink(capture_sink, &c);

#if defined(RB_DEBUG)
    RB_EXPECT_EQ_INT(evaluated, 1);
    RB_EXPECT_EQ_UINT(c.count, 1);
#else
    RB_EXPECT_EQ_INT(evaluated, 0);
    RB_EXPECT_EQ_UINT(c.count, 0);
#endif
}

RB_TEST(log, time_does_not_go_backwards) {
    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));
    RB_LOG_INFO("first");
    f64 first = c.time;
    RB_LOG_INFO("second");
    rb_log_remove_sink(capture_sink, &c);
    RB_EXPECT(c.time >= first);
}

/* ============================================================================
 * Message formatting
 * ========================================================================== */

RB_TEST(log, long_message_is_cut_with_marker) {
    static char long_text[RB_LOG_MESSAGE_MAX + 100];
    memset(long_text, 'a', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';

    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));
    RB_LOG_INFO("%s", long_text);
    rb_log_remove_sink(capture_sink, &c);

    usize length = strlen(c.message);
    RB_EXPECT_EQ_UINT(length, RB_LOG_MESSAGE_MAX - 1);
    RB_EXPECT_EQ_STR(c.message + length - 3, "...");
}

RB_TEST(log, cut_never_splits_a_utf8_character) {
    /*
     * The cut starts at byte MAX - 4. Placing a 2-byte "é" (C3 A9) at
     * MAX - 5 and MAX - 4 puts the cut on its second byte: the whole
     * character must go.
     */
    static char text[RB_LOG_MESSAGE_MAX + 100];
    usize       prefix = RB_LOG_MESSAGE_MAX - 5;
    memset(text, 'a', prefix);
    usize at = prefix;
    while (at + 2 < sizeof(text)) {
        text[at++] = (char)0xC3;
        text[at++] = (char)0xA9;
    }
    text[at] = '\0';

    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));
    RB_LOG_INFO("%s", text);
    rb_log_remove_sink(capture_sink, &c);

    RB_EXPECT_EQ_UINT(strlen(c.message), prefix + 3);
    RB_EXPECT_EQ_STR(c.message + prefix, "...");
}

RB_TEST(log, level_names) {
    RB_EXPECT_EQ_STR(rb_log_level_name(RB_LOG_LEVEL_TRACE), "TRACE");
    RB_EXPECT_EQ_STR(rb_log_level_name(RB_LOG_LEVEL_FATAL), "FATAL");
    RB_EXPECT_EQ_STR(rb_log_level_name((rb_log_level)99), "?");
    RB_EXPECT_EQ_STR(rb_log_level_name((rb_log_level)-1), "?");
}

/* ============================================================================
 * Console format
 * ========================================================================== */

RB_TEST(log, console_line_without_location_below_warn) {
    char          out[256];
    rb_log_record r = make_record(RB_LOG_LEVEL_INFO, "hello");
    usize         length = rb_log_format_console(&r, false, out, sizeof(out));
    RB_EXPECT_EQ_STR(out, "    1.500 INFO  hello\n");
    RB_EXPECT_EQ_UINT(length, strlen(out));
}

RB_TEST(log, console_line_with_location_from_warn) {
    char          out[256];
    rb_log_record r = make_record(RB_LOG_LEVEL_WARN, "careful");
    rb_log_format_console(&r, false, out, sizeof(out));
    RB_EXPECT_EQ_STR(out, "    1.500 WARN  careful  (engine/src/core/x.c:42)\n");
}

RB_TEST(log, console_color_wraps_level_in_escape_codes) {
    char          out[256];
    rb_log_record r = make_record(RB_LOG_LEVEL_ERROR, "boom");
    rb_log_format_console(&r, true, out, sizeof(out));
    RB_EXPECT(strstr(out, "\x1b[31mERROR\x1b[0m") != NULL);
    RB_EXPECT(strstr(out, "boom") != NULL);
}

RB_TEST(log, console_line_that_does_not_fit_still_ends_with_newline) {
    char          out[16];
    rb_log_record r = make_record(RB_LOG_LEVEL_INFO, "a message longer than the buffer");
    usize         length = rb_log_format_console(&r, false, out, sizeof(out));
    RB_EXPECT_EQ_UINT(length, sizeof(out) - 1);
    RB_EXPECT_EQ_INT(out[length - 1], '\n');
    RB_EXPECT_EQ_INT(out[length], '\0');
}

/* ============================================================================
 * JSON format
 * ========================================================================== */

RB_TEST(log, json_line_has_every_field) {
    char          out[512];
    rb_log_record r = make_record(RB_LOG_LEVEL_WARN, "hello");
    usize         length = rb_log_format_json(&r, out, sizeof(out));
    RB_EXPECT_EQ_STR(out, "{\"t\":1.500000,\"ts\":1763231530123,\"level\":\"WARN\","
                          "\"file\":\"engine/src/core/x.c\",\"line\":42,\"func\":\"f\","
                          "\"msg\":\"hello\"}\n");
    RB_EXPECT_EQ_UINT(length, strlen(out));
}

RB_TEST(log, json_escapes_quotes_backslashes_and_control_characters) {
    char          out[512];
    rb_log_record r = make_record(RB_LOG_LEVEL_INFO, "say \"hi\" \\ \n\t\x01 città");
    rb_log_format_json(&r, out, sizeof(out));
    const char *msg = strstr(out, "\"msg\":");
    RB_REQUIRE(msg != NULL);
    /* UTF-8 passes through unchanged: JSON text is UTF-8. */
    RB_EXPECT_EQ_STR(msg, "\"msg\":\"say \\\"hi\\\" \\\\ \\n\\t\\u0001 città\"}\n");
}

RB_TEST(log, json_that_does_not_fit_stays_valid) {
    char          out[160];
    char          message[300];
    rb_log_record r = make_record(RB_LOG_LEVEL_INFO, message);
    memset(message, 'x', sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';

    usize length = rb_log_format_json(&r, out, sizeof(out));
    RB_REQUIRE(length > 0);
    RB_EXPECT(strstr(out, "\"msg\":\"(message too long") != NULL);
    RB_EXPECT_EQ_STR(out + length - 3, "\"}\n");
}

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

RB_TEST(log, startup_writes_json_lines_file) {
    const char *path = "test_log_startup.jsonl";
    /* Console at FATAL: keeps the test output quiet. */
    rb_log_config config = {
        .console_level = RB_LOG_LEVEL_FATAL,
        .file_path = path,
        .file_level = RB_LOG_LEVEL_TRACE,
    };
    RB_REQUIRE(rb_log_startup(&config));
    RB_EXPECT(!rb_log_startup(&config)); /* already started */
    RB_LOG_INFO("from the test");
    rb_log_shutdown();

    FILE *file = fopen(path, "rb");
    RB_REQUIRE(file != NULL);
    char  content[512] = {0};
    usize read = fread(content, 1, sizeof(content) - 1, file);
    fclose(file);
    remove(path);

    RB_EXPECT(read > 0);
    RB_EXPECT(strncmp(content, "{\"t\":", 5) == 0);
    RB_EXPECT(strstr(content, "\"msg\":\"Log clock: t = 0 at ") != NULL);
    RB_EXPECT(strstr(content, "\"level\":\"INFO\"") != NULL);
    RB_EXPECT(strstr(content, "\"msg\":\"from the test\"}\n") != NULL);
}

RB_TEST(log, startup_with_unwritable_file_fails_but_keeps_console) {
    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));
    rb_log_config config = {
        .console_level = RB_LOG_LEVEL_FATAL,
        .file_path = "no_such_directory/x.jsonl",
    };
    RB_EXPECT(!rb_log_startup(&config));
    rb_log_shutdown();
    rb_log_remove_sink(capture_sink, &c);

    /* The failure itself was logged, and the clock line still came after it. */
    RB_EXPECT_EQ_UINT(c.errors, 1);
    RB_EXPECT_EQ_UINT(c.count, 2);
    RB_EXPECT(strncmp(c.message, "Log clock: t = 0 at ", 20) == 0);
}

/* ============================================================================
 * Wall clock
 * ========================================================================== */

RB_TEST(log, record_carries_wall_clock_milliseconds) {
    capture c = {0};
    RB_REQUIRE(rb_log_add_sink(capture_sink, &c, RB_LOG_LEVEL_TRACE));
    RB_LOG_INFO("now");
    rb_log_remove_sink(capture_sink, &c);
    /* 1577836800000 is 2020-01-01 UTC: any working clock is past it. */
    RB_EXPECT(c.unix_ms > 1577836800000);
}

RB_TEST(log, timestamp_is_iso8601_local_time_with_offset) {
    /* The hour depends on the machine's time zone: check the shape, and the milliseconds. */
    struct timespec instant = {.tv_sec = 1763231530, .tv_nsec = 123456789};
    char            out[64];
    RB_REQUIRE(rb_log_format_timestamp(&instant, out, sizeof(out)));
    RB_EXPECT_EQ_UINT(strlen(out), RB_LOG_TIMESTAMP_LENGTH);

    const char *shape = "dddd-dd-ddTdd:dd:dd.ddd+dd:dd";
    for (usize i = 0; i < RB_LOG_TIMESTAMP_LENGTH; ++i) {
        if (shape[i] == 'd') {
            RB_EXPECT(out[i] >= '0' && out[i] <= '9');
        } else if (shape[i] == '+') {
            RB_EXPECT(out[i] == '+' || out[i] == '-');
        } else {
            RB_EXPECT_EQ_INT(out[i], shape[i]);
        }
    }
    RB_EXPECT(strncmp(out + 19, ".123", 4) == 0);
}

RB_TEST(log, timestamp_rejects_a_small_buffer) {
    struct timespec instant = {.tv_sec = 0, .tv_nsec = 0};
    char            out[RB_LOG_TIMESTAMP_LENGTH];
    RB_EXPECT(!rb_log_format_timestamp(&instant, out, sizeof(out)));
    RB_EXPECT_EQ_STR(out, "");
}
