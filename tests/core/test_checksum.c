#include "rb_test.h"

#include <string.h>

#include "core/checksum.h"

/* ============================================================================
 * CRC-32
 * ========================================================================== */

/* The check value every CRC-32 implementation publishes. */
RB_TEST(checksum, crc32_check_value) {
    RB_EXPECT_EQ_UINT(rb_crc32(0, "123456789", 9), 0xCBF43926u);
}

RB_TEST(checksum, crc32_of_nothing_is_zero) {
    RB_EXPECT_EQ_UINT(rb_crc32(0, NULL, 0), 0u);
}

/* The CRC of PNG's IEND chunk (type, no data), in every PNG file ever written. */
RB_TEST(checksum, crc32_of_png_iend) {
    RB_EXPECT_EQ_UINT(rb_crc32(0, "IEND", 4), 0xAE426082u);
}

RB_TEST(checksum, crc32_in_pieces_matches_one_call) {
    const char *text = "The quick brown fox jumps over the lazy dog";
    usize       size = strlen(text);
    u32         whole = rb_crc32(0, text, size);
    u32         pieces = rb_crc32(rb_crc32(0, text, 10), text + 10, size - 10);
    RB_EXPECT_EQ_UINT(whole, 0x414FA339u);
    RB_EXPECT_EQ_UINT(pieces, whole);
}

/* ============================================================================
 * Adler-32
 * ========================================================================== */

RB_TEST(checksum, adler32_known_value) {
    RB_EXPECT_EQ_UINT(rb_adler32(1, "Wikipedia", 9), 0x11E60398u);
}

RB_TEST(checksum, adler32_of_nothing_is_one) {
    RB_EXPECT_EQ_UINT(rb_adler32(1, NULL, 0), 1u);
}

/* Past one block of the deferred modulo, with the largest bytes: the sums must not overflow. */
RB_TEST(checksum, adler32_long_run_of_ff) {
    static u8 bytes[100000];
    memset(bytes, 0xFF, sizeof(bytes));
    /* By hand: a = 1 + 255n, b = n + 255 n(n+1)/2, both mod 65521, with n = 100000. */
    u64 n = sizeof(bytes);
    u64 a = (1 + 255 * n) % 65521;
    u64 b = (n + 255 * (n * (n + 1) / 2)) % 65521;
    RB_EXPECT_EQ_UINT(rb_adler32(1, bytes, sizeof(bytes)), (u32)((b << 16) | a));
}

RB_TEST(checksum, adler32_in_pieces_matches_one_call) {
    static u8 bytes[20000];
    for (usize i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (u8)(i * 7 + 3);
    }
    u32 whole = rb_adler32(1, bytes, sizeof(bytes));
    u32 pieces = rb_adler32(rb_adler32(1, bytes, 7001), bytes + 7001, sizeof(bytes) - 7001);
    RB_EXPECT_EQ_UINT(pieces, whole);
}
