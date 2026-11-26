#include "rb_test.h"

#include <string.h>

#include <reborn/memory.h>

#include "compression/zlib.h"
#include "core/checksum.h"
#include "inflate.h"

RB_TEST(zlib, header_body_and_checksum) {
    const char *text = "hello hello hello hello";
    usize       size = strlen(text);
    u8          out[128];
    RB_REQUIRE(rb_zlib_bound(size) <= sizeof(out));
    usize written = rb_zlib_compress((const u8 *)text, size, out, sizeof(out), rb_heap_allocator());
    RB_REQUIRE(written > 6);
    /* A header any zlib reader accepts: DEFLATE, 32 KiB window, check bits right. */
    RB_EXPECT_EQ_UINT(out[0], 0x78u);
    RB_EXPECT_EQ_UINT(((u32)out[0] << 8 | out[1]) % 31, 0u);
    u32 adler = rb_adler32(1, text, size);
    RB_EXPECT_EQ_UINT(out[written - 4], adler >> 24);
    RB_EXPECT_EQ_UINT(out[written - 1], adler & 0xFFu);

    u8    back[64];
    usize back_size = 0;
    RB_REQUIRE(rb_test_zlib_decompress(out, written, back, sizeof(back), &back_size));
    RB_REQUIRE_EQ_UINT(back_size, size);
    RB_EXPECT(memcmp(back, text, size) == 0);
}

RB_TEST(zlib, empty_input) {
    u8    out[32];
    usize written = rb_zlib_compress(NULL, 0, out, sizeof(out), rb_heap_allocator());
    /* Header, the 2-byte empty block, Adler-32 of nothing (1). */
    RB_REQUIRE_EQ_UINT(written, 8u);
    RB_EXPECT_EQ_UINT(out[7], 1u);
    u8    back[1];
    usize back_size = 99;
    RB_EXPECT(rb_test_zlib_decompress(out, written, back, sizeof(back), &back_size));
    RB_EXPECT_EQ_UINT(back_size, 0u);
}

/* The test decoder must catch damage, or the round trips prove nothing. */
RB_TEST(zlib, corrupted_stream_is_rejected) {
    const char *text = "some bytes worth checking";
    usize       size = strlen(text);
    u8          out[128];
    usize written = rb_zlib_compress((const u8 *)text, size, out, sizeof(out), rb_heap_allocator());
    RB_REQUIRE(written > 0);
    out[written - 1] ^= 0x01u;
    u8    back[64];
    usize back_size = 0;
    RB_EXPECT(!rb_test_zlib_decompress(out, written, back, sizeof(back), &back_size));
}
