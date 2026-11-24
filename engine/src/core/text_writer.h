/*
 * Text appended to a fixed buffer: the logger's lines (ADR 0008) and the
 * command replies (ADR 0022). Always NUL-terminated. When the text does not
 * fit, it keeps what does and sets `overflow`, so callers check once at the
 * end instead of after every append.
 */
#ifndef RB_CORE_TEXT_WRITER_H
#define RB_CORE_TEXT_WRITER_H

#include <reborn/defines.h>

typedef struct rb_text_writer {
    char *data;
    usize capacity; /* at least 1: the NUL */
    usize length;
    bool  overflow;
} rb_text_writer;

/* `capacity` >= 1; `data` becomes the empty string. */
void rb_text_writer_init(rb_text_writer *w, char *data, usize capacity);

void rb_text_writer_bytes(rb_text_writer *w, const char *bytes, usize count);
void rb_text_writer_str(rb_text_writer *w, const char *text);
void rb_text_writer_fmt(rb_text_writer *w, const char *fmt, ...) RB_PRINTF_FORMAT(2, 3);

/*
 * `text` as a JSON string, quotes included. With `path`, '\' becomes '/':
 * __FILE__ uses '\' on Windows, and paths read the same on every OS.
 */
void rb_text_writer_json_string(rb_text_writer *w, const char *text, bool path);

#endif /* RB_CORE_TEXT_WRITER_H */
