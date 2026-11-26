/*
 * DEFLATE compression (RFC 1951, ADR 0024): the format inside zlib, gzip, zip
 * and PNG. Two ideas together: LZ77 replaces a run of bytes seen shortly before
 * with a (length, distance) pair, "copy 258 bytes from 3 bytes back"; Huffman
 * codes then write frequent symbols in fewer bits.
 *
 * This encoder writes one block with the fixed Huffman codes of the standard:
 * no code tables to build or send, a little less compression than the dynamic
 * codes zlib picks. Any DEFLATE decoder reads it.
 *
 *   usize capacity = rb_deflate_bound(size);
 *   u8   *out = ...capacity bytes...;
 *   usize written = rb_deflate_compress(in, size, out, capacity, scratch);
 */
#ifndef RB_DEFLATE_H
#define RB_DEFLATE_H

#include <reborn/defines.h>
#include <reborn/memory.h>

/* The largest input accepted: positions are kept in 32 bits. */
#define RB_DEFLATE_MAX_INPUT ((usize)UINT32_MAX - 1u)

/* Bytes no compressed output of `size` input bytes can exceed. */
usize rb_deflate_bound(usize size);

/*
 * Compresses `size` bytes (at most RB_DEFLATE_MAX_INPUT) into `out`, whose
 * `capacity` is at least rb_deflate_bound(size). The match tables (256 KiB)
 * come from `scratch`, tag RB_MEMORY_TAG_IMAGE, and go back before the return.
 * Returns the bytes written; 0 when the tables could not be allocated (logged).
 */
usize rb_deflate_compress(const u8 *in, usize size, u8 *out, usize capacity, rb_allocator scratch);

#endif /* RB_DEFLATE_H */
