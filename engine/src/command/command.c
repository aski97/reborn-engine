/*
 * Command registry, line splitting and replies (ADR 0022). Single-threaded:
 * commands run on the main thread, one at a time, sharing one reply buffer.
 */
#include "command/command_internal.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Line splitting
 * ========================================================================== */

static bool split_is_space(char c) {
    return c == ' ' || c == '\t';
}

/*
 * One pass with a read and a write position in the same buffer: removing
 * quotes and escapes only shortens a word, so writes never overtake reads.
 */
rb_command_split_result rb_command_split(char *line, char **words, u32 capacity, u32 *count) {
    RB_ASSERT(line != NULL && words != NULL && count != NULL);
    *count = 0;
    char *read = line;
    char *write = line;
    for (;;) {
        while (split_is_space(*read)) {
            ++read;
        }
        if (*read == '\0' || *read == '#') {
            break;
        }
        if (*count == capacity) {
            return RB_COMMAND_SPLIT_TOO_MANY_WORDS;
        }
        words[(*count)++] = write;
        bool quoted = false;
        while (*read != '\0' && (quoted || !split_is_space(*read))) {
            if (*read == '"') {
                quoted = !quoted;
                ++read;
            } else if (quoted && *read == '\\' && (read[1] == '"' || read[1] == '\\')) {
                *write++ = read[1];
                read += 2;
            } else {
                *write++ = *read++;
            }
        }
        if (quoted) {
            return RB_COMMAND_SPLIT_UNTERMINATED_QUOTE;
        }
        /* The separator after the word: overwritten by its NUL, so step past it first. */
        bool more = *read != '\0';
        if (more) {
            ++read;
        }
        *write++ = '\0';
        if (!more) {
            break;
        }
    }
    return *count == 0 ? RB_COMMAND_SPLIT_EMPTY : RB_COMMAND_SPLIT_OK;
}

/* ============================================================================
 * Registry
 * ========================================================================== */

void rb_commands_init(rb_commands *commands) {
    RB_ASSERT(commands != NULL);
    memset(commands, 0, sizeof(*commands));
}

static bool commands_name_is_valid(const char *name, bool needs_prefix) {
    usize length = strlen(name);
    if (length == 0 || length > RB_COMMAND_NAME_MAX) {
        return false;
    }
    bool has_dot = false;
    for (const char *c = name; *c != '\0'; ++c) {
        bool lower = *c >= 'a' && *c <= 'z';
        bool digit = *c >= '0' && *c <= '9';
        if (!lower && !digit && *c != '_' && *c != '.') {
            return false;
        }
        has_dot = has_dot || *c == '.';
    }
    /* A dot at either end would leave an empty prefix or an empty name. */
    if (name[0] == '.' || name[length - 1] == '.') {
        return false;
    }
    return !needs_prefix || has_dot;
}

bool rb_commands_add(rb_commands *commands, const rb_command_desc *desc, bool needs_prefix) {
    RB_ASSERT(commands != NULL && desc != NULL);
    RB_ASSERT(desc->name != NULL && desc->fn != NULL);
    if (!commands_name_is_valid(desc->name, needs_prefix)) {
        RB_LOG_ERROR("Command \"%s\": names are lowercase letters, digits, '_' and '.', at most %u "
                     "characters%s",
                     desc->name, RB_COMMAND_NAME_MAX,
                     needs_prefix ? ", with the game's prefix (\"game.name\")" : "");
        return false;
    }
    if (desc->min_args > desc->max_args || desc->max_args > RB_COMMAND_MAX_WORDS - 1) {
        RB_LOG_ERROR("Command \"%s\": arguments %u..%u, at most %u", desc->name, desc->min_args,
                     desc->max_args, RB_COMMAND_MAX_WORDS - 1);
        return false;
    }
    if (rb_commands_find(commands, desc->name) != NULL) {
        RB_LOG_ERROR("Command \"%s\" is already registered", desc->name);
        return false;
    }
    if (commands->count == RB_COMMAND_MAX_COMMANDS) {
        RB_LOG_ERROR("Command \"%s\": the table is full (%u commands)", desc->name,
                     RB_COMMAND_MAX_COMMANDS);
        return false;
    }
    commands->table[commands->count++] = *desc;
    return true;
}

/* A linear scan: a few dozen names, one lookup per command typed (ADR 0022, F1). */
const rb_command_desc *rb_commands_find(const rb_commands *commands, const char *name) {
    RB_ASSERT(commands != NULL && name != NULL);
    for (u32 i = 0; i < commands->count; ++i) {
        if (strcmp(commands->table[i].name, name) == 0) {
            return &commands->table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Reply
 * ========================================================================== */

static void reply_reset(rb_command_reply *reply, const char *command) {
    reply->command = command;
    rb_text_writer_init(&reply->body, reply->body_data, sizeof(reply->body_data));
    reply->depth = 0;
    memset(reply->needs_comma, 0, sizeof(reply->needs_comma));
    reply->failed = false;
    reply->error[0] = '\0';
}

static RB_PRINTF_FORMAT(2, 0) void reply_verror(rb_command_reply *reply, const char *fmt,
                                                va_list args) {
    if (reply->failed) {
        return;
    }
    reply->failed = true;
    vsnprintf(reply->error, sizeof(reply->error), fmt, args);
}

static void reply_fail(rb_command_reply *reply, const char *fmt, ...) RB_PRINTF_FORMAT(2, 3);
static void reply_fail(rb_command_reply *reply, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    reply_verror(reply, fmt, args);
    va_end(args);
}

void rb_command_reply_error(rb_command_reply *reply, const char *fmt, ...) {
    RB_ASSERT(reply != NULL && fmt != NULL);
    va_list args;
    va_start(args, fmt);
    reply_verror(reply, fmt, args);
    va_end(args);
}

/* The key and its colon, after a comma when the object has fields already. False once failed. */
static bool reply_key(rb_command_reply *reply, const char *key) {
    RB_ASSERT(key != NULL);
    if (reply->failed) {
        return false;
    }
    if (reply->needs_comma[reply->depth]) {
        rb_text_writer_bytes(&reply->body, ",", 1);
    }
    reply->needs_comma[reply->depth] = true;
    rb_text_writer_json_string(&reply->body, key, false);
    rb_text_writer_bytes(&reply->body, ":", 1);
    return true;
}

void rb_command_reply_bool(rb_command_reply *reply, const char *key, bool value) {
    RB_ASSERT(reply != NULL);
    if (reply_key(reply, key)) {
        rb_text_writer_str(&reply->body, value ? "true" : "false");
    }
}

void rb_command_reply_i64(rb_command_reply *reply, const char *key, i64 value) {
    RB_ASSERT(reply != NULL);
    if (reply_key(reply, key)) {
        rb_text_writer_fmt(&reply->body, "%" PRId64, value);
    }
}

void rb_command_reply_u64(rb_command_reply *reply, const char *key, u64 value) {
    RB_ASSERT(reply != NULL);
    if (reply_key(reply, key)) {
        rb_text_writer_fmt(&reply->body, "%" PRIu64, value);
    }
}

/*
 * Tries 1, 2, ... significant digits until the text reads back as `value`:
 * 9 always suffice for a float, 17 for a double. %g never writes a form JSON
 * refuses for a finite value ("1e+20" is valid JSON).
 */
static void reply_number(rb_command_reply *reply, f64 value, bool is_f32) {
    if (!isfinite(value)) {
        rb_text_writer_str(&reply->body, "null");
        return;
    }
    char text[32];
    int  max_digits = is_f32 ? 9 : 17;
    for (int digits = 1; digits <= max_digits; ++digits) {
        snprintf(text, sizeof(text), "%.*g", digits, value);
        bool same = is_f32 ? strtof(text, NULL) == (f32)value : strtod(text, NULL) == value;
        if (same) {
            break;
        }
    }
    rb_text_writer_str(&reply->body, text);
}

void rb_command_reply_f32(rb_command_reply *reply, const char *key, f32 value) {
    RB_ASSERT(reply != NULL);
    if (reply_key(reply, key)) {
        reply_number(reply, (f64)value, true);
    }
}

void rb_command_reply_f64(rb_command_reply *reply, const char *key, f64 value) {
    RB_ASSERT(reply != NULL);
    if (reply_key(reply, key)) {
        reply_number(reply, value, false);
    }
}

void rb_command_reply_str(rb_command_reply *reply, const char *key, const char *value) {
    RB_ASSERT(reply != NULL && value != NULL);
    if (reply_key(reply, key)) {
        rb_text_writer_json_string(&reply->body, value, false);
    }
}

void rb_command_reply_begin_object(rb_command_reply *reply, const char *key) {
    RB_ASSERT(reply != NULL);
    RB_ASSERT(reply->depth < RB_COMMAND_REPLY_MAX_DEPTH);
    if (reply->depth == RB_COMMAND_REPLY_MAX_DEPTH) {
        reply_fail(reply, "reply nested deeper than %u objects", RB_COMMAND_REPLY_MAX_DEPTH);
        return;
    }
    if (reply_key(reply, key)) {
        rb_text_writer_bytes(&reply->body, "{", 1);
        ++reply->depth;
        reply->needs_comma[reply->depth] = false;
    }
}

void rb_command_reply_end_object(rb_command_reply *reply) {
    RB_ASSERT(reply != NULL);
    RB_ASSERT(reply->depth > 0);
    if (reply->failed || reply->depth == 0) {
        return;
    }
    rb_text_writer_bytes(&reply->body, "}", 1);
    --reply->depth;
}

/* ============================================================================
 * Arguments
 * ========================================================================== */

/* Digits only: strtoull would accept a sign, spaces and "0x". */
static bool arg_parse_unsigned(const char *text, u64 max, u64 *out) {
    if (*text == '\0') {
        return false;
    }
    u64 value = 0;
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        u64 digit = (u64)(*c - '0');
        if (value > (max - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

bool rb_command_arg_u32(const rb_command_args *args, u32 index, u32 *out, rb_command_reply *reply) {
    RB_ASSERT(args != NULL && out != NULL && reply != NULL);
    RB_ASSERT(index < args->count);
    u64 value;
    if (!arg_parse_unsigned(args->values[index], UINT32_MAX, &value)) {
        reply_fail(reply, "argument %u of %s: \"%s\" is not a whole number from 0 to %" PRIu32,
                   index + 1, reply->command, args->values[index], UINT32_MAX);
        return false;
    }
    *out = (u32)value;
    return true;
}

bool rb_command_arg_u64(const rb_command_args *args, u32 index, u64 *out, rb_command_reply *reply) {
    RB_ASSERT(args != NULL && out != NULL && reply != NULL);
    RB_ASSERT(index < args->count);
    if (!arg_parse_unsigned(args->values[index], UINT64_MAX, out)) {
        reply_fail(reply, "argument %u of %s: \"%s\" is not a whole number from 0 to %" PRIu64,
                   index + 1, reply->command, args->values[index], UINT64_MAX);
        return false;
    }
    return true;
}

bool rb_command_arg_f32(const rb_command_args *args, u32 index, f32 *out, rb_command_reply *reply) {
    RB_ASSERT(args != NULL && out != NULL && reply != NULL);
    RB_ASSERT(index < args->count);
    const char *text = args->values[index];
    char       *end = NULL;
    errno = 0;
    /* The C locale, which nothing in the engine changes: '.' as the decimal point. */
    f32 value = strtof(text, &end);
    if (*text == '\0' || *end != '\0' || errno == ERANGE || !isfinite(value)) {
        reply_fail(reply, "argument %u of %s: \"%s\" is not a finite number", index + 1,
                   reply->command, text);
        return false;
    }
    *out = value;
    return true;
}

/* ============================================================================
 * Running a line
 * ========================================================================== */

static void run_check_and_call(rb_commands *commands, char **words, u32 count, void *state,
                               rb_app *app) {
    rb_command_reply      *reply = &commands->reply;
    const rb_command_desc *desc = rb_commands_find(commands, words[0]);
    if (desc == NULL) {
        reply_fail(reply, "unknown command \"%s\" (help lists them)", words[0]);
        return;
    }
    u32 arg_count = count - 1;
    if (arg_count < desc->min_args || arg_count > desc->max_args) {
        const char *usage = desc->usage != NULL ? desc->usage : "";
        if (desc->min_args == desc->max_args) {
            reply_fail(reply, "%s takes %u argument%s: %s %s", desc->name, desc->min_args,
                       desc->min_args == 1 ? "" : "s", desc->name, usage);
        } else {
            reply_fail(reply, "%s takes %u to %u arguments: %s %s", desc->name, desc->min_args,
                       desc->max_args, desc->name, usage);
        }
        return;
    }
    rb_command_args args = {arg_count, (const char *const *)(words + 1)};
    desc->fn(state, app, &args, reply);
    /* A handler that forgot an end_object: a bug (asserted), closed anyway in release builds. */
    RB_ASSERT(reply->depth == 0);
    while (!reply->failed && reply->depth > 0) {
        rb_command_reply_end_object(reply);
    }
}

/* The line, as the caller gave it, into its JSON reply. False if it does not fit. */
static bool run_format(rb_commands *commands, const char *line, const char *from) {
    rb_command_reply *reply = &commands->reply;
    rb_text_writer    w;
    rb_text_writer_init(&w, commands->out, sizeof(commands->out));
    /* id and ok first, always in this order: clients check the outcome without parsing JSON. */
    rb_text_writer_fmt(&w, "{\"id\":%" PRIu64 ",\"ok\":%s,\"from\":", commands->last_id,
                       reply->failed ? "false" : "true");
    rb_text_writer_json_string(&w, from, false);
    rb_text_writer_str(&w, ",\"line\":");
    rb_text_writer_json_string(&w, line, false);
    if (reply->failed) {
        rb_text_writer_str(&w, ",\"error\":");
        rb_text_writer_json_string(&w, reply->error, false);
    } else {
        rb_text_writer_str(&w, ",\"result\":{");
        rb_text_writer_bytes(&w, reply->body.data, reply->body.length);
        rb_text_writer_str(&w, "}");
    }
    rb_text_writer_str(&w, "}\n");
    return !w.overflow && !reply->body.overflow;
}

/* The reply already filled in (or failed): formatted, logged. */
static const char *run_finish(rb_commands *commands, const char *line, const char *from) {
    rb_command_reply *reply = &commands->reply;
    if (!run_format(commands, line, from)) {
        reply_reset(reply, reply->command);
        reply_fail(reply, "reply longer than %u bytes", RB_COMMAND_REPLY_MAX);
        run_format(commands, line, from);
    }
    if (reply->failed) {
        RB_LOG_WARN("Command %" PRIu64 " failed: %s", commands->last_id, reply->error);
    }
    return commands->out;
}

const char *rb_commands_run(rb_commands *commands, const char *line, const char *from, void *state,
                            rb_app *app) {
    RB_ASSERT(commands != NULL && line != NULL && from != NULL);
    usize length = strlen(line);
    if (length > RB_COMMAND_LINE_MAX) {
        return rb_commands_reject_long_line(commands, from);
    }

    rb_command_reply *reply = &commands->reply;
    char             *words[RB_COMMAND_MAX_WORDS];
    u32               count = 0;
    memcpy(commands->words_data, line, length + 1);
    rb_command_split_result split =
        rb_command_split(commands->words_data, words, RB_COMMAND_MAX_WORDS, &count);
    if (split == RB_COMMAND_SPLIT_EMPTY) {
        return NULL;
    }

    ++commands->last_id;
    RB_LOG_DEBUG("Command %" PRIu64 " from %s: %s", commands->last_id, from, line);
    reply_reset(reply, words[0]);
    if (split == RB_COMMAND_SPLIT_UNTERMINATED_QUOTE) {
        reply_fail(reply, "a quote is not closed");
    } else if (split == RB_COMMAND_SPLIT_TOO_MANY_WORDS) {
        reply_fail(reply, "more than %u words", RB_COMMAND_MAX_WORDS);
    } else {
        run_check_and_call(commands, words, count, state, app);
    }
    return run_finish(commands, line, from);
}

const char *rb_commands_reject_long_line(rb_commands *commands, const char *from) {
    RB_ASSERT(commands != NULL && from != NULL);
    ++commands->last_id;
    reply_reset(&commands->reply, "");
    reply_fail(&commands->reply, "line longer than %u bytes", RB_COMMAND_LINE_MAX);
    return run_finish(commands, "(line too long)", from);
}
