/*
 * Text layout and measuring (ADR 0021), without a GPU: a builtin font built
 * on the stack (no atlas texture), its glyphs collected instead of drawn.
 *
 * The builtin font: 8x8 cells, advance 8, ascent 7, descent 1, line gap 2.
 * At size 16 everything doubles: glyphs 16x16, advance 16, lines 20 apart.
 */
#include "rb_test.h"

#include <reborn/text.h>

#include "text/font_atlas.h"
#include "text/font_builtin.h"
#include "text/text_internal.h"

#define EXPECT_F32_NEAR(a, b) RB_EXPECT_NEAR((f64)(a), (f64)(b), 1e-4)

static rb_font *test_font(void) {
    static rb_font font;
    static bool    ready;
    if (!ready) {
        static u8      coverage[RB_FONT_GLYPH_COUNT][RB_FONT_BUILTIN_CELL * RB_FONT_BUILTIN_CELL];
        rb_font_source source;
        rb_rect        rects[RB_FONT_GLYPH_COUNT];
        u32            height;
        rb_font_builtin_source(&source, coverage);
        RB_EXPECT(rb_font_atlas_pack(source.images, RB_FONT_GLYPH_COUNT, source.atlas_width, 1,
                                     rects, &height));
        rb_font_set_glyphs(&font, &source, rects);
        ready = true;
    }
    return &font;
}

static rb_text_style style16(rb_text_align_x align_x, rb_text_align_y align_y) {
    return (rb_text_style){.font = test_font(),
                           .size = 16.0f,
                           .color = rb_color_srgb(1.0f, 1.0f, 1.0f, 1.0f),
                           .align_x = align_x,
                           .align_y = align_y};
}

/* What rb_text_layout emitted, in order. */
typedef struct collected {
    u32     count;
    rb_rect dest[16];
    rb_rect source[16];
} collected;

static void collect(void *context, rb_rect dest, rb_rect source) {
    collected *out = context;
    if (out->count < 16) {
        out->dest[out->count] = dest;
        out->source[out->count] = source;
    }
    ++out->count;
}

static collected layout(const rb_text_style *style, f32 x, f32 y, const char *text) {
    collected out = {0};
    rb_text_layout(style, x, y, text, collect, &out);
    return out;
}

/* ============================================================================
 * Measuring
 * ========================================================================== */

RB_TEST(text, measure_one_line) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    rb_vec2       size = rb_text_measure(&style, "AB");
    EXPECT_F32_NEAR(size.x, 32.0f);
    EXPECT_F32_NEAR(size.y, 16.0f);
}

/* Spaces have no image but take their advance, trailing ones too. */
RB_TEST(text, measure_counts_spaces) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    EXPECT_F32_NEAR(rb_text_measure(&style, "A B ").x, 64.0f);
}

/* The widest line; two lines of 16 with a gap of 2x2 between them. */
RB_TEST(text, measure_several_lines) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    rb_vec2       size = rb_text_measure(&style, "A\nBCD");
    EXPECT_F32_NEAR(size.x, 48.0f);
    EXPECT_F32_NEAR(size.y, 36.0f);
}

RB_TEST(text, empty_string_is_one_empty_line) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    rb_vec2       size = rb_text_measure(&style, "");
    EXPECT_F32_NEAR(size.x, 0.0f);
    EXPECT_F32_NEAR(size.y, 16.0f);
    EXPECT_F32_NEAR(rb_text_measure(&style, "\n").y, 36.0f);
}

/* è is two bytes but one character: one advance, one box. */
RB_TEST(text, measure_counts_characters_not_bytes) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    EXPECT_F32_NEAR(rb_text_measure(&style, "\xC3\xA8").x, 16.0f);
}

RB_TEST(text, no_size_no_text) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    style.size = 0.0f;
    rb_vec2 size = rb_text_measure(&style, "AB");
    EXPECT_F32_NEAR(size.x, 0.0f);
    EXPECT_F32_NEAR(size.y, 0.0f);
    RB_EXPECT_EQ_UINT(layout(&style, 0.0f, 0.0f, "AB").count, 0);
}

/* ============================================================================
 * Layout
 * ========================================================================== */

/* Top left at the anchor: the glyph's cell starts there, the baseline 14 below. */
RB_TEST(text, glyphs_follow_the_pen) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    collected     out = layout(&style, 10.0f, 20.0f, "AB");
    RB_REQUIRE_EQ_UINT(out.count, 2);
    EXPECT_F32_NEAR(out.dest[0].x, 10.0f);
    EXPECT_F32_NEAR(out.dest[0].y, 20.0f);
    EXPECT_F32_NEAR(out.dest[0].width, 16.0f);
    EXPECT_F32_NEAR(out.dest[0].height, 16.0f);
    EXPECT_F32_NEAR(out.dest[1].x, 26.0f);
    EXPECT_F32_NEAR(out.dest[1].y, 20.0f);
}

RB_TEST(text, sources_are_the_glyphs_atlas_rectangles) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    collected     out = layout(&style, 0.0f, 0.0f, "A\xC3\xA8");
    RB_REQUIRE_EQ_UINT(out.count, 2);
    rb_rect a = rb_font_get_glyph(test_font(), 'A').source;
    rb_rect missing = rb_font_get_glyph(test_font(), 0xE8).source;
    EXPECT_F32_NEAR(out.source[0].x, a.x);
    EXPECT_F32_NEAR(out.source[0].y, a.y);
    EXPECT_F32_NEAR(out.source[0].width, a.width);
    EXPECT_F32_NEAR(out.source[1].x, missing.x);
    EXPECT_F32_NEAR(out.source[1].y, missing.y);
}

RB_TEST(text, spaces_move_the_pen_without_a_glyph) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    collected     out = layout(&style, 0.0f, 0.0f, "A  B");
    RB_REQUIRE_EQ_UINT(out.count, 2);
    EXPECT_F32_NEAR(out.dest[1].x, 48.0f);
}

RB_TEST(text, horizontal_alignment) {
    rb_text_style center = style16(RB_TEXT_CENTER, RB_TEXT_TOP);
    rb_text_style right = style16(RB_TEXT_RIGHT, RB_TEXT_TOP);
    EXPECT_F32_NEAR(layout(&center, 100.0f, 0.0f, "AB").dest[0].x, 84.0f);
    EXPECT_F32_NEAR(layout(&right, 100.0f, 0.0f, "AB").dest[0].x, 68.0f);
}

/* Each line centered on its own width, the second one 20 below the first. */
RB_TEST(text, each_line_is_aligned_on_its_own) {
    rb_text_style style = style16(RB_TEXT_CENTER, RB_TEXT_TOP);
    collected     out = layout(&style, 100.0f, 0.0f, "AB\nC");
    RB_REQUIRE_EQ_UINT(out.count, 3);
    EXPECT_F32_NEAR(out.dest[0].x, 84.0f);
    EXPECT_F32_NEAR(out.dest[2].x, 92.0f);
    EXPECT_F32_NEAR(out.dest[2].y, 20.0f);
}

RB_TEST(text, vertical_alignment) {
    rb_text_style middle = style16(RB_TEXT_LEFT, RB_TEXT_MIDDLE);
    rb_text_style baseline = style16(RB_TEXT_LEFT, RB_TEXT_BASELINE);
    rb_text_style bottom = style16(RB_TEXT_LEFT, RB_TEXT_BOTTOM);
    EXPECT_F32_NEAR(layout(&middle, 0.0f, 100.0f, "A").dest[0].y, 92.0f);
    EXPECT_F32_NEAR(layout(&baseline, 0.0f, 100.0f, "A").dest[0].y, 86.0f);
    EXPECT_F32_NEAR(layout(&bottom, 0.0f, 100.0f, "A").dest[0].y, 84.0f);
}

/* Middle centers the whole block: 36 high, so it starts 18 above the anchor. */
RB_TEST(text, vertical_alignment_of_a_block) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_MIDDLE);
    collected     out = layout(&style, 0.0f, 100.0f, "A\nB");
    RB_REQUIRE_EQ_UINT(out.count, 2);
    EXPECT_F32_NEAR(out.dest[0].y, 82.0f);
    EXPECT_F32_NEAR(out.dest[1].y, 102.0f);
}

/* World units: a line half a unit high, glyphs scaled by 1/16. */
RB_TEST(text, any_space_any_size) {
    rb_text_style style = style16(RB_TEXT_LEFT, RB_TEXT_TOP);
    style.size = 0.5f;
    collected out = layout(&style, 1.0f, 2.0f, "AB");
    RB_REQUIRE_EQ_UINT(out.count, 2);
    EXPECT_F32_NEAR(out.dest[0].width, 0.5f);
    EXPECT_F32_NEAR(out.dest[1].x, 1.5f);
    EXPECT_F32_NEAR(rb_text_measure(&style, "AB").x, 1.0f);
}
