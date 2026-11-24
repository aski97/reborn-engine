/*
 * Fonts (ADR 0018): implements reborn/font.h and text/font_internal.h.
 */
#include "text/font_internal.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include "text/font_builtin.h"

/* Space between glyphs in the atlas (C1): one pixel is all a linear filter reaches. */
#define RB_FONT_ATLAS_PADDING 1u

void rb_font_set_glyphs(rb_font *font, const rb_font_source *source, const rb_rect *rects) {
    font->metrics = source->metrics;
    for (u32 i = 0; i < RB_FONT_GLYPH_COUNT; ++i) {
        font->glyphs[i] = source->glyphs[i];
        font->glyphs[i].source = rects[i];
    }
}

rb_font *rb_font_create_from_source(rb_allocator allocator, rb_renderer *renderer,
                                    const rb_font_source *source) {
    rb_rect rects[RB_FONT_GLYPH_COUNT];
    u32     width = source->atlas_width;
    u32     height = 0;
    if (!rb_font_atlas_pack(source->images, RB_FONT_GLYPH_COUNT, width, RB_FONT_ATLAS_PADDING,
                            rects, &height)) {
        return NULL;
    }
    if (height == 0) {
        RB_LOG_ERROR("Font without a single visible glyph: no atlas to make");
        return NULL;
    }

    /* Zeroed (ADR 0010, H1): a zero atlas handle until the upload succeeds. */
    rb_font *font = RB_ALLOC_TYPE(allocator, rb_font, RB_MEMORY_TAG_TEXT);
    if (font == NULL) {
        return NULL;
    }
    /* Only until the upload: the texture keeps its own copy, in GPU memory. */
    usize size = (usize)width * height * 4;
    u8   *pixels = RB_ALLOC_ARRAY(allocator, u8, size, RB_MEMORY_TAG_TEXT);
    if (pixels == NULL) {
        RB_FREE_TYPE(allocator, font, rb_font, RB_MEMORY_TAG_TEXT);
        return NULL;
    }
    rb_font_atlas_fill(pixels, width, height, source->images, rects, RB_FONT_GLYPH_COUNT);
    rb_texture atlas = rb_renderer_create_texture(
        renderer,
        &(rb_texture_desc){
            .width = width, .height = height, .pixels = pixels, .filter = source->filter});
    RB_FREE_ARRAY(allocator, pixels, u8, size, RB_MEMORY_TAG_TEXT);
    if (atlas.id == 0) {
        /* The renderer logged why. */
        RB_FREE_TYPE(allocator, font, rb_font, RB_MEMORY_TAG_TEXT);
        return NULL;
    }

    font->allocator = allocator;
    font->renderer = renderer;
    font->atlas = atlas;
    rb_font_set_glyphs(font, source, rects);
    RB_LOG_DEBUG("Font created: %u glyphs, %ux%u atlas", RB_FONT_GLYPH_COUNT, width, height);
    return font;
}

rb_font *rb_font_create_builtin(rb_allocator allocator, rb_renderer *renderer) {
    RB_ASSERT(allocator.vtable != NULL);
    RB_ASSERT(renderer != NULL);
    /* The images point into it: it must outlive the source, which ends with this call. */
    u8             coverage[RB_FONT_GLYPH_COUNT][RB_FONT_BUILTIN_CELL * RB_FONT_BUILTIN_CELL];
    rb_font_source source;
    rb_font_builtin_source(&source, coverage);
    return rb_font_create_from_source(allocator, renderer, &source);
}

void rb_font_destroy(rb_font *font) {
    if (font == NULL) {
        return;
    }
    rb_renderer_destroy_texture(font->renderer, font->atlas);
    /* Copied out first: the free ends the font's life, allocator field included. */
    rb_allocator allocator = font->allocator;
    RB_FREE_TYPE(allocator, font, rb_font, RB_MEMORY_TAG_TEXT);
}

rb_texture rb_font_get_atlas(const rb_font *font) {
    RB_ASSERT(font != NULL);
    return font->atlas;
}

rb_glyph rb_font_get_glyph(const rb_font *font, u32 codepoint) {
    RB_ASSERT(font != NULL);
    return font->glyphs[rb_font_glyph_index(codepoint)];
}

rb_font_metrics rb_font_get_metrics(const rb_font *font) {
    RB_ASSERT(font != NULL);
    return font->metrics;
}
