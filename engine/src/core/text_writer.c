/* Fixed-buffer text writer, shared by the logger and the command replies. */
#include "core/text_writer.h"

#include <reborn/assert.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void rb_text_writer_init(rb_text_writer *w, char *data, usize capacity) {
    RB_ASSERT(w != NULL && data != NULL && capacity >= 1);
    *w = (rb_text_writer){data, capacity, 0, false};
    data[0] = '\0';
}

void rb_text_writer_bytes(rb_text_writer *w, const char *bytes, usize count) {
    usize room = w->capacity - 1 - w->length;
    if (count > room) {
        count = room;
        w->overflow = true;
    }
    memcpy(w->data + w->length, bytes, count);
    w->length += count;
    w->data[w->length] = '\0';
}

void rb_text_writer_str(rb_text_writer *w, const char *text) {
    rb_text_writer_bytes(w, text, strlen(text));
}

void rb_text_writer_fmt(rb_text_writer *w, const char *fmt, ...) {
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
 * '"' and '\' must be escaped, and so must control characters (below 0x20),
 * which JSON forbids raw. Bytes from 0x80 up are UTF-8 and pass through:
 * JSON text is UTF-8.
 */
void rb_text_writer_json_string(rb_text_writer *w, const char *text, bool path) {
    rb_text_writer_bytes(w, "\"", 1);
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0'; ++c) {
        unsigned char ch = (path && *c == '\\') ? '/' : *c;
        switch (ch) {
        case '"':
            rb_text_writer_str(w, "\\\"");
            break;
        case '\\':
            rb_text_writer_str(w, "\\\\");
            break;
        case '\n':
            rb_text_writer_str(w, "\\n");
            break;
        case '\r':
            rb_text_writer_str(w, "\\r");
            break;
        case '\t':
            rb_text_writer_str(w, "\\t");
            break;
        default:
            if (ch < 0x20) {
                rb_text_writer_fmt(w, "\\u%04x", (unsigned)ch);
            } else {
                char byte = (char)ch;
                rb_text_writer_bytes(w, &byte, 1);
            }
            break;
        }
    }
    rb_text_writer_bytes(w, "\"", 1);
}
