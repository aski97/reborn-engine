/*
 * Images in memory (ADR 0020): a view of pixels owned by someone else.
 *
 *   static u8 pixels[16 * 16 * 4];
 *   rb_image image = {.width = 16, .height = 16, .pixels = pixels};
 *
 * RGBA, 8 bits per channel, sRGB, straight alpha (not premultiplied), rows
 * from the top, no padding: pixel (x, y) starts at pixels[(y * width + x) * 4].
 */
#ifndef RB_IMAGE_H
#define RB_IMAGE_H

#include <reborn/defines.h>

typedef struct rb_image {
    u32       width;
    u32       height;
    const u8 *pixels; /* width * height * 4 bytes; not owned */
} rb_image;

#endif /* RB_IMAGE_H */
