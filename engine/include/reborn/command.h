/*
 * Commands (ADR 0022): named actions with arguments, run by the engine from a
 * script given at startup (--script) or from a local connection (--listen),
 * each answering with one JSON line. The engine has its own (help, status,
 * pause, resume, step, quit, wait); a game registers more in init:
 *
 *   static void cmd_score(void *state, rb_app *app, const rb_command_args *args,
 *                         rb_command_reply *reply) {
 *       my_game *game = state;
 *       rb_command_reply_u64(reply, "score", game->score);
 *   }
 *   static void cmd_set_score(void *state, rb_app *app, const rb_command_args *args,
 *                             rb_command_reply *reply) {
 *       my_game *game = state;
 *       u32 score;
 *       if (!rb_command_arg_u32(args, 0, &score, reply)) return;   // the reply holds the error
 *       game->score = score;
 *   }
 *
 *   rb_app_register_command(app, &(rb_command_desc){
 *       .name = "game.score", .summary = "the current score", .fn = cmd_score});
 *   rb_app_register_command(app, &(rb_command_desc){
 *       .name = "game.set_score", .usage = "<score>", .summary = "change the score",
 *       .min_args = 1, .max_args = 1, .fn = cmd_set_score});
 *
 * "game.score" then replies
 * {"id":3,"ok":true,"from":"socket","line":"game.score","result":{"score":12}}.
 */
#ifndef RB_COMMAND_H
#define RB_COMMAND_H

#include <reborn/defines.h>

/* Declared again in reborn/app.h: repeating a typedef is allowed since C11. */
typedef struct rb_app rb_app;

/* Longest command line in bytes, the '\n' excluded. */
#define RB_COMMAND_LINE_MAX 1023u
/* Most words on a line, the command's name included. */
#define RB_COMMAND_MAX_WORDS 16u
/* Longest command name. */
#define RB_COMMAND_NAME_MAX 31u
/* Commands the engine and the game can register together. */
#define RB_COMMAND_MAX_COMMANDS 64u
/* Longest reply line in bytes, the '\n' included: a longer result becomes an error. */
#define RB_COMMAND_REPLY_MAX 16384u

/* The words after the command's name, as typed; quotes removed. */
typedef struct rb_command_args {
    u32                count;
    const char *const *values;
} rb_command_args;

/* Where a handler writes its result (or its error). Opaque: only through the functions below. */
typedef struct rb_command_reply rb_command_reply;

/*
 * Runs on the main thread, at the start of a frame, before its steps.
 * `state` is the game's, as in the other callbacks. Reply nothing and the
 * command succeeds with an empty result.
 */
typedef void (*rb_command_fn)(void *state, rb_app *app, const rb_command_args *args,
                              rb_command_reply *reply);

typedef struct rb_command_desc {
    /* Lowercase letters, digits, '_' and '.'; a game's carry its prefix: "blink.state". */
    const char   *name;
    const char   *usage;   /* the arguments, for help: "<count>"; NULL for none */
    const char   *summary; /* one line, for help */
    u32           min_args;
    u32           max_args; /* checked by the engine before the call */
    rb_command_fn fn;
} rb_command_desc;

/* ============================================================================
 * Arguments
 *
 * Each parses args->values[index] whole ("12x" is refused). On failure it
 * writes the error into `reply` and returns false: the handler just returns.
 * `index` must be below args->count (the engine checked min_args).
 * ========================================================================== */

RB_API bool rb_command_arg_u32(const rb_command_args *args, u32 index, u32 *out,
                               rb_command_reply *reply);
RB_API bool rb_command_arg_u64(const rb_command_args *args, u32 index, u64 *out,
                               rb_command_reply *reply);
/* A finite number: "nan" and "inf" are refused. */
RB_API bool rb_command_arg_f32(const rb_command_args *args, u32 index, f32 *out,
                               rb_command_reply *reply);

/* ============================================================================
 * Reply
 *
 * Fields of the "result" object, in the order written. Keys are plain text,
 * escaped by the engine. Every begin_object needs its end_object.
 * ========================================================================== */

RB_API void rb_command_reply_bool(rb_command_reply *reply, const char *key, bool value);
RB_API void rb_command_reply_i64(rb_command_reply *reply, const char *key, i64 value);
RB_API void rb_command_reply_u64(rb_command_reply *reply, const char *key, u64 value);
/*
 * The fewest digits that read back as the same number: 0.1f is "0.1", not
 * the "0.10000000149011612" of the float widened to a double. JSON has no NaN
 * or infinity: they are written as null.
 */
RB_API void rb_command_reply_f32(rb_command_reply *reply, const char *key, f32 value);
RB_API void rb_command_reply_f64(rb_command_reply *reply, const char *key, f64 value);
RB_API void rb_command_reply_str(rb_command_reply *reply, const char *key, const char *value);

/* A nested object under `key`, at most 8 levels deep; fields go into it until end_object. */
RB_API void rb_command_reply_begin_object(rb_command_reply *reply, const char *key);
RB_API void rb_command_reply_end_object(rb_command_reply *reply);

/* The command failed: "ok":false and this message instead of the result. The first error wins. */
RB_API void rb_command_reply_error(rb_command_reply *reply, const char *fmt, ...)
    RB_PRINTF_FORMAT(2, 3);

#endif /* RB_COMMAND_H */
