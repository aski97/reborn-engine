/*
 * PNG writing (ADR 0024): the PNG container over the zlib stream.
 *
 * A PNG file is an 8-byte signature followed by chunks, each one
 *
 *   length (4 bytes, big-endian) | type (4 letters) | data | CRC-32 of type and data
 *
 * Ours are IHDR (size and pixel format), one IDAT (the pixels, compressed) and
 * IEND. Before compression each row of pixels goes through a filter that turns
 * it into differences from its neighbors: smooth images become runs of small
 * values, which DEFLATE compresses far better than the pixels themselves.
 */
#include "image/png_write.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "compression/deflate.h"
#include "compression/zlib.h"
#include "core/checksum.h"

/* The format's limit on width and height. */
#define PNG_MAX_SIDE 0x7FFFFFFFu
/* 8-byte signature, IHDR (12 + 13), IDAT's own 12, IEND (12). */
#define PNG_OVERHEAD (8u + 25u + 12u + 12u)

/* The filters, by the number PNG stores at the start of each row. */
enum {
    PNG_FILTER_NONE,
    PNG_FILTER_SUB,     /* minus the pixel to the left */
    PNG_FILTER_UP,      /* minus the pixel above */
    PNG_FILTER_AVERAGE, /* minus the mean of left and above */
    PNG_FILTER_PAETH,   /* minus whichever of left, above, above-left best predicts it */
    PNG_FILTER_COUNT
};

/* ============================================================================
 * Filters
 * ========================================================================== */

/*
 * Paeth's predictor: estimate the byte as left + above - above_left (a plane
 * through the three neighbors), then take the neighbor closest to the estimate.
 */
static u8 paeth(u8 left, u8 above, u8 above_left) {
    int estimate = (int)left + above - above_left;
    int to_left = estimate > left ? estimate - left : left - estimate;
    int to_above = estimate > above ? estimate - above : above - estimate;
    int to_corner = estimate > above_left ? estimate - above_left : above_left - estimate;
    if (to_left <= to_above && to_left <= to_corner) {
        return left;
    }
    return to_above <= to_corner ? above : above_left;
}

/*
 * Byte `i` of `row` filtered with `filter`. `above` is the previous row (zeros
 * for the first); the "left" neighbor is the same channel one pixel back,
 * `channels` bytes earlier, zero for the first pixel. Arithmetic wraps at 256:
 * the decoder adds the prediction back with the same wrap.
 */
static u8 filter_byte(int filter, const u8 *row, const u8 *above, usize i, usize channels) {
    u8 left = i >= channels ? row[i - channels] : 0;
    u8 up = above[i];
    u8 corner = i >= channels ? above[i - channels] : 0;
    switch (filter) {
    case PNG_FILTER_SUB:
        return (u8)(row[i] - left);
    case PNG_FILTER_UP:
        return (u8)(row[i] - up);
    case PNG_FILTER_AVERAGE:
        return (u8)(row[i] - (u8)(((u32)left + up) / 2));
    case PNG_FILTER_PAETH:
        return (u8)(row[i] - paeth(left, up, corner));
    default:
        return row[i];
    }
}

/*
 * The filter for a row, by the heuristic the PNG specification suggests: the
 * one whose output, read as signed bytes, has the smallest sum of absolute
 * values. Small values (0, 1, -1...) are what DEFLATE compresses well.
 */
static int filter_choose(const u8 *row, const u8 *above, usize row_bytes, usize channels) {
    int best = PNG_FILTER_NONE;
    u64 best_score = UINT64_MAX;
    for (int filter = 0; filter < PNG_FILTER_COUNT; ++filter) {
        u64 score = 0;
        for (usize i = 0; i < row_bytes && score < best_score; ++i) {
            i8 value = (i8)filter_byte(filter, row, above, i, channels);
            score += (u64)(value < 0 ? -(int)value : value);
        }
        if (score < best_score) {
            best_score = score;
            best = filter;
        }
    }
    return best;
}

/* ============================================================================
 * Encoding
 * ========================================================================== */

static void put_u32_be(u8 *at, u32 value) {
    at[0] = (u8)(value >> 24);
    at[1] = (u8)(value >> 16);
    at[2] = (u8)(value >> 8);
    at[3] = (u8)value;
}

/*
 * Writes the chunk header at `at` (length, type); the data must already be at
 * at + 8. Appends the CRC; returns the chunk's total size.
 */
static usize put_chunk(u8 *at, const char type[4], u32 length) {
    put_u32_be(at, length);
    memcpy(at + 4, type, 4);
    put_u32_be(at + 8 + length, rb_crc32(0, at + 4, 4 + (usize)length));
    return 12 + (usize)length;
}

static bool image_is_opaque(const rb_image *image) {
    usize count = (usize)image->width * image->height;
    for (usize i = 0; i < count; ++i) {
        if (image->pixels[i * 4 + 3] != 255) {
            return false;
        }
    }
    return true;
}

/*
 * Every row, filtered, into `filtered`: per row its filter byte, then the
 * row's bytes. `rows` holds two rows of scratch: the current one in the
 * output's channels, and the one above it.
 */
static void filter_image(const rb_image *image, usize channels, u8 *rows, u8 *filtered) {
    usize row_bytes = (usize)image->width * channels;
    u8   *above = rows;
    u8   *row = rows + row_bytes;
    memset(above, 0, row_bytes);
    for (u32 y = 0; y < image->height; ++y) {
        const u8 *source = image->pixels + (usize)y * image->width * 4;
        for (u32 x = 0; x < image->width; ++x) {
            memcpy(row + (usize)x * channels, source + (usize)x * 4, channels);
        }
        int filter = filter_choose(row, above, row_bytes, channels);
        u8 *out = filtered + (usize)y * (row_bytes + 1);
        out[0] = (u8)filter;
        for (usize i = 0; i < row_bytes; ++i) {
            out[1 + i] = filter_byte(filter, row, above, i, channels);
        }
        u8 *swap = above;
        above = row;
        row = swap;
    }
}

bool rb_png_encode(const rb_image *image, rb_allocator allocator, rb_png_data *out) {
    RB_ASSERT(image != NULL && image->pixels != NULL && out != NULL);
    RB_ASSERT(image->width > 0 && image->height > 0);
    *out = (rb_png_data){0};
    /* Opaque images need no alpha: a quarter less to store. */
    usize channels = image_is_opaque(image) ? 3 : 4;
    u64   row_bytes = (u64)image->width * channels;
    u64   filtered_size = (row_bytes + 1) * image->height;
    if (image->width > PNG_MAX_SIDE || image->height > PNG_MAX_SIDE ||
        filtered_size > RB_DEFLATE_MAX_INPUT || rb_zlib_bound((usize)filtered_size) > UINT32_MAX) {
        RB_LOG_ERROR("PNG: a %ux%u image is too large to encode", image->width, image->height);
        return false;
    }

    usize rows_size = (usize)row_bytes * 2;
    usize capacity = PNG_OVERHEAD + rb_zlib_bound((usize)filtered_size);
    u8   *rows = RB_ALLOC_ARRAY(allocator, u8, rows_size, RB_MEMORY_TAG_IMAGE);
    u8   *filtered = RB_ALLOC_ARRAY(allocator, u8, (usize)filtered_size, RB_MEMORY_TAG_IMAGE);
    u8   *bytes = RB_ALLOC_ARRAY(allocator, u8, capacity, RB_MEMORY_TAG_IMAGE);
    bool  ok = rows != NULL && filtered != NULL && bytes != NULL;
    usize size = 0;
    if (!ok) {
        RB_LOG_ERROR("PNG: no memory to encode a %ux%u image", image->width, image->height);
    } else {
        filter_image(image, channels, rows, filtered);

        static const u8 signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        memcpy(bytes, signature, sizeof(signature));
        size = sizeof(signature);

        u8 *header = bytes + size + 8;
        put_u32_be(header, image->width);
        put_u32_be(header + 4, image->height);
        header[8] = 8;                     /* bits per channel */
        header[9] = channels == 3 ? 2 : 6; /* color type: RGB, or RGB with alpha */
        header[10] = 0;                    /* compression: zlib, the only one defined */
        header[11] = 0;                    /* filtering: per row, the only one defined */
        header[12] = 0;                    /* no interlacing */
        size += put_chunk(bytes + size, "IHDR", 13);

        /* Compressed straight into its place in the file: no copy. */
        usize compressed = rb_zlib_compress(filtered, (usize)filtered_size, bytes + size + 8,
                                            capacity - size - 8 - 4 - 12, allocator);
        ok = compressed > 0;
        if (ok) {
            size += put_chunk(bytes + size, "IDAT", (u32)compressed);
            size += put_chunk(bytes + size, "IEND", 0);
        }
    }
    RB_FREE_ARRAY(allocator, rows, u8, rows_size, RB_MEMORY_TAG_IMAGE);
    RB_FREE_ARRAY(allocator, filtered, u8, (usize)filtered_size, RB_MEMORY_TAG_IMAGE);
    if (!ok) {
        RB_FREE_ARRAY(allocator, bytes, u8, capacity, RB_MEMORY_TAG_IMAGE);
        return false;
    }
    *out = (rb_png_data){.bytes = bytes, .size = size, .capacity = capacity};
    return true;
}

void rb_png_data_free(rb_png_data *data, rb_allocator allocator) {
    RB_ASSERT(data != NULL);
    if (data->bytes != NULL) {
        RB_FREE_ARRAY(allocator, data->bytes, u8, data->capacity, RB_MEMORY_TAG_IMAGE);
    }
    *data = (rb_png_data){0};
}

/* ============================================================================
 * Files
 * ========================================================================== */

/*
 * C's stdio, not a file system layer (none exists yet). "wb": Windows would
 * otherwise turn every 0x0A byte into 0x0D 0x0A and break the file.
 */
bool rb_image_write_png(const rb_image *image, const char *path, rb_allocator scratch,
                        usize *bytes) {
    RB_ASSERT(image != NULL && path != NULL);
    if (bytes != NULL) {
        *bytes = 0;
    }
    if (image->width == 0 || image->height == 0 || image->pixels == NULL) {
        RB_LOG_ERROR("PNG \"%s\": the image has no pixels", path);
        return false;
    }
    rb_png_data data;
    if (!rb_png_encode(image, scratch, &data)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    bool  ok = file != NULL;
    if (!ok) {
        RB_LOG_ERROR("PNG \"%s\": cannot create it (%s)", path, strerror(errno));
    } else {
        ok = fwrite(data.bytes, 1, data.size, file) == data.size;
        /* fclose flushes: a full disk may show up only here. */
        ok = fclose(file) == 0 && ok;
        if (!ok) {
            RB_LOG_ERROR("PNG \"%s\": writing failed (%s)", path, strerror(errno));
            /* A truncated image is worse than none: an agent would read it as the frame. */
            remove(path);
        }
    }
    if (ok && bytes != NULL) {
        *bytes = data.size;
    }
    rb_png_data_free(&data, scratch);
    return ok;
}
