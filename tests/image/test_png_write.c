#include "rb_test.h"

#include <stdio.h>
#include <string.h>

#include <reborn/image.h>
#include <reborn/memory.h>
#include <reborn/random.h>

#include "core/checksum.h"
#include "failing_allocator.h"
#include "image/png_write.h"
#include "inflate.h"

/* ============================================================================
 * A small PNG reader: enough for what the writer produces
 * ========================================================================== */

static u32 read_u32_be(const u8 *at) {
    return ((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8) | at[3];
}

static u8 paeth(u8 a, u8 b, u8 c) {
    int p = (int)a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

typedef struct decoded {
    u32 width;
    u32 height;
    u32 color_type;
    u32 chunk_count;
    u8 *rgba; /* width * height * 4, from the heap */
} decoded;

/*
 * Checks the signature, every chunk's CRC, the order IHDR, IDAT, IEND; inflates
 * IDAT, undoes the filters and expands to RGBA. False at the first problem.
 */
static bool png_decode(const u8 *png, usize size, decoded *out) {
    static const u8 signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    *out = (decoded){0};
    if (size < 8 || memcmp(png, signature, 8) != 0) {
        return false;
    }
    const u8 *idat = NULL;
    u32       idat_size = 0;
    bool      ended = false;
    for (usize at = 8; at < size;) {
        if (at + 12 > size || ended) {
            return false;
        }
        u32 length = read_u32_be(png + at);
        if (at + 12 + length > size) {
            return false;
        }
        const u8 *type = png + at + 4;
        const u8 *data = png + at + 8;
        if (rb_crc32(0, type, 4 + (usize)length) != read_u32_be(data + length)) {
            return false;
        }
        if (out->chunk_count == 0) {
            if (memcmp(type, "IHDR", 4) != 0 || length != 13 || data[8] != 8) {
                return false;
            }
            out->width = read_u32_be(data);
            out->height = read_u32_be(data + 4);
            out->color_type = data[9];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat = data;
            idat_size = length;
        } else if (memcmp(type, "IEND", 4) == 0) {
            ended = true;
        }
        ++out->chunk_count;
        at += 12 + (usize)length;
    }
    if (!ended || idat == NULL || (out->color_type != 2 && out->color_type != 6)) {
        return false;
    }
    usize channels = out->color_type == 2 ? 3 : 4;
    usize row_bytes = out->width * channels;
    usize raw_size = (row_bytes + 1) * out->height;
    u8   *raw = RB_ALLOC_ARRAY(rb_heap_allocator(), u8, raw_size + 1, RB_MEMORY_TAG_GENERAL);
    usize raw_got = 0;
    bool  ok = raw != NULL &&
               rb_test_zlib_decompress(idat, idat_size, raw, raw_size + 1, &raw_got) &&
               raw_got == raw_size;
    if (ok) {
        out->rgba = RB_ALLOC_ARRAY(rb_heap_allocator(), u8, (usize)out->width * out->height * 4,
                                   RB_MEMORY_TAG_GENERAL);
        ok = out->rgba != NULL;
    }
    /* Undo the filters in place: each row's bytes become the pixel bytes. */
    for (u32 y = 0; ok && y < out->height; ++y) {
        u8       *row = raw + y * (row_bytes + 1);
        const u8 *above = y > 0 ? raw + (y - 1) * (row_bytes + 1) + 1 : NULL;
        u8        filter = row[0];
        ok = filter <= 4;
        for (usize i = 0; ok && i < row_bytes; ++i) {
            u8 a = i >= channels ? row[1 + i - channels] : 0;
            u8 b = above != NULL ? above[i] : 0;
            u8 c = above != NULL && i >= channels ? above[i - channels] : 0;
            u8 predicted = filter == 1   ? a
                           : filter == 2 ? b
                           : filter == 3 ? (u8)(((u32)a + b) / 2)
                           : filter == 4 ? paeth(a, b, c)
                                         : 0;
            row[1 + i] = (u8)(row[1 + i] + predicted);
        }
        for (u32 x = 0; ok && x < out->width; ++x) {
            u8 *pixel = out->rgba + ((usize)y * out->width + x) * 4;
            memcpy(pixel, row + 1 + x * channels, channels);
            if (channels == 3) {
                pixel[3] = 255;
            }
        }
    }
    RB_FREE_ARRAY(rb_heap_allocator(), raw, u8, raw_size + 1, RB_MEMORY_TAG_GENERAL);
    return ok;
}

static void decoded_free(decoded *d) {
    RB_FREE_ARRAY(rb_heap_allocator(), d->rgba, u8, (usize)d->width * d->height * 4,
                  RB_MEMORY_TAG_GENERAL);
}

/* Encodes, decodes, compares. Returns the encoded size (0 on failure, already reported). */
static usize round_trip(const rb_image *image, u32 expected_color_type) {
    rb_png_data data;
    if (!rb_png_encode(image, rb_heap_allocator(), &data)) {
        RB_EXPECT(!"rb_png_encode failed");
        return 0;
    }
    decoded d;
    bool    ok = png_decode(data.bytes, data.size, &d) && d.width == image->width &&
                 d.height == image->height && d.color_type == expected_color_type &&
                 d.chunk_count == 3 &&
                 memcmp(d.rgba, image->pixels, (usize)image->width * image->height * 4) == 0;
    RB_EXPECT(ok);
    usize size = data.size;
    decoded_free(&d);
    rb_png_data_free(&data, rb_heap_allocator());
    return ok ? size : 0;
}

/* ============================================================================
 * Tests
 * ========================================================================== */

RB_TEST(png_write, one_opaque_pixel_is_rgb) {
    u8       pixels[4] = {10, 200, 30, 255};
    rb_image image = {.width = 1, .height = 1, .pixels = pixels};
    RB_EXPECT(round_trip(&image, 2) > 0);
}

RB_TEST(png_write, any_transparency_keeps_alpha) {
    u8       pixels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 9, 9, 9, 128};
    rb_image image = {.width = 2, .height = 2, .pixels = pixels};
    RB_EXPECT(round_trip(&image, 6) > 0);
}

/* A frame like Blink's: flat background, a square. Filtered rows are zeros: a tiny file. */
RB_TEST(png_write, flat_frame_is_small) {
    enum { W = 640, H = 360 };
    static u8 pixels[W * H * 4];
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            u8  *p = pixels + (y * W + x) * 4;
            bool square = x >= 300 && x < 340 && y >= 160 && y < 200;
            p[0] = square ? 230 : 26;
            p[1] = square ? 80 : 26;
            p[2] = square ? 60 : 26;
            p[3] = 255;
        }
    }
    rb_image image = {.width = W, .height = H, .pixels = pixels};
    usize    size = round_trip(&image, 2);
    RB_EXPECT(size > 0);
    /* 691 200 bytes of RGB: a few KB at most. */
    RB_EXPECT(size < 8000);
}

/* Smooth gradients pick Average and Paeth; noise picks None: every filter gets undone. */
RB_TEST(png_write, gradients_and_noise_round_trip) {
    enum { W = 97, H = 61 };
    static u8 pixels[W * H * 4];
    rb_rng    rng = rb_rng_create(7);
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            u8  *p = pixels + (y * W + x) * 4;
            bool noisy = y % 3 == 0;
            p[0] = noisy ? (u8)rb_rng_u32(&rng) : (u8)(x * 2 + y);
            p[1] = noisy ? (u8)rb_rng_u32(&rng) : (u8)(y * 3);
            p[2] = (u8)(x * y);
            p[3] = (u8)(255 - x);
        }
    }
    rb_image image = {.width = W, .height = H, .pixels = pixels};
    RB_EXPECT(round_trip(&image, 6) > 0);
}

RB_TEST(png_write, file_matches_the_encoding) {
    u8          pixels[3 * 2 * 4] = {1,  2,  3,  255, 4,  5,  6,  255, 7,  8,  9,  255,
                                     10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18, 255};
    rb_image    image = {.width = 3, .height = 2, .pixels = pixels};
    const char *path = "test_png_write.png";
    usize       bytes = 0;
    RB_REQUIRE(rb_image_write_png(&image, path, rb_heap_allocator(), &bytes));

    rb_png_data data;
    RB_REQUIRE(rb_png_encode(&image, rb_heap_allocator(), &data));
    u8    file_bytes[512];
    FILE *file = fopen(path, "rb");
    RB_REQUIRE(file != NULL);
    usize read = fread(file_bytes, 1, sizeof(file_bytes), file);
    fclose(file);
    remove(path);
    RB_EXPECT_EQ_UINT(bytes, data.size);
    RB_EXPECT_EQ_UINT(read, data.size);
    RB_EXPECT(read == data.size && memcmp(file_bytes, data.bytes, read) == 0);
    rb_png_data_free(&data, rb_heap_allocator());
}

RB_TEST(png_write, unwritable_path_fails) {
    u8       pixels[4] = {0, 0, 0, 255};
    rb_image image = {.width = 1, .height = 1, .pixels = pixels};
    usize    bytes = 99;
    RB_EXPECT(!rb_image_write_png(&image, "no_such_dir/x/shot.png", rb_heap_allocator(), &bytes));
    RB_EXPECT_EQ_UINT(bytes, 0u);
}

RB_TEST(png_write, empty_image_fails) {
    rb_image image = {0};
    RB_EXPECT(!rb_image_write_png(&image, "never.png", rb_heap_allocator(), NULL));
}

/* Each allocation in turn fails: nothing leaks, *out stays zeroed. */
RB_TEST(png_write, every_allocation_failure_is_clean) {
    u8       pixels[4 * 4 * 4] = {0};
    rb_image image = {.width = 4, .height = 4, .pixels = pixels};
    for (u32 n = 1;; ++n) {
        rb_failing_allocator failing;
        rb_failing_allocator_init(&failing, rb_heap_allocator(), n);
        rb_allocator allocator = rb_failing_allocator_get(&failing);
        rb_png_data  data;
        bool         ok = rb_png_encode(&image, allocator, &data);
        if (!ok) {
            RB_EXPECT(data.bytes == NULL);
            RB_EXPECT_EQ_UINT(failing.live_bytes, 0u);
            continue;
        }
        rb_png_data_free(&data, allocator);
        RB_EXPECT_EQ_UINT(failing.live_bytes, 0u);
        RB_EXPECT(n > 1);
        break;
    }
}
