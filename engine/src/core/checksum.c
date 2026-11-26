/*
 * Checksums (ADR 0024).
 */
#include "core/checksum.h"

#include <reborn/assert.h>

/* ============================================================================
 * CRC-32
 * ========================================================================== */

/*
 * The remainder of each byte value divided by the polynomial, so the loop
 * handles 8 bits per step instead of 1. Built on first use rather than written
 * out: 256 magic numbers would hide where they come from.
 */
static u32  g_crc_table[256];
static bool g_crc_table_ready;

static void crc32_build_table(void) {
    for (u32 n = 0; n < 256; ++n) {
        u32 c = n;
        for (int bit = 0; bit < 8; ++bit) {
            /* 0xEDB88320: the polynomial with its bits reversed, as the stream is LSB first. */
            c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        }
        g_crc_table[n] = c;
    }
    g_crc_table_ready = true;
}

u32 rb_crc32(u32 crc, const void *data, usize size) {
    RB_ASSERT(data != NULL || size == 0);
    /* Not thread-safe on the first call; nothing calls it from threads yet (TODO.md). */
    if (!g_crc_table_ready) {
        crc32_build_table();
    }
    const u8 *bytes = data;
    /* The standard inverts before and after: leading zero bytes then change the result. */
    u32 c = ~crc;
    for (usize i = 0; i < size; ++i) {
        c = g_crc_table[(c ^ bytes[i]) & 0xFFu] ^ (c >> 8);
    }
    return ~c;
}

/* ============================================================================
 * Adler-32
 * ========================================================================== */

/* The largest prime below 2^16: both sums stay below it. */
#define ADLER_MOD 65521u
/*
 * Bytes summed before the modulo is due: the most for which b cannot overflow
 * 32 bits even from ADLER_MOD - 1 with every byte 255 (zlib's NMAX).
 */
#define ADLER_BLOCK 5552u

u32 rb_adler32(u32 adler, const void *data, usize size) {
    RB_ASSERT(data != NULL || size == 0);
    const u8 *bytes = data;
    /* a: 1 plus the sum of the bytes; b: the sum of every a along the way. */
    u32 a = adler & 0xFFFFu;
    u32 b = adler >> 16;
    while (size > 0) {
        usize block = size < ADLER_BLOCK ? size : ADLER_BLOCK;
        size -= block;
        for (usize i = 0; i < block; ++i) {
            a += *bytes++;
            b += a;
        }
        a %= ADLER_MOD;
        b %= ADLER_MOD;
    }
    return (b << 16) | a;
}
