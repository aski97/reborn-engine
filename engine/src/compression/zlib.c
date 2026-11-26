/*
 * The zlib format (RFC 1950, ADR 0024).
 */
#include "compression/zlib.h"

#include <reborn/assert.h>

#include "compression/deflate.h"
#include "core/checksum.h"

/* Header and trailing Adler-32. */
#define ZLIB_OVERHEAD 6u

usize rb_zlib_bound(usize size) {
    return rb_deflate_bound(size) + ZLIB_OVERHEAD;
}

usize rb_zlib_compress(const u8 *in, usize size, u8 *out, usize capacity, rb_allocator scratch) {
    RB_ASSERT(out != NULL && capacity >= rb_zlib_bound(size));
    /*
     * CMF 0x78: method 8 (DEFLATE), a 32 KiB window. FLG 0x01: no preset
     * dictionary, level "fastest" (informative only), and the check bits that
     * make 0x7801 a multiple of 31, which readers verify.
     */
    out[0] = 0x78;
    out[1] = 0x01;
    usize written = rb_deflate_compress(in, size, out + 2, capacity - ZLIB_OVERHEAD, scratch);
    if (written == 0) {
        return 0;
    }
    /* Big-endian, unlike DEFLATE's bits: zlib's own fields follow network order. */
    u32   adler = rb_adler32(1, in, size);
    usize end = 2 + written;
    out[end + 0] = (u8)(adler >> 24);
    out[end + 1] = (u8)(adler >> 16);
    out[end + 2] = (u8)(adler >> 8);
    out[end + 3] = (u8)adler;
    return end + 4;
}
