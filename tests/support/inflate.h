/*
 * DEFLATE decompression for tests (ADR 0024): reads what the engine's encoder
 * writes, so tests check the encoders by a round trip. Stored and
 * fixed-Huffman blocks only; dynamic codes (what zlib writes) come with the
 * engine's own PNG decoder, which can grow from this.
 *
 *   u8    out[1024];
 *   usize size;
 *   RB_REQUIRE(rb_test_zlib_decompress(compressed, compressed_size, out, sizeof(out), &size));
 */
#ifndef RB_TEST_INFLATE_H
#define RB_TEST_INFLATE_H

#include <reborn/defines.h>

/*
 * Raw DEFLATE from `in` into `out`. False on a malformed stream, a block type
 * it does not read, or more output than `capacity`. *consumed (may be NULL)
 * receives the input bytes read, the last partial byte included.
 */
bool rb_test_inflate(const u8 *in, usize in_size, u8 *out, usize capacity, usize *out_size,
                     usize *consumed);

/* A zlib stream: header checked, DEFLATE inflated, Adler-32 compared, nothing after it. */
bool rb_test_zlib_decompress(const u8 *in, usize in_size, u8 *out, usize capacity, usize *out_size);

#endif /* RB_TEST_INFLATE_H */
