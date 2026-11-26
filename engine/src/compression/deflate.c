/*
 * DEFLATE compression (RFC 1951, ADR 0024): LZ77 over hash chains, one block
 * with the fixed Huffman codes.
 */
#include "compression/deflate.h"

#include <reborn/assert.h>
#include <reborn/log.h>

/* How far back a match may start: the format's limit. */
#define DEFLATE_WINDOW    32768u
#define DEFLATE_MIN_MATCH 3u
#define DEFLATE_MAX_MATCH 258u
/* Slots of the hash table: one per 3-byte hash value. */
#define DEFLATE_HASH_BITS 15u
#define DEFLATE_HASH_SIZE (1u << DEFLATE_HASH_BITS)
/*
 * Candidates tried per position, newest first. More finds longer matches on
 * repetitive data and costs time on data that has none; 32 is about zlib's
 * default level.
 */
#define DEFLATE_MAX_CHAIN 32u
/*
 * A 3-byte match this far back costs about as many bits as the three bytes as
 * literals, and a near one may follow: not taken (zlib's TOO_FAR).
 */
#define DEFLATE_FAR_MIN_MATCH 4096u

/* ============================================================================
 * Bit writer
 *
 * DEFLATE packs fields starting from the lowest bit of each byte. Huffman codes
 * are the exception: they are defined most significant bit first, so they are
 * reversed before going through the same writer.
 * ========================================================================== */

typedef struct deflate_writer {
    u8   *out;
    usize capacity;
    usize size;
    u64   bits;  /* pending bits, the oldest in the lowest positions */
    u32   count; /* how many of them */
} deflate_writer;

static void writer_put(deflate_writer *w, u32 value, u32 bit_count) {
    /* At most 7 pending plus 32: fits 64 bits. */
    w->bits |= (u64)value << w->count;
    w->count += bit_count;
    while (w->count >= 8) {
        /* The bound guarantees room: running out is a bug in rb_deflate_bound. */
        RB_ASSERT(w->size < w->capacity);
        w->out[w->size++] = (u8)w->bits;
        w->bits >>= 8;
        w->count -= 8;
    }
}

/* The last partial byte, its unused high bits zero. */
static void writer_flush(deflate_writer *w) {
    if (w->count > 0) {
        RB_ASSERT(w->size < w->capacity);
        w->out[w->size++] = (u8)w->bits;
        w->bits = 0;
        w->count = 0;
    }
}

static u32 reverse_bits(u32 code, u32 length) {
    u32 reversed = 0;
    for (u32 i = 0; i < length; ++i) {
        reversed = (reversed << 1) | (code & 1u);
        code >>= 1;
    }
    return reversed;
}

/* ============================================================================
 * Fixed Huffman codes (RFC 1951, 3.2.6)
 * ========================================================================== */

/*
 * Symbols 0-255 are literal bytes, 256 ends the block, 257-285 start a match
 * and give its length. The standard fixes the code of each: lengths of 7, 8 or
 * 9 bits, short ones for the end and short lengths, which come up most.
 */
static void put_symbol(deflate_writer *w, u32 symbol) {
    u32 code;
    u32 length;
    if (symbol < 144) {
        code = 0x30u + symbol;
        length = 8;
    } else if (symbol < 256) {
        code = 0x190u + (symbol - 144);
        length = 9;
    } else if (symbol < 280) {
        code = symbol - 256;
        length = 7;
    } else {
        code = 0xC0u + (symbol - 280);
        length = 8;
    }
    writer_put(w, reverse_bits(code, length), length);
}

/*
 * A length is a symbol for its range plus extra bits for the place inside it:
 * 3 to 10 have a symbol each, then ranges double every four symbols. 258, the
 * longest, has its own (285).
 */
static const u16 LENGTH_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8  LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
/* Distances the same way, 1 to 32768, in 30 codes of 5 bits each. */
static const u16 DISTANCE_BASE[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 DISTANCE_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* The last range whose base is not above `value`. */
static u32 find_range(const u16 *bases, u32 count, u32 value) {
    u32 index = count - 1;
    while (bases[index] > value) {
        --index;
    }
    return index;
}

static void put_match(deflate_writer *w, u32 length, u32 distance) {
    u32 l = find_range(LENGTH_BASE, 29, length);
    put_symbol(w, 257 + l);
    writer_put(w, length - LENGTH_BASE[l], LENGTH_EXTRA[l]);
    u32 d = find_range(DISTANCE_BASE, 30, distance);
    /* Distance codes are all 5 bits: the code is the index, reversed like any Huffman code. */
    writer_put(w, reverse_bits(d, 5), 5);
    writer_put(w, distance - DISTANCE_BASE[d], DISTANCE_EXTRA[d]);
}

/* ============================================================================
 * Compression
 * ========================================================================== */

/*
 * Worst case: every byte a 9-bit literal (a match never costs more per byte,
 * see the far-match rule), plus the 3-bit block header and the 7-bit end.
 */
usize rb_deflate_bound(usize size) {
    return size + size / 8 + 16;
}

/* Multiplicative hash of the 3 bytes at `p`: the top bits of the product mix all 24. */
static u32 hash3(const u8 *p) {
    u32 v = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    return (v * 2654435761u) >> (32 - DEFLATE_HASH_BITS);
}

/*
 * The chains: head[h] is the newest position whose 3 bytes hash to h, prev[p %
 * window] the one before position p with the same hash. Stored as position + 1,
 * so a zeroed table means "none". A slot of prev is reused every window: a
 * position that far back is past the distance limit, and the search stops
 * there before following it.
 */
typedef struct deflate_tables {
    u32 head[DEFLATE_HASH_SIZE];
    u32 prev[DEFLATE_WINDOW];
} deflate_tables;

static void tables_insert(deflate_tables *t, const u8 *in, u32 pos) {
    u32 h = hash3(in + pos);
    t->prev[pos & (DEFLATE_WINDOW - 1)] = t->head[h];
    t->head[h] = pos + 1;
}

/* The longest earlier match for the bytes at `pos`, length 0 if none reaches the minimum. */
static u32 find_match(const deflate_tables *t, const u8 *in, u32 size, u32 pos, u32 *distance) {
    u32 max = size - pos < DEFLATE_MAX_MATCH ? size - pos : DEFLATE_MAX_MATCH;
    u32 best = 0;
    u32 candidate = t->head[hash3(in + pos)];
    for (u32 tries = 0; candidate != 0 && tries < DEFLATE_MAX_CHAIN; ++tries) {
        u32 start = candidate - 1;
        if (pos - start > DEFLATE_WINDOW) {
            break;
        }
        /*
         * The byte that would make it longer than the best so far first: most
         * candidates fail there, without comparing the bytes before it.
         */
        if (in[start + best] == in[pos + best]) {
            u32 length = 0;
            while (length < max && in[start + length] == in[pos + length]) {
                ++length;
            }
            if (length > best) {
                best = length;
                *distance = pos - start;
                if (length == max) {
                    break;
                }
            }
        }
        candidate = t->prev[start & (DEFLATE_WINDOW - 1)];
    }
    if (best < DEFLATE_MIN_MATCH ||
        (best == DEFLATE_MIN_MATCH && *distance > DEFLATE_FAR_MIN_MATCH)) {
        return 0;
    }
    return best;
}

usize rb_deflate_compress(const u8 *in, usize size, u8 *out, usize capacity, rb_allocator scratch) {
    RB_ASSERT(in != NULL || size == 0);
    RB_ASSERT(out != NULL && capacity >= rb_deflate_bound(size));
    RB_ASSERT(size <= RB_DEFLATE_MAX_INPUT);
    /* Zeroed: every chain starts empty. */
    deflate_tables *tables = RB_ALLOC_TYPE(scratch, deflate_tables, RB_MEMORY_TAG_IMAGE);
    if (tables == NULL) {
        RB_LOG_ERROR("No memory for the DEFLATE tables (%zu bytes)", sizeof(deflate_tables));
        return 0;
    }
    deflate_writer w = {.out = out, .capacity = capacity};
    /* Block header: BFINAL 1 (the only block), BTYPE 01 (fixed codes). */
    writer_put(&w, 1, 1);
    writer_put(&w, 1, 2);

    u32 n = (u32)size;
    u32 pos = 0;
    while (pos < n) {
        u32 length = 0;
        u32 distance = 0;
        /* A hash needs 3 bytes: the last two are always literals. */
        if (n - pos >= DEFLATE_MIN_MATCH) {
            length = find_match(tables, in, n, pos, &distance);
            tables_insert(tables, in, pos);
        }
        if (length == 0) {
            put_symbol(&w, in[pos]);
            ++pos;
            continue;
        }
        put_match(&w, length, distance);
        /* The bytes inside the match can start later matches: they enter the chains too. */
        for (u32 i = 1; i < length; ++i) {
            if (n - (pos + i) >= DEFLATE_MIN_MATCH) {
                tables_insert(tables, in, pos + i);
            }
        }
        pos += length;
    }
    put_symbol(&w, 256);
    writer_flush(&w);

    RB_FREE_TYPE(scratch, tables, deflate_tables, RB_MEMORY_TAG_IMAGE);
    return w.size;
}
