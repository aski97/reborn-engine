/*
 * DEFLATE decompression for tests (ADR 0024).
 */
#include "inflate.h"

#include "core/checksum.h"

/* ============================================================================
 * Bit reader
 * ========================================================================== */

typedef struct inflate_reader {
    const u8 *in;
    usize     size;
    usize     byte; /* the next byte to load */
    u32       bit;  /* the next bit inside in[byte], 0 = lowest */
    bool      overrun;
} inflate_reader;

static u32 read_bit(inflate_reader *r) {
    if (r->byte >= r->size) {
        r->overrun = true;
        return 0;
    }
    u32 value = (r->in[r->byte] >> r->bit) & 1u;
    if (++r->bit == 8) {
        r->bit = 0;
        ++r->byte;
    }
    return value;
}

/* A field: its lowest bit first. */
static u32 read_bits(inflate_reader *r, u32 count) {
    u32 value = 0;
    for (u32 i = 0; i < count; ++i) {
        value |= read_bit(r) << i;
    }
    return value;
}

/* A Huffman code: its highest bit first. */
static u32 read_code(inflate_reader *r, u32 count) {
    u32 value = 0;
    for (u32 i = 0; i < count; ++i) {
        value = (value << 1) | read_bit(r);
    }
    return value;
}

/* ============================================================================
 * Blocks
 * ========================================================================== */

static const u16 LENGTH_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8  LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 DISTANCE_BASE[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 DISTANCE_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/*
 * The fixed codes are prefix-free: 7 bits tell whether it is one of 256-279,
 * an eighth whether it is 0-143 or 280-287, a ninth gives 144-255.
 */
static u32 read_fixed_symbol(inflate_reader *r) {
    u32 code = read_code(r, 7);
    if (code <= 0x17u) {
        return 256 + code;
    }
    code = (code << 1) | read_bit(r);
    if (code >= 0x30u && code <= 0xBFu) {
        return code - 0x30u;
    }
    if (code >= 0xC0u && code <= 0xC7u) {
        return 280 + (code - 0xC0u);
    }
    code = (code << 1) | read_bit(r);
    return 144 + (code - 0x190u);
}

static bool inflate_stored(inflate_reader *r, u8 *out, usize capacity, usize *size) {
    /* Stored data starts at a byte boundary. */
    if (r->bit != 0) {
        r->bit = 0;
        ++r->byte;
    }
    if (r->byte + 4 > r->size) {
        return false;
    }
    u32 len = r->in[r->byte] | ((u32)r->in[r->byte + 1] << 8);
    u32 nlen = r->in[r->byte + 2] | ((u32)r->in[r->byte + 3] << 8);
    r->byte += 4;
    if ((len ^ 0xFFFFu) != nlen || r->byte + len > r->size || *size + len > capacity) {
        return false;
    }
    for (u32 i = 0; i < len; ++i) {
        out[(*size)++] = r->in[r->byte++];
    }
    return true;
}

static bool inflate_fixed(inflate_reader *r, u8 *out, usize capacity, usize *size) {
    for (;;) {
        u32 symbol = read_fixed_symbol(r);
        if (r->overrun) {
            return false;
        }
        if (symbol < 256) {
            if (*size >= capacity) {
                return false;
            }
            out[(*size)++] = (u8)symbol;
            continue;
        }
        if (symbol == 256) {
            return true;
        }
        u32 l = symbol - 257;
        if (l >= 29) {
            return false;
        }
        u32 length = LENGTH_BASE[l] + read_bits(r, LENGTH_EXTRA[l]);
        u32 d = read_code(r, 5);
        if (d >= 30) {
            return false;
        }
        u32 distance = DISTANCE_BASE[d] + read_bits(r, DISTANCE_EXTRA[d]);
        if (r->overrun || distance > *size || *size + length > capacity) {
            return false;
        }
        /* Byte by byte: a match may overlap what it writes (distance 1 repeats one byte). */
        for (u32 i = 0; i < length; ++i) {
            out[*size] = out[*size - distance];
            ++*size;
        }
    }
}

bool rb_test_inflate(const u8 *in, usize in_size, u8 *out, usize capacity, usize *out_size,
                     usize *consumed) {
    inflate_reader r = {.in = in, .size = in_size};
    usize          size = 0;
    bool           last = false;
    while (!last) {
        last = read_bit(&r) != 0;
        u32  type = read_bits(&r, 2);
        bool ok = false;
        if (type == 0) {
            ok = inflate_stored(&r, out, capacity, &size);
        } else if (type == 1) {
            ok = inflate_fixed(&r, out, capacity, &size);
        }
        if (!ok || r.overrun) {
            return false;
        }
    }
    *out_size = size;
    if (consumed != NULL) {
        *consumed = r.byte + (r.bit != 0 ? 1 : 0);
    }
    return true;
}

bool rb_test_zlib_decompress(const u8 *in, usize in_size, u8 *out, usize capacity,
                             usize *out_size) {
    if (in_size < 6 || (in[0] & 0x0Fu) != 8 || ((u32)in[0] << 8 | in[1]) % 31 != 0 ||
        (in[1] & 0x20u) != 0) {
        return false;
    }
    usize consumed;
    if (!rb_test_inflate(in + 2, in_size - 6, out, capacity, out_size, &consumed) ||
        2 + consumed + 4 != in_size) {
        return false;
    }
    const u8 *tail = in + 2 + consumed;
    u32       stored = ((u32)tail[0] << 24) | ((u32)tail[1] << 16) | ((u32)tail[2] << 8) | tail[3];
    return stored == rb_adler32(1, out, *out_size);
}
