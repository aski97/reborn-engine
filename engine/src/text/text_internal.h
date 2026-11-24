/*
 * Text layout (ADR 0021): where each glyph of a string goes, apart from the
 * drawing, so it can be tested without a GPU.
 */
#ifndef RB_TEXT_INTERNAL_H
#define RB_TEXT_INTERNAL_H

#include <reborn/math.h>
#include <reborn/text.h>

/*
 * Called once per visible glyph: `dest` is the rectangle it covers in the
 * current space, `source` its image in the font's atlas, in the atlas's pixels.
 */
typedef void (*rb_text_emit_fn)(void *context, rb_rect dest, rb_rect source);

/*
 * Lays `text` out as rb_text_draw would, calling `emit` for each visible glyph
 * in the string's order with `context` passed through. Nothing for a size of 0
 * or less.
 */
void rb_text_layout(const rb_text_style *style, f32 x, f32 y, const char *text,
                    rb_text_emit_fn emit, void *context);

#endif /* RB_TEXT_INTERNAL_H */
