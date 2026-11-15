/*
 * Logger (ADR 0008).
 *
 * The state is one file-local global, zero-initialized before main(): a log
 * call works at any time, with no setup. Every record is formatted once into
 * a static buffer and handed to each registered sink; with no sink, it goes
 * straight to the console. Single-threaded: the buffers are shared.
 */
#include <reborn/log.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/log_internal.h"
#include "platform/platform_os.h"

/* ============================================================================
 * State
 * ========================================================================== */

typedef struct log_sink {
    rb_log_sink_fn fn;
    void          *user;
    rb_log_level   min_level;
} log_sink;

typedef struct log_state {
    log_sink sinks[RB_LOG_MAX_SINKS];
    u32      sink_count;

    bool  started;
    FILE *file; /* JSON lines output added by startup; NULL if none */

    /* The clock starts at the first log call, not at startup: earlier calls need a time too. */
    bool            clock_started;
    struct timespec clock_start;

    /* The console is prepared lazily, by the first line it prints. */
    bool console_ready;
    bool console_color;

    /*
     * Set while a record is delivered. A sink that logs would overwrite the
     * shared buffers and recurse without end, so such calls are dropped.
     */
    bool writing;

    char message[RB_LOG_MESSAGE_MAX];
    char line[RB_LOG_LINE_MAX];
} log_state;

static log_state g_log;

/* ============================================================================
 * Levels
 * ========================================================================== */

static const char *const LOG_LEVEL_NAMES[RB_LOG_LEVEL_COUNT] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL",
};

/* ANSI SGR codes: gray, cyan, green, yellow, red, bright white on red. */
static const char *const LOG_LEVEL_COLORS[RB_LOG_LEVEL_COUNT] = {
    "\x1b[90m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[97;41m",
};

#define LOG_COLOR_RESET "\x1b[0m"
#define LOG_COLOR_DIM   "\x1b[90m"

const char *rb_log_level_name(rb_log_level level) {
    /* The cast also rejects negative values: an enum may hold any int. */
    return (u32)level < RB_LOG_LEVEL_COUNT ? LOG_LEVEL_NAMES[level] : "?";
}

/* ============================================================================
 * Line writer
 * ========================================================================== */

/*
 * Appends to a fixed buffer, always NUL-terminated. When the text does not
 * fit, it keeps what does and sets `overflow`, so callers check once at the end
 * instead of after every append.
 */
typedef struct log_writer {
    char *data;
    usize capacity; /* at least 1: the NUL */
    usize length;
    bool  overflow;
} log_writer;

static void writer_bytes(log_writer *w, const char *bytes, usize count) {
    usize room = w->capacity - 1 - w->length;
    if (count > room) {
        count = room;
        w->overflow = true;
    }
    memcpy(w->data + w->length, bytes, count);
    w->length += count;
    w->data[w->length] = '\0';
}

static void writer_str(log_writer *w, const char *text) {
    writer_bytes(w, text, strlen(text));
}

static RB_PRINTF_FORMAT(2, 3) void writer_fmt(log_writer *w, const char *fmt, ...) {
    usize   room = w->capacity - 1 - w->length;
    va_list args;
    va_start(args, fmt);
    /* vsnprintf returns the length the full text needs, even when it cuts. */
    int needed = vsnprintf(w->data + w->length, room + 1, fmt, args);
    va_end(args);
    if (needed < 0) {
        w->data[w->length] = '\0';
        return;
    }
    if ((usize)needed > room) {
        w->length += room;
        w->overflow = true;
    } else {
        w->length += (usize)needed;
    }
}

/*
 * __FILE__ is the path the compiler was given: on Windows it uses '\'.
 * Logs show '/' on every OS, so the same file reads the same everywhere.
 */
static void writer_path(log_writer *w, const char *path) {
    for (const char *c = path; *c != '\0'; ++c) {
        char ch = *c == '\\' ? '/' : *c;
        writer_bytes(w, &ch, 1);
    }
}

/*
 * A JSON string: '"' and '\' must be escaped, and so must control characters
 * (below 0x20), which JSON forbids raw. Bytes from 0x80 up are UTF-8 and pass
 * through: JSON text is UTF-8.
 */
static void writer_json_string(log_writer *w, const char *text, bool is_path) {
    writer_bytes(w, "\"", 1);
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0'; ++c) {
        unsigned char ch = (is_path && *c == '\\') ? '/' : *c;
        switch (ch) {
        case '"':
            writer_str(w, "\\\"");
            break;
        case '\\':
            writer_str(w, "\\\\");
            break;
        case '\n':
            writer_str(w, "\\n");
            break;
        case '\r':
            writer_str(w, "\\r");
            break;
        case '\t':
            writer_str(w, "\\t");
            break;
        default:
            if (ch < 0x20) {
                writer_fmt(w, "\\u%04x", (unsigned)ch);
            } else {
                char byte = (char)ch;
                writer_bytes(w, &byte, 1);
            }
            break;
        }
    }
    writer_bytes(w, "\"", 1);
}

/* ============================================================================
 * Formats
 * ========================================================================== */

usize rb_log_format_console(const rb_log_record *record, bool color, char *out, usize capacity) {
    if (capacity < 2) {
        if (capacity == 1) {
            out[0] = '\0';
        }
        return 0;
    }
    log_writer  w = {out, capacity, 0, false};
    const char *name = rb_log_level_name(record->level);
    const char *level_color =
        (u32)record->level < RB_LOG_LEVEL_COUNT ? LOG_LEVEL_COLORS[record->level] : LOG_COLOR_RESET;
    out[0] = '\0';

    /* %-5s pads every level name to the width of the longest, so messages line up. */
    if (color) {
        writer_fmt(&w, LOG_COLOR_DIM "%9.3f" LOG_COLOR_RESET " %s%-5s" LOG_COLOR_RESET " ",
                   record->time, level_color, name);
    } else {
        writer_fmt(&w, "%9.3f %-5s ", record->time, name);
    }
    writer_str(&w, record->message);

    /* Below WARN the location is noise on a console; the JSON file always has it. */
    if (record->level >= RB_LOG_LEVEL_WARN) {
        writer_str(&w, color ? "  " LOG_COLOR_DIM "(" : "  (");
        writer_path(&w, record->file);
        writer_fmt(&w, ":%" PRIu32 ")", record->line);
        if (color) {
            writer_str(&w, LOG_COLOR_RESET);
        }
    }
    writer_str(&w, "\n");

    if (w.overflow) {
        /* Cut, but still one line: the next record must not start mid-line. */
        out[capacity - 2] = '\n';
        out[capacity - 1] = '\0';
        return capacity - 1;
    }
    return w.length;
}

static void json_record(log_writer *w, const rb_log_record *record, const char *message) {
    writer_fmt(w, "{\"t\":%.6f,\"ts\":%" PRId64 ",\"level\":\"%s\",\"file\":", record->time,
               record->unix_ms, rb_log_level_name(record->level));
    writer_json_string(w, record->file, true);
    writer_fmt(w, ",\"line\":%" PRIu32 ",\"func\":", record->line);
    writer_json_string(w, record->func, false);
    writer_str(w, ",\"msg\":");
    writer_json_string(w, message, false);
    writer_str(w, "}\n");
}

usize rb_log_format_json(const rb_log_record *record, char *out, usize capacity) {
    if (capacity == 0) {
        return 0;
    }
    log_writer w = {out, capacity, 0, false};
    out[0] = '\0';
    json_record(&w, record, record->message);
    if (!w.overflow) {
        return w.length;
    }

    /*
     * A cut line would be invalid JSON and break whoever parses the file.
     * Escaping can grow a message up to 6 times ("\u0001"), so this only
     * happens with pathological input or a small buffer.
     */
    w = (log_writer){out, capacity, 0, false};
    out[0] = '\0';
    json_record(&w, record, "(message too long for the JSON line buffer)");
    if (w.overflow) {
        out[0] = '\0';
        return 0;
    }
    return w.length;
}

bool rb_log_format_timestamp(const struct timespec *instant, char *out, usize capacity) {
    static const char unknown[] = "unknown time";
    if (capacity <= RB_LOG_TIMESTAMP_LENGTH) {
        if (capacity > 0) {
            out[0] = '\0';
        }
        return false;
    }

    /*
     * localtime splits seconds since 1970 into date and time in the user's
     * time zone. It returns a pointer to one static struct shared by the whole
     * process (not thread-safe), so it is copied at once. The thread-safe
     * versions differ per OS (localtime_r on POSIX, localtime_s on Windows,
     * with the arguments swapped), and this runs once per startup.
     */
    time_t     seconds = instant->tv_sec;
    struct tm *shared = localtime(&seconds);
    if (shared == NULL) {
        snprintf(out, capacity, "%s", unknown);
        return false;
    }
    struct tm local = *shared;

    /* %z is the offset from UTC as "+0100"; ISO 8601 with a full date wants "+01:00". */
    char date[24];
    char offset[8];
    if (strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local) == 0 ||
        strftime(offset, sizeof(offset), "%z", &local) != 5) {
        snprintf(out, capacity, "%s", unknown);
        return false;
    }
    long millis = instant->tv_nsec / 1000000L;
    snprintf(out, capacity, "%s.%03ld%.3s:%.2s", date, millis, offset, offset + 3);
    return true;
}

/* ============================================================================
 * Built-in sinks
 * ========================================================================== */

static void log_console_prepare(void) {
    if (g_log.console_ready) {
        return;
    }
    g_log.console_ready = true;
    bool terminal = rb_platform_console_init();
    /* https://no-color.org: any non-empty value disables colors. */
    const char *no_color = getenv("NO_COLOR");
    g_log.console_color = terminal && (no_color == NULL || no_color[0] == '\0');
}

static void log_console_sink(const rb_log_record *record, void *user) {
    RB_UNUSED(user);
    log_console_prepare();
    usize length = rb_log_format_console(record, g_log.console_color, g_log.line, RB_LOG_LINE_MAX);
    rb_platform_console_write(g_log.line, length);
}

static void log_file_sink(const rb_log_record *record, void *user) {
    FILE *file = user;
    usize length = rb_log_format_json(record, g_log.line, RB_LOG_LINE_MAX);
    fwrite(g_log.line, 1, length, file);
    /* Costs a system call per line, but a crash right after cannot lose the line that explains it.
     */
    fflush(file);
}

/* ============================================================================
 * Sinks
 * ========================================================================== */

bool rb_log_add_sink(rb_log_sink_fn fn, void *user, rb_log_level min_level) {
    if (fn == NULL || g_log.sink_count == RB_LOG_MAX_SINKS) {
        return false;
    }
    g_log.sinks[g_log.sink_count++] = (log_sink){fn, user, min_level};
    return true;
}

void rb_log_remove_sink(rb_log_sink_fn fn, void *user) {
    for (u32 i = 0; i < g_log.sink_count; ++i) {
        if (g_log.sinks[i].fn == fn && g_log.sinks[i].user == user) {
            /* Shift the rest down: sinks keep their registration order. */
            memmove(&g_log.sinks[i], &g_log.sinks[i + 1],
                    (g_log.sink_count - i - 1) * sizeof(g_log.sinks[0]));
            --g_log.sink_count;
            return;
        }
    }
}

/* ============================================================================
 * Clock
 * ========================================================================== */

/*
 * Wall clock (timespec_get is standard C11): it can jump if the system time
 * changes, harmless for logs. Starts the log clock on first use. False if the
 * clock cannot be read.
 */
static bool log_clock_read(struct timespec *now) {
    if (timespec_get(now, TIME_UTC) != TIME_UTC) {
        return false;
    }
    if (!g_log.clock_started) {
        g_log.clock_start = *now;
        g_log.clock_started = true;
    }
    return true;
}

/*
 * One line with the absolute date and time of t = 0: any record's absolute
 * time is that plus its `t`, so console lines can stay short (ADR 0008).
 */
static void log_announce_clock(void) {
    struct timespec now;
    char            stamp[RB_LOG_TIMESTAMP_LENGTH + 1] = "unknown time";
    if (log_clock_read(&now)) {
        rb_log_format_timestamp(&g_log.clock_start, stamp, sizeof(stamp));
    }
    RB_LOG_INFO("Log clock: t = 0 at %s", stamp);
}

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

/* Adds the JSON lines sink. True when there is nothing to open. Logs its own failures. */
static bool log_open_file(const rb_log_config *cfg) {
    if (cfg->file_path == NULL) {
        return true;
    }
    /* "b": no "\n" -> "\r\n" translation on Windows; the file has the same bytes everywhere. */
    FILE *file = fopen(cfg->file_path, "wb");
    if (file == NULL) {
        RB_LOG_ERROR("Cannot open log file '%s'", cfg->file_path);
        return false;
    }
    if (!rb_log_add_sink(log_file_sink, file, cfg->file_level)) {
        fclose(file);
        RB_LOG_ERROR("No free log sink for the file '%s'", cfg->file_path);
        return false;
    }
    g_log.file = file;
    return true;
}

bool rb_log_startup(const rb_log_config *config) {
    if (g_log.started) {
        return false;
    }
    rb_log_config cfg = config != NULL ? *config : (rb_log_config){0};
    if (!rb_log_add_sink(log_console_sink, NULL, cfg.console_level)) {
        return false;
    }
    g_log.started = true;
    bool file_ok = log_open_file(&cfg);
    /* After the file sink: the anchor is the file's first line unless something logged earlier. */
    log_announce_clock();
    return file_ok;
}

void rb_log_shutdown(void) {
    if (!g_log.started) {
        return;
    }
    rb_log_remove_sink(log_console_sink, NULL);
    if (g_log.file != NULL) {
        rb_log_remove_sink(log_file_sink, g_log.file);
        fclose(g_log.file);
        g_log.file = NULL;
    }
    g_log.started = false;
}

/* ============================================================================
 * Logging
 * ========================================================================== */

static RB_PRINTF_FORMAT(1, 0) void log_format_message(const char *fmt, va_list args) {
    char *msg = g_log.message;
    int   needed = vsnprintf(msg, RB_LOG_MESSAGE_MAX, fmt, args);
    if (needed < 0) {
        snprintf(msg, RB_LOG_MESSAGE_MAX, "(invalid log format: \"%s\")", fmt);
        return;
    }
    if ((usize)needed < RB_LOG_MESSAGE_MAX) {
        return;
    }

    /*
     * Cut: make room for "..." and the NUL. A UTF-8 character is one lead byte
     * followed by continuation bytes (10xxxxxx): if the cut lands on a
     * continuation byte, back up to the lead byte so the whole character goes,
     * never half of it.
     */
    usize end = RB_LOG_MESSAGE_MAX - 4;
    while (end > 0 && ((unsigned char)msg[end] & 0xC0u) == 0x80u) {
        --end;
    }
    memcpy(msg + end, "...", 4);
}

void rb_log_write(rb_log_level level, const char *file, u32 line, const char *func, const char *fmt,
                  ...) {
    if (g_log.writing) {
        return;
    }
    g_log.writing = true;

    va_list args;
    va_start(args, fmt);
    log_format_message(fmt, args);
    va_end(args);

    /*
     * Subtracting seconds and nanoseconds separately keeps the nanoseconds,
     * which a double holding seconds since 1970 would round off.
     */
    struct timespec now;
    f64             elapsed = 0.0;
    i64             unix_ms = 0;
    if (log_clock_read(&now)) {
        elapsed = (f64)(now.tv_sec - g_log.clock_start.tv_sec) +
                  (f64)(now.tv_nsec - g_log.clock_start.tv_nsec) * 1e-9;
        unix_ms = (i64)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

    rb_log_record record = {
        .level = level,
        .time = elapsed,
        .unix_ms = unix_ms,
        .file = file != NULL ? file : "",
        .line = line,
        .func = func != NULL ? func : "",
        .message = g_log.message,
    };

    if (g_log.sink_count == 0) {
        log_console_sink(&record, NULL);
    } else {
        for (u32 i = 0; i < g_log.sink_count; ++i) {
            const log_sink *sink = &g_log.sinks[i];
            if (level >= sink->min_level) {
                sink->fn(&record, sink->user);
            }
        }
    }

    g_log.writing = false;
}
