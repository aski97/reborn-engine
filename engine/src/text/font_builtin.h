/*
 * The engine's own bitmap font (ADR 0018, E1): one of the glyph sources a font
 * is made from. 8x8 pixels per glyph, drawn in font_builtin.c as rows of '.'
 * (empty) and '#' (covered).
 */
#ifndef RB_FONT_BUILTIN_H
#define RB_FONT_BUILTIN_H

#include <reborn/defines.h>

#include "text/font_internal.h"

/* Width and height of every glyph, in pixels. */
#define RB_FONT_BUILTIN_CELL 8u

/*
 * Each glyph's rows, top first, indexed like rb_font_glyph_index. A row is 8
 * characters and the terminating '\0' of its string literal. Exposed for the
 * tests, which check every row's shape.
 */
extern const char rb_font_builtin_glyphs[RB_FONT_GLYPH_COUNT][RB_FONT_BUILTIN_CELL]
                                        [RB_FONT_BUILTIN_CELL + 1];

/*
 * Fills `source` with the builtin font: each glyph's coverage (0 or 255) is
 * written into `coverage`, which source->images point into, so it must outlive
 * every use of `source`.
 */
void rb_font_builtin_source(
    rb_font_source *source,
    u8              coverage[RB_FONT_GLYPH_COUNT][RB_FONT_BUILTIN_CELL * RB_FONT_BUILTIN_CELL]);

#endif /* RB_FONT_BUILTIN_H */
