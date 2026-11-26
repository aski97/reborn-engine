/*
 * Images in memory (ADR 0020): a view of pixels owned by someone else.
 *
 *   static u8 pixels[16 * 16 * 4];
 *   rb_image image = {.width = 16, .height = 16, .pixels = pixels};
 *
 * RGBA, 8 bits per channel, sRGB, straight alpha (not premultiplied), rows
 * from the top, no padding: pixel (x, y) starts at pixels[(y * width + x) * 4].
 *
 * Writers turn one into a file (ADR 0024), one function per format:
 *
 *   usize bytes;
 *   if (rb_image_write_png(&image, "shot.png", rb_app_allocator(app), &bytes)) { ... }
 */
#ifndef RB_IMAGE_H
#define RB_IMAGE_H

#include <reborn/defines.h>
#include <reborn/memory.h>

typedef struct rb_image {
    u32       width;
    u32       height;
    const u8 *pixels; /* width * height * 4 bytes; not owned */
} rb_image;

/*
 * Writes `image` to the file at `path` (created or overwritten) as PNG: 8-bit
 * RGB when every pixel is opaque, RGBA otherwise. The scratch, about twice the
 * pixels' size, comes from `scratch` (tag RB_MEMORY_TAG_IMAGE) and goes back
 * before the return. *bytes (may be NULL) receives the file's size. False
 * (logged) for an image with no pixels or too large, no memory, or a file that
 * cannot be written (a partly written file is removed).
 */
RB_API bool rb_image_write_png(const rb_image *image, const char *path, rb_allocator scratch,
                               usize *bytes);

#endif /* RB_IMAGE_H */
