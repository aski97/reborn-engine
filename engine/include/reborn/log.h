/*
 * Logging (ADR 0008).
 *
 *   rb_log_startup(&(rb_log_config){.console_level = RB_LOG_LEVEL_INFO,
 *                                   .file_path = "reborn.log.jsonl"});
 *   RB_LOG_INFO("Window created: %ux%u", width, height);
 *   rb_log_shutdown();
 *
 * The logger is one global instance: it must work everywhere, before any
 * system starts and inside OS callbacks. With no output registered (before
 * startup, after shutdown) messages go to stderr, so nothing is ever lost.
 * Not thread-safe yet.
 */
#ifndef RB_LOG_H
#define RB_LOG_H

#include <reborn/defines.h>

/* ============================================================================
 * Levels
 * ========================================================================== */

typedef enum rb_log_level {
    RB_LOG_LEVEL_TRACE = 0, /* step-by-step detail, very noisy */
    RB_LOG_LEVEL_DEBUG,     /* useful while developing */
    RB_LOG_LEVEL_INFO,      /* milestones: startup, window created */
    RB_LOG_LEVEL_WARN,      /* unexpected but handled */
    RB_LOG_LEVEL_ERROR,     /* an operation failed */
    RB_LOG_LEVEL_FATAL,     /* the engine cannot go on; does not stop it by itself */
    RB_LOG_LEVEL_COUNT
} rb_log_level;

/* "TRACE", "DEBUG", ... ; "?" for an out-of-range value. */
RB_API const char *rb_log_level_name(rb_log_level level);

/* ============================================================================
 * Records and outputs
 * ========================================================================== */

/* Longer messages are cut at a UTF-8 character boundary and end with "...". */
#define RB_LOG_MESSAGE_MAX 2048u

/* One log call, as every output receives it. Pointers are valid only during the call. */
typedef struct rb_log_record {
    rb_log_level level;
    f64          time;    /* seconds since the logger was first used */
    i64          unix_ms; /* wall clock: milliseconds since 1970-01-01 UTC */
    const char  *file;    /* relative to the repository root when built by its CMake */
    u32          line;
    const char  *func;
    const char  *message; /* formatted UTF-8, NUL-terminated */
} rb_log_record;

/*
 * An output ("sink"). `user` is the pointer given at registration: the sink's
 * own data, the role `this` plays in a method.
 */
typedef void (*rb_log_sink_fn)(const rb_log_record *record, void *user);

#define RB_LOG_MAX_SINKS 8u

/* Receives records at `min_level` and above. False when all slots are taken. */
RB_API bool rb_log_add_sink(rb_log_sink_fn fn, void *user, rb_log_level min_level);

/* Removes the sink registered with this exact pair. Unknown pairs are ignored. */
RB_API void rb_log_remove_sink(rb_log_sink_fn fn, void *user);

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

/* A zero-initialized config logs everything to the console and writes no file. */
typedef struct rb_log_config {
    rb_log_level console_level;
    const char  *file_path; /* JSON lines, overwritten; NULL: no file */
    rb_log_level file_level;
} rb_log_config;

/*
 * Adds the console and file outputs, then logs the local date and time at
 * which `time` was 0: every record's absolute time is that plus its `time`.
 * False if already started or the file cannot be opened: the console output
 * is added anyway, so the reason gets logged.
 */
RB_API bool rb_log_startup(const rb_log_config *config);

/* Removes the outputs added by startup and closes the file. Other sinks stay. */
RB_API void rb_log_shutdown(void);

/* ============================================================================
 * Logging
 * ========================================================================== */

/* Use the RB_LOG_* macros: they fill in file, line and function. */
RB_API void rb_log_write(rb_log_level level, const char *file, u32 line, const char *func,
                         const char *fmt, ...) RB_PRINTF_FORMAT(5, 6);

/*
 * Calls below this level are removed at compile time: their arguments are not
 * even evaluated, so `RB_LOG_DEBUG("%d", i++)` does not increment `i` in
 * release. Define it before including this header to override.
 */
#ifndef RB_LOG_MIN_LEVEL
    #if defined(RB_DEBUG)
        #define RB_LOG_MIN_LEVEL RB_LOG_LEVEL_TRACE
    #else
        #define RB_LOG_MIN_LEVEL RB_LOG_LEVEL_INFO
    #endif
#endif

/*
 * The format string is part of `...`: with `(fmt, ...)`, RB_LOG_INFO("text")
 * would leave `...` empty, which C17 forbids (-Wpedantic). The `if` on two
 * constants is folded away by the compiler, but the arguments are still
 * type-checked and count as used, so no "unused variable" warning in release.
 */
#define RB_LOG_AT_(level, ...)                                                                     \
    do {                                                                                           \
        if ((level) >= RB_LOG_MIN_LEVEL) {                                                         \
            rb_log_write((level), __FILE__, __LINE__, __func__, __VA_ARGS__);                      \
        }                                                                                          \
    } while (0)

#define RB_LOG_TRACE(...) RB_LOG_AT_(RB_LOG_LEVEL_TRACE, __VA_ARGS__)
#define RB_LOG_DEBUG(...) RB_LOG_AT_(RB_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define RB_LOG_INFO(...)  RB_LOG_AT_(RB_LOG_LEVEL_INFO, __VA_ARGS__)
#define RB_LOG_WARN(...)  RB_LOG_AT_(RB_LOG_LEVEL_WARN, __VA_ARGS__)
#define RB_LOG_ERROR(...) RB_LOG_AT_(RB_LOG_LEVEL_ERROR, __VA_ARGS__)
#define RB_LOG_FATAL(...) RB_LOG_AT_(RB_LOG_LEVEL_FATAL, __VA_ARGS__)

#endif /* RB_LOG_H */
