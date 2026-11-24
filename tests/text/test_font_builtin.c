/*
 * The builtin bitmap font (ADR 0018, E1): the drawings' shape and what the
 * source hands to the font. No GPU.
 */
#include "rb_test.h"

#include <reborn/log.h>

#include "text/font_builtin.h"

/* RB_EXPECT_NEAR compares f64: the casts make the promotion of f32 explicit. */
#define EXPECT_F32_NEAR(a, b, eps) RB_EXPECT_NEAR((f64)(a), (f64)(b), (f64)(eps))

/* ============================================================================
 * Glyph lookup
 * ========================================================================== */

RB_TEST(font_builtin, printable_ascii_maps_after_the_missing_glyph) {
    RB_EXPECT_EQ_UINT(rb_font_glyph_index(' '), 1);
    RB_EXPECT_EQ_UINT(rb_font_glyph_index('A'), 'A' - 31);
    RB_EXPECT_EQ_UINT(rb_font_glyph_index('~'), RB_FONT_GLYPH_COUNT - 1);
}

RB_TEST(font_builtin, other_codepoints_map_to_the_missing_glyph) {
    RB_EXPECT_EQ_UINT(rb_font_glyph_index(0), RB_FONT_GLYPH_MISSING);
    RB_EXPECT_EQ_UINT(rb_font_glyph_index('\n'), RB_FONT_GLYPH_MISSING);
    RB_EXPECT_EQ_UINT(rb_font_glyph_index(127), RB_FONT_GLYPH_MISSING);  /* DEL */
    RB_EXPECT_EQ_UINT(rb_font_glyph_index(0xE8), RB_FONT_GLYPH_MISSING); /* è */
    RB_EXPECT_EQ_UINT(rb_font_glyph_index(0xFFFFFFFFu), RB_FONT_GLYPH_MISSING);
}

/* ============================================================================
 * The drawings
 * ========================================================================== */

/*
 * A row with 7 characters, or 9 (which C accepts, dropping the '\0'), or a
 * typo would shift or corrupt a glyph without any compiler error.
 */
RB_TEST(font_builtin, every_row_is_eight_dots_or_hashes) {
    for (u32 g = 0; g < RB_FONT_GLYPH_COUNT; ++g) {
        for (u32 row = 0; row < RB_FONT_BUILTIN_CELL; ++row) {
            const char *line = rb_font_builtin_glyphs[g][row];
            for (u32 col = 0; col < RB_FONT_BUILTIN_CELL; ++col) {
                if (line[col] != '.' && line[col] != '#') {
                    RB_EXPECT(false);
                    RB_LOG_ERROR("Glyph %u, row %u, column %u: '%c'", g, row, col, line[col]);
                }
            }
            RB_EXPECT(line[RB_FONT_BUILTIN_CELL] == '\0');
        }
    }
}

/* Only the space is blank; a glyph forgotten in the table would be all '\0' and fail above. */
RB_TEST(font_builtin, every_glyph_but_the_space_has_ink) {
    for (u32 g = 0; g < RB_FONT_GLYPH_COUNT; ++g) {
        bool ink = false;
        for (u32 row = 0; row < RB_FONT_BUILTIN_CELL; ++row) {
            for (u32 col = 0; col < RB_FONT_BUILTIN_CELL; ++col) {
                ink = ink || rb_font_builtin_glyphs[g][row][col] == '#';
            }
        }
        RB_EXPECT(ink == (g != rb_font_glyph_index(' ')));
    }
}

/* Column 7 is the gap between neighbors: ink there would touch the next glyph. */
RB_TEST(font_builtin, the_last_column_stays_empty) {
    for (u32 g = 0; g < RB_FONT_GLYPH_COUNT; ++g) {
        for (u32 row = 0; row < RB_FONT_BUILTIN_CELL; ++row) {
            RB_EXPECT(rb_font_builtin_glyphs[g][row][RB_FONT_BUILTIN_CELL - 1] == '.');
        }
    }
}

/* ============================================================================
 * The source
 * ========================================================================== */

static u8             coverage[RB_FONT_GLYPH_COUNT][RB_FONT_BUILTIN_CELL * RB_FONT_BUILTIN_CELL];
static rb_font_source source;

RB_TEST(font_builtin, source_decodes_hashes_to_full_coverage) {
    rb_font_builtin_source(&source, coverage);
    u32                   a = rb_font_glyph_index('A');
    const rb_glyph_image *image = &source.images[a];
    RB_REQUIRE_EQ_UINT(image->width, 8);
    RB_REQUIRE_EQ_UINT(image->height, 8);
    /* "..##....": the top of the A. */
    RB_EXPECT_EQ_UINT(image->coverage[0], 0);
    RB_EXPECT_EQ_UINT(image->coverage[2], 255);
    RB_EXPECT_EQ_UINT(image->coverage[3], 255);
    RB_EXPECT_EQ_UINT(image->coverage[4], 0);
    /* "######..": its bar, row 4. */
    RB_EXPECT_EQ_UINT(image->coverage[4 * 8 + 5], 255);
    RB_EXPECT_EQ_UINT(image->coverage[4 * 8 + 6], 0);
}

RB_TEST(font_builtin, source_gives_the_space_no_image) {
    rb_font_builtin_source(&source, coverage);
    u32 space = rb_font_glyph_index(' ');
    RB_EXPECT_EQ_UINT(source.images[space].width, 0);
    RB_EXPECT_EQ_UINT(source.images[space].height, 0);
    EXPECT_F32_NEAR(source.glyphs[space].advance, 8.0, 0.0);
}

/* Every glyph a cell wide, its top-left corner 7 pixels above the baseline. */
RB_TEST(font_builtin, source_metrics_describe_the_cell) {
    rb_font_builtin_source(&source, coverage);
    for (u32 g = 0; g < RB_FONT_GLYPH_COUNT; ++g) {
        EXPECT_F32_NEAR(source.glyphs[g].advance, 8.0, 0.0);
        EXPECT_F32_NEAR(source.glyphs[g].offset.x, 0.0, 0.0);
        EXPECT_F32_NEAR(source.glyphs[g].offset.y, -7.0, 0.0);
    }
    EXPECT_F32_NEAR(source.metrics.ascent, 7.0, 0.0);
    EXPECT_F32_NEAR(source.metrics.descent, 1.0, 0.0);
    EXPECT_F32_NEAR(source.metrics.line_gap, 2.0, 0.0);
    RB_EXPECT(source.filter == RB_TEXTURE_FILTER_NEAREST);
}

/* The real glyphs through the real packer: 95 images, 14 per row, 7 rows of 9 pixels less one. */
RB_TEST(font_builtin, source_packs_into_its_atlas_width) {
    rb_font_builtin_source(&source, coverage);
    rb_rect rects[RB_FONT_GLYPH_COUNT];
    u32     height = 0;
    RB_REQUIRE(rb_font_atlas_pack(source.images, RB_FONT_GLYPH_COUNT, source.atlas_width, 1, rects,
                                  &height));
    RB_EXPECT_EQ_UINT(source.atlas_width, 128);
    RB_EXPECT_EQ_UINT(height, 62);
}
