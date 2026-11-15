/*
 * Formatting of the built-in log outputs (ADR 0008). Exposed for the tests:
 * the exact text is a contract (LLMs and scripts parse the JSON lines).
 */
#ifndef RB_LOG_INTERNAL_H
#define RB_LOG_INTERNAL_H

#include <reborn/log.h>

#include <time.h>

/* Large enough for any record in both formats; longer lines are handled, not overflowed. */
#define RB_LOG_LINE_MAX 16384u

/*
 * Human-readable line, '\n'-terminated: time, level, message and, from WARN
 * up, the source location. `color` adds ANSI escape sequences.
 * Writes at most `capacity` bytes including the NUL; returns the length
 * without it. A line that does not fit is cut.
 */
usize rb_log_format_console(const rb_log_record *record, bool color, char *out, usize capacity);

/*
 * One JSON object followed by '\n' (JSON lines). Always valid JSON: if the
 * record does not fit, the message is replaced by a fixed notice.
 */
usize rb_log_format_json(const rb_log_record *record, char *out, usize capacity);

/* "YYYY-MM-DDTHH:MM:SS.mmm+HH:MM": ISO 8601 local time with its offset from UTC. */
#define RB_LOG_TIMESTAMP_LENGTH 29u

/*
 * Writes `instant` as RB_LOG_TIMESTAMP_LENGTH characters plus the NUL
 * (`capacity` must be larger than RB_LOG_TIMESTAMP_LENGTH). False if the
 * conversion fails; `out` then holds "unknown time".
 * Uses localtime(), which is not thread-safe: call it from one thread only.
 */
bool rb_log_format_timestamp(const struct timespec *instant, char *out, usize capacity);

#endif /* RB_LOG_INTERNAL_H */
