/* Commands (ADR 0022): line splitting, the registry, running lines into replies. */
#include "rb_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "command/command_internal.h"

/* ============================================================================
 * Line splitting
 * ========================================================================== */

typedef struct split_case {
    char                    line[128];
    char                   *words[8];
    u32                     count;
    rb_command_split_result result;
} split_case;

static void split(split_case *c, const char *line) {
    strcpy(c->line, line);
    c->result = rb_command_split(c->line, c->words, RB_ARRAY_COUNT(c->words), &c->count);
}

RB_TEST(command_split, words_are_separated_by_spaces_and_tabs) {
    split_case c;
    split(&c, "  step\t 10   ");
    RB_REQUIRE_EQ_INT(c.result, RB_COMMAND_SPLIT_OK);
    RB_REQUIRE_EQ_UINT(c.count, 2);
    RB_EXPECT_EQ_STR(c.words[0], "step");
    RB_EXPECT_EQ_STR(c.words[1], "10");
}

RB_TEST(command_split, quotes_group_words_and_are_removed) {
    split_case c;
    split(&c, "say \"hello world\" a\"b c\"d \"\"");
    RB_REQUIRE_EQ_INT(c.result, RB_COMMAND_SPLIT_OK);
    RB_REQUIRE_EQ_UINT(c.count, 4);
    RB_EXPECT_EQ_STR(c.words[1], "hello world");
    RB_EXPECT_EQ_STR(c.words[2], "ab cd");
    RB_EXPECT_EQ_STR(c.words[3], "");
}

/* Only \" and \\ are escapes, and only inside quotes: Windows paths survive. */
RB_TEST(command_split, backslashes_escape_only_quotes_and_backslashes) {
    split_case c;
    split(&c, "\"a \\\"b\\\" \\\\c\" \"C:\\dir\" C:\\x\\\"");
    RB_REQUIRE_EQ_INT(c.result, RB_COMMAND_SPLIT_UNTERMINATED_QUOTE);

    split(&c, "\"a \\\"b\\\" \\\\c\" \"C:\\dir\" C:\\x");
    RB_REQUIRE_EQ_INT(c.result, RB_COMMAND_SPLIT_OK);
    RB_REQUIRE_EQ_UINT(c.count, 3);
    RB_EXPECT_EQ_STR(c.words[0], "a \"b\" \\c");
    RB_EXPECT_EQ_STR(c.words[1], "C:\\dir");
    RB_EXPECT_EQ_STR(c.words[2], "C:\\x");
}

RB_TEST(command_split, a_hash_at_the_start_of_a_word_ends_the_line) {
    split_case c;
    split(&c, "step 2 # two more  \"unclosed");
    RB_REQUIRE_EQ_INT(c.result, RB_COMMAND_SPLIT_OK);
    RB_EXPECT_EQ_UINT(c.count, 2);

    split(&c, "say a#b \"#c\"");
    RB_REQUIRE_EQ_UINT(c.count, 3);
    RB_EXPECT_EQ_STR(c.words[1], "a#b");
    RB_EXPECT_EQ_STR(c.words[2], "#c");
}

RB_TEST(command_split, blank_lines_and_comments_are_empty) {
    split_case c;
    split(&c, "");
    RB_EXPECT_EQ_INT(c.result, RB_COMMAND_SPLIT_EMPTY);
    split(&c, " \t ");
    RB_EXPECT_EQ_INT(c.result, RB_COMMAND_SPLIT_EMPTY);
    split(&c, "  # a comment");
    RB_EXPECT_EQ_INT(c.result, RB_COMMAND_SPLIT_EMPTY);
}

RB_TEST(command_split, errors) {
    split_case c;
    split(&c, "say \"open");
    RB_EXPECT_EQ_INT(c.result, RB_COMMAND_SPLIT_UNTERMINATED_QUOTE);
    split(&c, "1 2 3 4 5 6 7 8");
    RB_EXPECT_EQ_INT(c.result, RB_COMMAND_SPLIT_OK);
    split(&c, "1 2 3 4 5 6 7 8 9");
    RB_EXPECT_EQ_INT(c.result, RB_COMMAND_SPLIT_TOO_MANY_WORDS);
}

/* ============================================================================
 * Handlers used below
 * ========================================================================== */

/* What the last handler call saw. */
static struct {
    u32         calls;
    void       *state;
    rb_app     *app;
    u32         arg_count;
    const char *first_arg;
} seen;

static void cmd_record(void *state, rb_app *app, const rb_command_args *args,
                       rb_command_reply *reply) {
    (void)reply;
    ++seen.calls;
    seen.state = state;
    seen.app = app;
    seen.arg_count = args->count;
    seen.first_arg = args->count > 0 ? args->values[0] : NULL;
}

static void cmd_fields(void *state, rb_app *app, const rb_command_args *args,
                       rb_command_reply *reply) {
    (void)state;
    (void)app;
    (void)args;
    rb_command_reply_bool(reply, "yes", true);
    rb_command_reply_i64(reply, "neg", -5);
    rb_command_reply_u64(reply, "big", UINT64_MAX);
    rb_command_reply_f32(reply, "tenth", 0.1f);
    rb_command_reply_f64(reply, "half", 0.5);
    rb_command_reply_f64(reply, "nan", (f64)NAN);
    rb_command_reply_str(reply, "quote", "a\"b\\c\n");
    rb_command_reply_begin_object(reply, "inner");
    rb_command_reply_u64(reply, "x", 1);
    rb_command_reply_begin_object(reply, "deeper");
    rb_command_reply_end_object(reply);
    rb_command_reply_end_object(reply);
    rb_command_reply_u64(reply, "after", 2);
}

static void cmd_fail_after_fields(void *state, rb_app *app, const rb_command_args *args,
                                  rb_command_reply *reply) {
    (void)state;
    (void)app;
    (void)args;
    rb_command_reply_u64(reply, "dropped", 1);
    rb_command_reply_error(reply, "first %d", 1);
    rb_command_reply_error(reply, "second");
}

static void cmd_numbers(void *state, rb_app *app, const rb_command_args *args,
                        rb_command_reply *reply) {
    (void)state;
    (void)app;
    u32 a;
    u64 b;
    f32 c;
    if (!rb_command_arg_u32(args, 0, &a, reply) || !rb_command_arg_u64(args, 1, &b, reply) ||
        !rb_command_arg_f32(args, 2, &c, reply)) {
        return;
    }
    rb_command_reply_u64(reply, "a", a);
    rb_command_reply_u64(reply, "b", b);
    rb_command_reply_f32(reply, "c", c);
}

static void cmd_huge(void *state, rb_app *app, const rb_command_args *args,
                     rb_command_reply *reply) {
    (void)state;
    (void)app;
    (void)args;
    static char text[1024];
    memset(text, 'x', sizeof(text) - 1);
    for (u32 i = 0; i < 20; ++i) {
        rb_command_reply_str(reply, "x", text);
    }
}

/* Large: static, like the arena the app takes it from. */
static rb_commands commands;

static void setup(void) {
    rb_commands_init(&commands);
    seen.calls = 0;
    RB_EXPECT(rb_commands_add(
        &commands,
        &(rb_command_desc){.name = "record", .usage = "[a] [b]", .max_args = 2, .fn = cmd_record},
        false));
    RB_EXPECT(
        rb_commands_add(&commands, &(rb_command_desc){.name = "fields", .fn = cmd_fields}, false));
    RB_EXPECT(rb_commands_add(
        &commands, &(rb_command_desc){.name = "fail", .fn = cmd_fail_after_fields}, false));
    RB_EXPECT(rb_commands_add(&commands,
                              &(rb_command_desc){.name = "numbers",
                                                 .usage = "<a> <b> <c>",
                                                 .min_args = 3,
                                                 .max_args = 3,
                                                 .fn = cmd_numbers},
                              false));
    RB_EXPECT(
        rb_commands_add(&commands, &(rb_command_desc){.name = "huge", .fn = cmd_huge}, false));
}

static const char *run(const char *line) {
    return rb_commands_run(&commands, line, "test", NULL, NULL);
}

/* ============================================================================
 * Registry
 * ========================================================================== */

RB_TEST(command_registry, names_are_checked) {
    setup();
    rb_command_desc desc = {.fn = cmd_record};
    const char     *bad[] = {"",
                             "Step",
                             "step-two",
                             "with space",
                             ".lead",
                             "trail.",
                             "a_name_much_longer_than_thirty_one"};
    for (u32 i = 0; i < RB_ARRAY_COUNT(bad); ++i) {
        desc.name = bad[i];
        RB_EXPECT(!rb_commands_add(&commands, &desc, false));
    }
    desc.name = "record";
    RB_EXPECT(!rb_commands_add(&commands, &desc, false)); /* taken */
    desc.name = "noprefix";
    RB_EXPECT(!rb_commands_add(&commands, &desc, true));
    desc.name = "game.cmd_2";
    RB_EXPECT(rb_commands_add(&commands, &desc, true));
    RB_EXPECT(rb_commands_find(&commands, "game.cmd_2") != NULL);
    RB_EXPECT(rb_commands_find(&commands, "game") == NULL);
}

RB_TEST(command_registry, argument_bounds_are_checked) {
    setup();
    RB_EXPECT(!rb_commands_add(
        &commands, &(rb_command_desc){.name = "a", .min_args = 2, .max_args = 1, .fn = cmd_record},
        false));
    RB_EXPECT(!rb_commands_add(
        &commands,
        &(rb_command_desc){.name = "b", .max_args = RB_COMMAND_MAX_WORDS, .fn = cmd_record},
        false));
}

RB_TEST(command_registry, the_table_fills_up) {
    rb_commands_init(&commands);
    char names[RB_COMMAND_MAX_COMMANDS + 1][8];
    for (u32 i = 0; i <= RB_COMMAND_MAX_COMMANDS; ++i) {
        snprintf(names[i], sizeof(names[i]), "c%u", i);
        bool added = rb_commands_add(&commands,
                                     &(rb_command_desc){.name = names[i], .fn = cmd_record}, false);
        RB_EXPECT(added == (i < RB_COMMAND_MAX_COMMANDS));
    }
}

/* ============================================================================
 * Running lines
 * ========================================================================== */

RB_TEST(command_run, calls_the_handler_with_state_app_and_arguments) {
    setup();
    int         state = 0;
    rb_app     *app = (rb_app *)&state; /* never dereferenced: only passed through */
    const char *reply = rb_commands_run(&commands, "record \"two words\"", "socket", &state, app);
    RB_EXPECT_EQ_UINT(seen.calls, 1);
    RB_EXPECT(seen.state == &state);
    RB_EXPECT(seen.app == app);
    RB_EXPECT_EQ_UINT(seen.arg_count, 1);
    RB_EXPECT_EQ_STR(seen.first_arg, "two words");
    RB_EXPECT_EQ_STR(
        reply, "{\"id\":1,\"ok\":true,\"from\":\"socket\",\"line\":\"record \\\"two words\\\"\","
               "\"result\":{}}\n");
}

RB_TEST(command_run, ids_count_commands_but_not_blank_lines) {
    setup();
    RB_EXPECT(run("") == NULL);
    RB_EXPECT(run("   # nothing") == NULL);
    RB_EXPECT(strncmp(run("record"), "{\"id\":1,", 8) == 0);
    RB_EXPECT(strncmp(run("nope"), "{\"id\":2,", 8) == 0);
    RB_EXPECT(strncmp(run("record"), "{\"id\":3,", 8) == 0);
}

RB_TEST(command_run, writes_every_kind_of_field) {
    setup();
    RB_EXPECT_EQ_STR(run("fields"),
                     "{\"id\":1,\"ok\":true,\"from\":\"test\",\"line\":\"fields\",\"result\":{"
                     "\"yes\":true,\"neg\":-5,\"big\":18446744073709551615,\"tenth\":0.1,"
                     "\"half\":0.5,\"nan\":null,\"quote\":\"a\\\"b\\\\c\\n\","
                     "\"inner\":{\"x\":1,\"deeper\":{}},\"after\":2}}\n");
}

RB_TEST(command_run, an_error_drops_the_fields_and_the_first_one_wins) {
    setup();
    RB_EXPECT_EQ_STR(run("fail"), "{\"id\":1,\"ok\":false,\"from\":\"test\",\"line\":\"fail\","
                                  "\"error\":\"first 1\"}\n");
}

RB_TEST(command_run, unknown_commands_and_wrong_counts_fail_without_a_call) {
    setup();
    RB_EXPECT(strstr(run("stepp 1"), "\"error\":\"unknown command \\\"stepp\\\" (help lists "
                                     "them)\"") != NULL);
    RB_EXPECT(strstr(run("record 1 2 3"),
                     "\"error\":\"record takes 0 to 2 arguments: record [a] [b]\"") != NULL);
    RB_EXPECT(strstr(run("numbers 1"),
                     "\"error\":\"numbers takes 3 arguments: numbers <a> <b> <c>\"") != NULL);
    RB_EXPECT(strstr(run("record \"open"), "\"error\":\"a quote is not closed\"") != NULL);
    RB_EXPECT_EQ_UINT(seen.calls, 0);
}

RB_TEST(command_run, number_arguments) {
    setup();
    RB_EXPECT(strstr(run("numbers 4294967295 18446744073709551615 -2.5"),
                     "{\"a\":4294967295,\"b\":18446744073709551615,\"c\":-2.5}") != NULL);
    RB_EXPECT(strstr(run("numbers 4294967296 0 0"),
                     "argument 1 of numbers: \\\"4294967296\\\" is not a whole number") != NULL);
    RB_EXPECT(strstr(run("numbers -1 0 0"), "argument 1 of numbers") != NULL);
    RB_EXPECT(strstr(run("numbers 1 0x10 0"), "argument 2 of numbers") != NULL);
    RB_EXPECT(strstr(run("numbers 1 18446744073709551616 0"), "argument 2 of numbers") != NULL);
    RB_EXPECT(strstr(run("numbers 1 2 nan"), "argument 3 of numbers: \\\"nan\\\" is not a finite "
                                             "number") != NULL);
    RB_EXPECT(strstr(run("numbers 1 2 1e99"), "argument 3 of numbers") != NULL);
    RB_EXPECT(strstr(run("numbers 1 2 3x"), "argument 3 of numbers") != NULL);
}

RB_TEST(command_run, a_reply_too_long_becomes_an_error) {
    setup();
    RB_EXPECT(strstr(run("huge"), "\"ok\":false") != NULL);
    RB_EXPECT(strstr(run("huge"), "\"error\":\"reply longer than 16384 bytes\"") != NULL);
}

RB_TEST(command_run, a_line_too_long_is_not_run) {
    setup();
    static char line[RB_COMMAND_LINE_MAX + 2];
    memset(line, 'a', sizeof(line) - 1);
    memcpy(line, "record ", 7);
    const char *reply = run(line);
    RB_EXPECT_EQ_UINT(seen.calls, 0);
    RB_EXPECT_EQ_STR(reply, "{\"id\":1,\"ok\":false,\"from\":\"test\",\"line\":\"(line too long)\","
                            "\"error\":\"line longer than 1023 bytes\"}\n");
    /* Exactly the limit is fine. */
    line[RB_COMMAND_LINE_MAX] = '\0';
    RB_EXPECT(strstr(run(line), "\"ok\":true") != NULL);
}
