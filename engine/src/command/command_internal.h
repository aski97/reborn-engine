/*
 * Command system internals (ADR 0022): line splitting, the registry, running a
 * line into its JSON reply. No app, no socket, no file: the app feeds it lines
 * from the script and the server, tests feed it strings.
 */
#ifndef RB_COMMAND_INTERNAL_H
#define RB_COMMAND_INTERNAL_H

#include <reborn/command.h>
#include <reborn/memory.h>

#include "core/text_writer.h"

/* Nesting of begin_object, the result object itself excluded. */
#define RB_COMMAND_REPLY_MAX_DEPTH 8u
#define RB_COMMAND_ERROR_MAX       512u

/* ============================================================================
 * Line splitting
 * ========================================================================== */

typedef enum rb_command_split_result {
    RB_COMMAND_SPLIT_OK,
    RB_COMMAND_SPLIT_EMPTY,              /* only spaces or a comment: nothing to run */
    RB_COMMAND_SPLIT_UNTERMINATED_QUOTE, /* a '"' without its closing one */
    RB_COMMAND_SPLIT_TOO_MANY_WORDS,     /* more than `capacity` */
} rb_command_split_result;

/*
 * Splits `line` in place into words: separated by spaces and tabs; double
 * quotes group words, with \" and \\ inside them (any other '\' is kept, so
 * "C:\dir" stays a path); a '#' at the start of a word ends the line. Quotes
 * are removed: a"b c"d is one word, ab cd. `words` receives pointers into
 * `line`, `count` how many.
 */
rb_command_split_result rb_command_split(char *line, char **words, u32 capacity, u32 *count);

/* ============================================================================
 * Registry and replies
 * ========================================================================== */

struct rb_command_reply {
    const char    *command; /* name, for the argument helpers' messages */
    rb_text_writer body;    /* the fields of "result", without its braces */
    char           body_data[RB_COMMAND_REPLY_MAX];
    u32            depth;
    /* Per level: a field was written, the next one needs a comma. */
    bool needs_comma[RB_COMMAND_REPLY_MAX_DEPTH + 1];
    bool failed;
    char error[RB_COMMAND_ERROR_MAX];
};

/* Large (two reply buffers): from an arena, not on the stack. */
typedef struct rb_commands {
    rb_command_desc  table[RB_COMMAND_MAX_COMMANDS];
    u32              count;
    u64              last_id; /* of the last command run; 0 before the first */
    rb_command_reply reply;
    char             words_data[RB_COMMAND_LINE_MAX + 1]; /* the line being split */
    char             out[RB_COMMAND_REPLY_MAX];
} rb_commands;

void rb_commands_init(rb_commands *commands);

/*
 * False, logged, for a bad name (lowercase letters, digits, '_', '.', at most
 * RB_COMMAND_NAME_MAX), a name already taken, min_args above max_args or above
 * RB_COMMAND_MAX_WORDS - 1, or a full table. With `needs_prefix` (games' commands)
 * the name must contain a '.'. NULL desc, name or fn: RB_ASSERT.
 */
bool rb_commands_add(rb_commands *commands, const rb_command_desc *desc, bool needs_prefix);

/* NULL when no command has that name. */
const rb_command_desc *rb_commands_find(const rb_commands *commands, const char *name);

/*
 * Runs one line: split, find, check the argument count, call. Returns the
 * reply, one JSON line ending in '\n', in commands->out; NULL for an empty
 * line or a comment, which gets no reply and no id. `from` is "socket" or
 * "script:<n>". Logged: the line at DEBUG, a failure at WARN.
 */
const char *rb_commands_run(rb_commands *commands, const char *line, const char *from, void *state,
                            rb_app *app);

/*
 * The reply to a line longer than RB_COMMAND_LINE_MAX, which is not run: for
 * the server, which never holds such a line whole. Takes an id like any other.
 */
const char *rb_commands_reject_long_line(rb_commands *commands, const char *from);

/* ============================================================================
 * Script (--script)
 * ========================================================================== */

typedef struct rb_command_script {
    char *text; /* modified in place: each '\n' becomes a NUL as lines are read */
    usize size;
    usize position;
    u32   line_number; /* of the last line returned, from 1 */
} rb_command_script;

/* Over the text in memory, which it owns until the end: `size` bytes, NUL at text[size]. */
void rb_command_script_init(rb_command_script *script, char *text, usize size);

/* Biggest script file accepted: more is surely not a script. */
#define RB_COMMAND_SCRIPT_MAX RB_MIB(1)

/*
 * Reads the whole file into `arena`, where it stays until the arena goes.
 * False, logged, when it cannot be read or is larger than RB_COMMAND_SCRIPT_MAX.
 */
bool rb_command_script_load(rb_command_script *script, rb_arena *arena, const char *path);

/* The next line, '\r' before the '\n' dropped; false at the end. */
bool rb_command_script_next(rb_command_script *script, const char **line);

#endif /* RB_COMMAND_INTERNAL_H */
