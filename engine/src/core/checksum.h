/*
 * Checksums (ADR 0024): short fingerprints of a byte stream, stored next to
 * the data so a reader can tell it arrived intact. Not hashes for tables, not
 * protection against tampering.
 *
 * Both continue from a previous value, so data can be fed in pieces:
 *
 *   u32 crc = rb_crc32(0, header, header_size);
 *   crc = rb_crc32(crc, body, body_size);    // same as one call over both
 */
#ifndef RB_CHECKSUM_H
#define RB_CHECKSUM_H

#include <reborn/defines.h>

/*
 * CRC-32 (ISO 3309, the polynomial of PNG, zip and Ethernet). Start from 0;
 * rb_crc32(0, "123456789", 9) is 0xCBF43926.
 */
u32 rb_crc32(u32 crc, const void *data, usize size);

/*
 * Adler-32 (RFC 1950), zlib's: cheaper than a CRC, weaker on short data.
 * Start from 1, not 0; rb_adler32(1, "Wikipedia", 9) is 0x11E60398.
 */
u32 rb_adler32(u32 adler, const void *data, usize size);

#endif /* RB_CHECKSUM_H */
