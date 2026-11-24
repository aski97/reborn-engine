/*
 * Fonts, the engine side (ADR 0018): what a glyph source hands to the font.
 *
 * A source (the builtin bitmap font; a TrueType rasterizer later) fills an
 * rb_font_source: per glyph an image and its metrics, plus the font's metrics.
 * rb_font_create_from_source does the rest, the same for every source: packs
 * the images into an atlas, uploads it, keeps the glyph table.
 */
#ifndef RB_FONT_INTERNAL_H
#define RB_FONT_INTERNAL_H

#include <reborn/defines.h>
#include <reborn/font.h>

#include "text/font_atlas.h"

/* The missing glyph, then ASCII 32 (space) to 126 (~). */
#define RB_FONT_GLYPH_COUNT     96u
#define RB_FONT_GLYPH_MISSING   0u
#define RB_FONT_FIRST_CODEPOINT 32u
#define RB_FONT_LAST_CODEPOINT  126u

/* Where `codepoint` is in a font's table: RB_FONT_GLYPH_MISSING if it has none. */
static inline u32 rb_font_glyph_index(u32 codepoint) {
    if (codepoint < RB_FONT_FIRST_CODEPOINT || codepoint > RB_FONT_LAST_CODEPOINT) {
        return RB_FONT_GLYPH_MISSING;
    }
    return codepoint - RB_FONT_FIRST_CODEPOINT + 1;
}

typedef struct rb_font_source {
    rb_glyph_image images[RB_FONT_GLYPH_COUNT]; /* indexed like rb_font_glyph_index */
    /* Offset and advance of each image; `source` is left to the packer. */
    rb_glyph          glyphs[RB_FONT_GLYPH_COUNT];
    rb_font_metrics   metrics;
    u32               atlas_width; /* at least the widest image */
    rb_texture_filter filter;      /* nearest for pixel fonts, linear for smooth ones */
} rb_font_source;

/*
 * A font from what a source produced: the images are read here and not kept,
 * so they may live on the caller's stack. NULL on failure (logged).
 */
rb_font *rb_font_create_from_source(rb_allocator allocator, rb_renderer *renderer,
                                    const rb_font_source *source);

#endif /* RB_FONT_INTERNAL_H */
