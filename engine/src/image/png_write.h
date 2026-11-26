/*
 * PNG encoding into memory (ADR 0024): what rb_image_write_png puts in the
 * file, apart from the file, so tests can read it back without touching disk.
 */
#ifndef RB_PNG_WRITE_H
#define RB_PNG_WRITE_H

#include <reborn/image.h>
#include <reborn/memory.h>

/* The encoded file: `size` bytes used of `capacity`, from the allocator given to the encoder. */
typedef struct rb_png_data {
    u8   *bytes;
    usize size;
    usize capacity;
} rb_png_data;

/*
 * Encodes `image` (width and height > 0) as a whole PNG file into *out,
 * allocated from `allocator` (tag RB_MEMORY_TAG_IMAGE); the scratch it needs
 * on the way comes from there too and goes back before the return. False
 * (logged) when the image is too large or memory runs out: *out is zeroed.
 */
bool rb_png_encode(const rb_image *image, rb_allocator allocator, rb_png_data *out);

/* Gives back what rb_png_encode allocated; zeroes *data. A zeroed one is ignored. */
void rb_png_data_free(rb_png_data *data, rb_allocator allocator);

#endif /* RB_PNG_WRITE_H */
