/*
 * Fonts (ADR 0018): the images of the characters (glyphs) in one texture, the
 * atlas, and the metrics that place them.
 *
 *   rb_font *font = rb_font_create_builtin(rb_app_allocator(app), renderer);
 *   ...
 *   rb_glyph glyph = rb_font_get_glyph(font, 'A');
 *   rb_renderer_draw_texture_region(renderer, pen_x + glyph.offset.x, baseline + glyph.offset.y,
 *                                   glyph.source.width, glyph.source.height,
 *                                   rb_font_get_atlas(font), glyph.source, white);
 *   pen_x += glyph.advance;
 *   ...
 *   rb_font_destroy(font);
 *
 * Every value is in pixels at the font's own size, y pointing down: drawing
 * at another size multiplies them all by the same factor. The pen moves along
 * the baseline, the line letters sit on.
 *
 * Only ASCII for now (32 to 126); any other codepoint gives the missing glyph,
 * an empty box. Pointer parameters must not be NULL (RB_ASSERT in debug builds).
 */
#ifndef RB_FONT_H
#define RB_FONT_H

#include <reborn/defines.h>
#include <reborn/math.h>
#include <reborn/memory.h>
#include <reborn/renderer.h>

/* Opaque: the glyph table and its storage may change (UTF-8, other sources). */
typedef struct rb_font rb_font;

/*
 *          offset.x
 *      pen ──►┌────────┐ ◄── top-left of the image: offset.y above the baseline
 *       ·     │  ▄▄▄   │
 *       ·     │ █   █  │
 *  ─────●─────│─█████──│──── baseline
 *       ├─────┴────────┴──► the next pen: pen + advance
 */
typedef struct rb_glyph {
    rb_rect source;  /* where its image is in the atlas, in the atlas's pixels; empty for a space */
    rb_vec2 offset;  /* from the pen on the baseline to the image's top-left; y < 0 is above */
    f32     advance; /* how far the pen moves after it */
} rb_glyph;

/* About the whole font, from the baseline. */
typedef struct rb_font_metrics {
    f32 ascent;   /* up to the top of the tallest letters, positive */
    f32 descent;  /* down to the bottom of the lowest ones (g, p, y), positive */
    f32 line_gap; /* extra space between lines: baselines are ascent + descent + line_gap apart */
} rb_font_metrics;

/*
 * The engine's own 8x8 bitmap font (ADR 0018, E1): advance 8, ascent 7,
 * descent 1, nearest filtering, so it stays sharp drawn 2, 3, 4 times larger.
 * Its state comes from `allocator` (tag RB_MEMORY_TAG_TEXT), its atlas from
 * `renderer`. NULL on failure (logged): out of memory, the atlas texture not
 * created. Callable at any time, inside `render` too.
 */
RB_API rb_font *rb_font_create_builtin(rb_allocator allocator, rb_renderer *renderer);

/*
 * Destroys the atlas texture and frees the font. Not inside `render`, like
 * rb_renderer_destroy_texture: the frame being recorded may use the atlas.
 * Before the renderer's shutdown. NULL is ignored.
 */
RB_API void rb_font_destroy(rb_font *font);

/* The texture every glyph's `source` refers to. */
RB_API rb_texture rb_font_get_atlas(const rb_font *font);

/* The glyph for `codepoint`; the missing glyph if the font has none. */
RB_API rb_glyph rb_font_get_glyph(const rb_font *font, u32 codepoint);

RB_API rb_font_metrics rb_font_get_metrics(const rb_font *font);

#endif /* RB_FONT_H */
