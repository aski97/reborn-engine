/*
 * Text (ADR 0021): implements reborn/text.h and text/text_internal.h. A layer
 * on the font's glyphs and the renderer's public draws.
 */
#include "text/text_internal.h"

#include <reborn/assert.h>

#include "text/utf8.h"

/* From the font's own pixels to the current space; 0 for a size that draws nothing. */
static f32 text_factor(const rb_text_style *style) {
    rb_font_metrics metrics = rb_font_get_metrics(style->font);
    f32             height = metrics.ascent + metrics.descent;
    if (style->size <= 0.0f || height <= 0.0f) {
        return 0.0f;
    }
    return style->size / height;
}

/*
 * The width of the line starting at `line`, in the font's pixels; *end
 * receives where it stops: at its '\n' or at the string's '\0'.
 */
static f32 text_line_width(const rb_font *font, const char *line, const char **end) {
    f32         width = 0.0f;
    const char *cursor = line;
    while (*cursor != '\0' && *cursor != '\n') {
        width += rb_font_get_glyph(font, rb_utf8_next(&cursor)).advance;
    }
    *end = cursor;
    return width;
}

/* Lines and widest line in the font's pixels: what measuring and vertical alignment share. */
static rb_vec2 text_block(const rb_font *font, const char *text, u32 *lines) {
    rb_font_metrics metrics = rb_font_get_metrics(font);
    f32             widest = 0.0f;
    u32             count = 0;
    const char     *cursor = text;
    for (;;) {
        const char *end;
        f32         width = text_line_width(font, cursor, &end);
        widest = width > widest ? width : widest;
        ++count;
        if (*end == '\0') {
            break;
        }
        cursor = end + 1; /* past the '\n' */
    }
    *lines = count;
    f32 line_height = metrics.ascent + metrics.descent;
    return (rb_vec2){widest, (f32)count * line_height + (f32)(count - 1) * metrics.line_gap};
}

rb_vec2 rb_text_measure(const rb_text_style *style, const char *text) {
    RB_ASSERT(style != NULL);
    RB_ASSERT(style->font != NULL);
    RB_ASSERT(text != NULL);
    f32 factor = text_factor(style);
    if (factor == 0.0f) {
        return (rb_vec2){0.0f, 0.0f};
    }
    u32 lines;
    return rb_vec2_scale(text_block(style->font, text, &lines), factor);
}

void rb_text_layout(const rb_text_style *style, f32 x, f32 y, const char *text,
                    rb_text_emit_fn emit, void *context) {
    RB_ASSERT(style != NULL);
    RB_ASSERT(style->font != NULL);
    RB_ASSERT(text != NULL);
    RB_ASSERT(emit != NULL);
    const rb_font *font = style->font;
    f32            factor = text_factor(style);
    if (factor == 0.0f) {
        return;
    }
    rb_font_metrics metrics = rb_font_get_metrics(font);

    /* The whole block is measured first: middle and bottom depend on how many lines follow. */
    u32 lines;
    f32 height = text_block(font, text, &lines).y * factor;
    f32 top = y;
    switch (style->align_y) {
    case RB_TEXT_TOP:
        break;
    case RB_TEXT_MIDDLE:
        top = y - height * 0.5f;
        break;
    case RB_TEXT_BASELINE:
        top = y - metrics.ascent * factor;
        break;
    case RB_TEXT_BOTTOM:
        top = y - height;
        break;
    }

    f32         baseline = top + metrics.ascent * factor;
    f32         line_step = (metrics.ascent + metrics.descent + metrics.line_gap) * factor;
    const char *cursor = text;
    for (;;) {
        /* Each line is measured before it is placed: its own width sets where it starts. */
        const char *end;
        f32         width = text_line_width(font, cursor, &end) * factor;
        f32         pen = style->align_x == RB_TEXT_CENTER  ? x - width * 0.5f
                          : style->align_x == RB_TEXT_RIGHT ? x - width
                                                            : x;
        while (cursor != end) {
            rb_glyph glyph = rb_font_get_glyph(font, rb_utf8_next(&cursor));
            /* A space has no image: only the pen moves. */
            if (glyph.source.width > 0.0f && glyph.source.height > 0.0f) {
                rb_rect dest = {pen + glyph.offset.x * factor, baseline + glyph.offset.y * factor,
                                glyph.source.width * factor, glyph.source.height * factor};
                emit(context, dest, glyph.source);
            }
            pen += glyph.advance * factor;
        }
        if (*end == '\0') {
            break;
        }
        cursor = end + 1;
        baseline += line_step;
    }
}

/* ============================================================================
 * Drawing
 * ========================================================================== */

typedef struct text_draw_context {
    rb_renderer *renderer;
    rb_texture   atlas;
    rb_color     color;
} text_draw_context;

static void text_draw_glyph(void *context, rb_rect dest, rb_rect source) {
    const text_draw_context *draw = context;
    rb_renderer_draw_texture_region(draw->renderer, dest.x, dest.y, dest.width, dest.height,
                                    draw->atlas, source, draw->color);
}

void rb_text_draw(rb_renderer *renderer, f32 x, f32 y, const rb_text_style *style,
                  const char *text) {
    RB_ASSERT(renderer != NULL);
    RB_ASSERT(style != NULL);
    RB_ASSERT(style->font != NULL);
    text_draw_context context = {renderer, rb_font_get_atlas(style->font), style->color};
    rb_text_layout(style, x, y, text, text_draw_glyph, &context);
}
