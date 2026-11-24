/* UTF-8 decoding (ADR 0021): implements text/utf8.h. */
#include "text/utf8.h"

/*
 * The valid ranges, after the Unicode standard's table of well-formed byte
 * sequences: a lead fixes how many continuation bytes follow, and the first
 * of them may be narrower than 80..BF. That narrower range is what rejects
 * overlong forms (E0 80..9F, F0 80..8F), surrogates (ED A0..BF) and values
 * beyond U+10FFFF (F4 90..BF) without decoding them first. C0, C1 and F5..FF
 * never start a valid sequence (C0 and C1 only ever start overlong ones).
 */
u32 rb_utf8_next(const char **cursor) {
    const u8 *bytes = (const u8 *)*cursor;
    u8        lead = bytes[0];
    if (lead < 0x80) {
        *cursor += 1;
        return lead;
    }

    u32 count;     /* continuation bytes */
    u32 codepoint; /* the lead's payload bits */
    u8  low = 0x80;
    u8  high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        count = 1;
        codepoint = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        count = 2;
        codepoint = lead & 0x0Fu;
        low = lead == 0xE0 ? 0xA0 : low;
        high = lead == 0xED ? 0x9F : high;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        count = 3;
        codepoint = lead & 0x07u;
        low = lead == 0xF0 ? 0x90 : low;
        high = lead == 0xF4 ? 0x8F : high;
    } else {
        /* A continuation byte without a lead, or a lead no valid sequence has. */
        *cursor += 1;
        return RB_UTF8_REPLACEMENT;
    }

    for (u32 i = 1; i <= count; ++i) {
        u8 byte = bytes[i];
        if (byte < low || byte > high) {
            /* Not consumed: it may start the next character (the '\0' included). */
            *cursor += i;
            return RB_UTF8_REPLACEMENT;
        }
        codepoint = (codepoint << 6) | (byte & 0x3Fu);
        /* Only the first continuation byte has a narrower range. */
        low = 0x80;
        high = 0xBF;
    }
    *cursor += count + 1;
    return codepoint;
}
