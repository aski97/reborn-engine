/*
 * The zlib format (RFC 1950, ADR 0024): a DEFLATE stream between a 2-byte
 * header and an Adler-32 of the uncompressed bytes. PNG stores its pixels this
 * way; so can any file of ours that wants compression.
 */
#ifndef RB_ZLIB_H
#define RB_ZLIB_H

#include <reborn/defines.h>
#include <reborn/memory.h>

/* Bytes no zlib output of `size` input bytes can exceed. */
usize rb_zlib_bound(usize size);

/*
 * As rb_deflate_compress, with the header and the checksum around it; at most
 * RB_DEFLATE_MAX_INPUT bytes, `capacity` at least rb_zlib_bound(size). Returns
 * the bytes written; 0 on failure (logged).
 */
usize rb_zlib_compress(const u8 *in, usize size, u8 *out, usize capacity, rb_allocator scratch);

#endif /* RB_ZLIB_H */
