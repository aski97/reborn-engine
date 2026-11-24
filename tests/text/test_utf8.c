/* UTF-8 decoding (ADR 0021, E1): valid sequences of every length, and each kind of invalid one. */
#include "rb_test.h"

#include "text/utf8.h"

/*
 * Decodes the whole of `text` into `out`; returns how many codepoints. Bytes
 * are written as escapes below, so the source file's own encoding plays no
 * part. A letter right after an escape is an escape too: \x takes every hex
 * digit that follows, so "\x80b" would be one escape (and out of range).
 */
static u32 decode(const char *text, u32 *out, u32 capacity) {
    u32         count = 0;
    const char *cursor = text;
    while (*cursor != '\0' && count < capacity) {
        out[count++] = rb_utf8_next(&cursor);
    }
    return count;
}

/* ============================================================================
 * Valid
 * ========================================================================== */

RB_TEST(utf8, ascii_is_one_byte_each) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("Ab ~", cps, 8), 4);
    RB_EXPECT_EQ_UINT(cps[0], 'A');
    RB_EXPECT_EQ_UINT(cps[1], 'b');
    RB_EXPECT_EQ_UINT(cps[2], ' ');
    RB_EXPECT_EQ_UINT(cps[3], '~');
}

RB_TEST(utf8, every_sequence_length) {
    u32 cps[8];
    /* è (2 bytes), € (3), 😀 (4), then ASCII again. */
    RB_REQUIRE_EQ_UINT(decode("\xC3\xA8\xE2\x82\xAC\xF0\x9F\x98\x80!", cps, 8), 4);
    RB_EXPECT_EQ_UINT(cps[0], 0xE8);
    RB_EXPECT_EQ_UINT(cps[1], 0x20AC);
    RB_EXPECT_EQ_UINT(cps[2], 0x1F600);
    RB_EXPECT_EQ_UINT(cps[3], '!');
}

/* The first and last codepoint of each length: the ranges' edges are where bugs hide. */
RB_TEST(utf8, range_edges) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF\xF0\x90\x80\x80"
                              "\xF4\x8F\xBF\xBF",
                              cps, 8),
                       7);
    RB_EXPECT_EQ_UINT(cps[0], 0x7F);
    RB_EXPECT_EQ_UINT(cps[1], 0x80);
    RB_EXPECT_EQ_UINT(cps[2], 0x7FF);
    RB_EXPECT_EQ_UINT(cps[3], 0x800);
    RB_EXPECT_EQ_UINT(cps[4], 0xFFFF);
    RB_EXPECT_EQ_UINT(cps[5], 0x10000);
    RB_EXPECT_EQ_UINT(cps[6], 0x10FFFF);
}

RB_TEST(utf8, cursor_moves_past_the_whole_sequence) {
    const char *text = "\xE2\x82\xAC\x41"; /* \x41: 'A' */
    const char *cursor = text;
    RB_EXPECT_EQ_UINT(rb_utf8_next(&cursor), 0x20AC);
    RB_EXPECT(cursor == text + 3);
}

/* ============================================================================
 * Invalid: one replacement per broken character
 * ========================================================================== */

RB_TEST(utf8, lone_continuation_byte) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("a\x80\x62", cps, 8), 3); /* \x62: 'b' */
    RB_EXPECT_EQ_UINT(cps[1], RB_UTF8_REPLACEMENT);
    RB_EXPECT_EQ_UINT(cps[2], 'b');
}

/* The '\0' after a lead is not consumed: decoding stops at the string's end. */
RB_TEST(utf8, truncated_at_the_end) {
    const char *text = "\xE2\x82";
    const char *cursor = text;
    RB_EXPECT_EQ_UINT(rb_utf8_next(&cursor), RB_UTF8_REPLACEMENT);
    RB_EXPECT(cursor == text + 2);
    RB_EXPECT(*cursor == '\0');
}

/* E2 82 is the start of a valid 3-byte sequence cut short: one replacement, then 'A'. */
RB_TEST(utf8, truncated_before_the_next_character) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("\xE2\x82\x41", cps, 8), 2); /* \x41: 'A' */
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    RB_EXPECT_EQ_UINT(cps[1], 'A');
}

/* A lead followed by another lead: the second one starts the next character. */
RB_TEST(utf8, lead_after_lead) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("\xC3\xC3\xA8", cps, 8), 2);
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    RB_EXPECT_EQ_UINT(cps[1], 0xE8);
}

/* '/' written in 2 and 3 bytes: decoders accepting them have let "../" through filters. */
RB_TEST(utf8, overlong_forms) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("\xC0\xAF", cps, 8), 2); /* C0: never valid, AF: lone */
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    RB_EXPECT_EQ_UINT(cps[1], RB_UTF8_REPLACEMENT);
    RB_REQUIRE_EQ_UINT(decode("\xE0\x80\xAF", cps, 8), 3); /* E0 needs A0..BF next */
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    RB_REQUIRE_EQ_UINT(decode("\xF0\x80\x80\xAF", cps, 8), 4); /* F0 needs 90..BF next */
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
}

/* U+D800..U+DFFF belong to UTF-16's pairs, never to text. */
RB_TEST(utf8, surrogates) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("\xED\xA0\x80", cps, 8), 3);
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    /* U+D7FF, just below: valid. */
    RB_REQUIRE_EQ_UINT(decode("\xED\x9F\xBF", cps, 8), 1);
    RB_EXPECT_EQ_UINT(cps[0], 0xD7FF);
}

RB_TEST(utf8, beyond_the_last_codepoint) {
    u32 cps[8];
    RB_REQUIRE_EQ_UINT(decode("\xF4\x90\x80\x80", cps, 8), 4); /* U+110000 */
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    RB_REQUIRE_EQ_UINT(decode("\xF5\x80", cps, 8), 2);
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
    RB_REQUIRE_EQ_UINT(decode("\xFF", cps, 8), 1);
    RB_EXPECT_EQ_UINT(cps[0], RB_UTF8_REPLACEMENT);
}
