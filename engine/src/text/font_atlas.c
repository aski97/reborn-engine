/*
 * Glyph atlas (ADR 0018, C1): implements text/font_atlas.h.
 */
#include "text/font_atlas.h"

#include <reborn/log.h>

#include <string.h>

bool rb_font_atlas_pack(const rb_glyph_image *images, u32 count, u32 width, u32 padding,
                        rb_rect *rects, u32 *height) {
    u32 x = 0;
    u32 y = 0;
    u32 row_height = 0;
    for (u32 i = 0; i < count; ++i) {
        u32 w = images[i].width;
        u32 h = images[i].height;
        if (w == 0 || h == 0) {
            rects[i] = (rb_rect){0};
            continue;
        }
        if (w > width) {
            RB_LOG_ERROR("Glyph %u is %u pixels wide, the atlas %u", i, w, width);
            return false;
        }
        /* Not on an empty row: an image as wide as the atlas must not open a row before it. */
        if (x > 0 && x + w > width) {
            y += row_height + padding;
            x = 0;
            row_height = 0;
        }
        rects[i] = (rb_rect){(f32)x, (f32)y, (f32)w, (f32)h};
        x += w + padding;
        row_height = h > row_height ? h : row_height;
    }
    *height = y + row_height;
    return true;
}

void rb_font_atlas_fill(u8 *pixels, u32 width, u32 height, const rb_glyph_image *images,
                        const rb_rect *rects, u32 count) {
    /*
     * White under the transparent pixels too, not black: a linear filter
     * blends a glyph's edge with its transparent neighbors, color included,
     * and black there would darken the edges of colored text.
     */
    memset(pixels, 0xFF, (usize)width * height * 4);
    for (usize i = 3; i < (usize)width * height * 4; i += 4) {
        pixels[i] = 0;
    }
    for (u32 i = 0; i < count; ++i) {
        const rb_glyph_image *image = &images[i];
        if (image->width == 0 || image->height == 0) {
            continue;
        }
        /* Whole pixels: the packer's rectangles hold integers. */
        u32 left = (u32)rects[i].x;
        u32 top = (u32)rects[i].y;
        for (u32 row = 0; row < image->height; ++row) {
            for (u32 col = 0; col < image->width; ++col) {
                usize at = ((usize)(top + row) * width + left + col) * 4;
                pixels[at + 3] = image->coverage[row * image->width + col];
            }
        }
    }
}
