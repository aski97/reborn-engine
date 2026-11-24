/*
 * Glyph atlas (ADR 0018, C1): glyph images of any size placed side by side in
 * one image, which becomes the font's texture. Independent of where the images
 * come from (a bitmap font now, a TrueType rasterizer later) and of the GPU:
 * pure functions on byte arrays.
 */
#ifndef RB_FONT_ATLAS_H
#define RB_FONT_ATLAS_H

#include <reborn/defines.h>
#include <reborn/math.h>

/*
 * One glyph's image as a source produces it: how much of each pixel the
 * glyph covers, 0 (nothing) to 255 (all), rows from the top. Empty (a space)
 * when either size is 0; `coverage` is then not read.
 */
typedef struct rb_glyph_image {
    u32       width;
    u32       height;
    const u8 *coverage; /* width * height bytes */
} rb_glyph_image;

/*
 * Places the images on shelves: left to right in the order given, a new row
 * below when the next one does not fit in `width`, each row as tall as its
 * tallest image. `padding` empty pixels separate neighbors, so a filter that
 * reads around a pixel never reaches the next glyph.
 *
 * rects[i] receives where images[i] goes, in pixels; an empty image gets an
 * empty rectangle at (0, 0) and takes no space. *height receives the rows'
 * total height, 0 if every image is empty. False when an image is wider than
 * `width` (logged); `rects` and `height` are then incomplete.
 */
bool rb_font_atlas_pack(const rb_glyph_image *images, u32 count, u32 width, u32 padding,
                        rb_rect *rects, u32 *height);

/*
 * Writes the atlas: width * height * 4 bytes of RGBA, as rb_texture_desc takes
 * them. White everywhere, the alpha being each image's coverage where `rects`
 * put it and 0 elsewhere. `rects` must come from rb_font_atlas_pack with the
 * same images and width.
 */
void rb_font_atlas_fill(u8 *pixels, u32 width, u32 height, const rb_glyph_image *images,
                        const rb_rect *rects, u32 count);

#endif /* RB_FONT_ATLAS_H */
