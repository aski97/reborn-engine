/* Command scripts (ADR 0022): lines out of a text, loaded from a file. */
#include "rb_test.h"

#include <stdio.h>
#include <string.h>

#include "command/command_internal.h"

static char text[128];

static rb_command_script script_of(const char *content) {
    strcpy(text, content);
    rb_command_script script;
    rb_command_script_init(&script, text, strlen(text));
    return script;
}

RB_TEST(command_script, lines_with_unix_or_windows_endings) {
    rb_command_script script = script_of("pause\r\nstep 2\n\nquit");
    const char       *line = NULL;
    RB_REQUIRE(rb_command_script_next(&script, &line));
    RB_EXPECT_EQ_STR(line, "pause");
    RB_EXPECT_EQ_UINT(script.line_number, 1);
    RB_REQUIRE(rb_command_script_next(&script, &line));
    RB_EXPECT_EQ_STR(line, "step 2");
    RB_REQUIRE(rb_command_script_next(&script, &line));
    RB_EXPECT_EQ_STR(line, "");
    RB_REQUIRE(rb_command_script_next(&script, &line));
    RB_EXPECT_EQ_STR(line, "quit");
    RB_EXPECT_EQ_UINT(script.line_number, 4);
    RB_EXPECT(!rb_command_script_next(&script, &line));
}

RB_TEST(command_script, a_final_newline_adds_no_line) {
    rb_command_script script = script_of("a\n");
    const char       *line = NULL;
    RB_EXPECT(rb_command_script_next(&script, &line));
    RB_EXPECT(!rb_command_script_next(&script, &line));

    script = script_of("");
    RB_EXPECT(!rb_command_script_next(&script, &line));
}

#define TEST_SCRIPT_FILE "test_command_script.txt"

RB_TEST(command_script, loads_a_file_into_the_arena) {
    FILE *file = fopen(TEST_SCRIPT_FILE, "wb");
    RB_REQUIRE(file != NULL);
    fputs("status\r\nquit\r\n", file);
    fclose(file);

    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(4)));
    rb_command_script script;
    RB_EXPECT(rb_command_script_load(&script, &arena, TEST_SCRIPT_FILE));
    const char *line = NULL;
    RB_EXPECT(rb_command_script_next(&script, &line) && strcmp(line, "status") == 0);
    RB_EXPECT(rb_command_script_next(&script, &line) && strcmp(line, "quit") == 0);
    RB_EXPECT(!rb_command_script_next(&script, &line));
    rb_arena_destroy(&arena);
    remove(TEST_SCRIPT_FILE);
}

RB_TEST(command_script, a_missing_file_fails) {
    rb_arena arena;
    RB_REQUIRE(rb_arena_create(&arena, RB_MIB(4)));
    rb_command_script script;
    RB_EXPECT(!rb_command_script_load(&script, &arena, "no/such/script.txt"));
    rb_arena_destroy(&arena);
}
