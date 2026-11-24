/*
 * Glyph atlas packing and filling (ADR 0018, C1). No GPU: byte arrays only.
 */
#include "rb_test.h"

#include "text/font_atlas.h"

#define EXPECT_RECT(r, ex, ey, ew, eh)                                                             \
    do {                                                                                           \
        RB_EXPECT_EQ_UINT((u32)(r).x, (ex));                                                       \
        RB_EXPECT_EQ_UINT((u32)(r).y, (ey));                                                       \
        RB_EXPECT_EQ_UINT((u32)(r).width, (ew));                                                   \
        RB_EXPECT_EQ_UINT((u32)(r).height, (eh));                                                  \
    } while (0)

/* Coverage is not read by the packer: any non-NULL pointer will do. */
static const u8 any_coverage[64];

/* ============================================================================
 * Packing
 * ========================================================================== */

/* Three 4x4 images in a 20-pixel row with padding 1: x = 0, 5, 10. */
RB_TEST(font_atlas, images_go_left_to_right_with_padding) {
    rb_glyph_image images[3] = {{4, 4, any_coverage}, {4, 4, any_coverage}, {4, 4, any_coverage}};
    rb_rect        rects[3];
    u32            height = 0;
    RB_REQUIRE(rb_font_atlas_pack(images, 3, 20, 1, rects, &height));
    EXPECT_RECT(rects[0], 0, 0, 4, 4);
    EXPECT_RECT(rects[1], 5, 0, 4, 4);
    EXPECT_RECT(rects[2], 10, 0, 4, 4);
    RB_EXPECT_EQ_UINT(height, 4);
}

/* The third would end at 14 in a 12-pixel row: it opens a row below, padding included. */
RB_TEST(font_atlas, an_image_that_does_not_fit_opens_a_row) {
    rb_glyph_image images[3] = {{4, 4, any_coverage}, {4, 6, any_coverage}, {4, 4, any_coverage}};
    rb_rect        rects[3];
    u32            height = 0;
    RB_REQUIRE(rb_font_atlas_pack(images, 3, 12, 1, rects, &height));
    EXPECT_RECT(rects[1], 5, 0, 4, 6);
    /* The first row is as tall as its tallest image (6), then one pixel of padding. */
    EXPECT_RECT(rects[2], 0, 7, 4, 4);
    RB_EXPECT_EQ_UINT(height, 11);
}

/* Padding goes between images, not after the last of a row: 4 + 1 + 4 + 1 + 4 = 14 fits 14. */
RB_TEST(font_atlas, a_row_fills_up_to_the_last_pixel) {
    rb_glyph_image images[3] = {{4, 4, any_coverage}, {4, 4, any_coverage}, {4, 4, any_coverage}};
    rb_rect        rects[3];
    u32            height = 0;
    RB_REQUIRE(rb_font_atlas_pack(images, 3, 14, 1, rects, &height));
    EXPECT_RECT(rects[2], 10, 0, 4, 4);
    RB_EXPECT_EQ_UINT(height, 4);
}

/* A space has no image: an empty rectangle, and the next image takes its place. */
RB_TEST(font_atlas, empty_images_take_no_space) {
    rb_glyph_image images[3] = {{4, 4, any_coverage}, {0, 0, NULL}, {4, 4, any_coverage}};
    rb_rect        rects[3];
    u32            height = 0;
    RB_REQUIRE(rb_font_atlas_pack(images, 3, 20, 1, rects, &height));
    EXPECT_RECT(rects[1], 0, 0, 0, 0);
    EXPECT_RECT(rects[2], 5, 0, 4, 4);
}

RB_TEST(font_atlas, only_empty_images_give_no_height) {
    rb_glyph_image images[2] = {{0, 0, NULL}, {5, 0, NULL}};
    rb_rect        rects[2];
    u32            height = 99;
    RB_REQUIRE(rb_font_atlas_pack(images, 2, 20, 1, rects, &height));
    RB_EXPECT_EQ_UINT(height, 0);
}

/* As wide as the atlas: fits, alone on its row, even after another image. */
RB_TEST(font_atlas, an_image_as_wide_as_the_atlas_fits) {
    rb_glyph_image images[2] = {{4, 4, any_coverage}, {16, 2, any_coverage}};
    rb_rect        rects[2];
    u32            height = 0;
    RB_REQUIRE(rb_font_atlas_pack(images, 2, 16, 1, rects, &height));
    EXPECT_RECT(rects[1], 0, 5, 16, 2);
    RB_EXPECT_EQ_UINT(height, 7);
}

RB_TEST(font_atlas, an_image_wider_than_the_atlas_fails) {
    rb_glyph_image images[1] = {{17, 4, any_coverage}};
    rb_rect        rects[1];
    u32            height = 0;
    RB_EXPECT(!rb_font_atlas_pack(images, 1, 16, 1, rects, &height));
}

/* ============================================================================
 * Filling
 * ========================================================================== */

/* A 2x2 image at (1, 0) of a 4x2 atlas: its coverage in alpha, white everywhere. */
RB_TEST(font_atlas, fill_writes_coverage_as_alpha_over_white) {
    static const u8 coverage[4] = {10, 20, 30, 40};
    rb_glyph_image  image = {2, 2, coverage};
    rb_rect         rect = {1.0f, 0.0f, 2.0f, 2.0f};
    u8              pixels[4 * 2 * 4];
    rb_font_atlas_fill(pixels, 4, 2, &image, &rect, 1);

    static const u8 expected_alpha[4 * 2] = {0, 10, 20, 0, 0, 30, 40, 0};
    for (u32 i = 0; i < 4 * 2; ++i) {
        RB_EXPECT_EQ_UINT(pixels[i * 4 + 0], 255);
        RB_EXPECT_EQ_UINT(pixels[i * 4 + 1], 255);
        RB_EXPECT_EQ_UINT(pixels[i * 4 + 2], 255);
        RB_EXPECT_EQ_UINT(pixels[i * 4 + 3], expected_alpha[i]);
    }
}

/* Packed then filled: every image lands where its rectangle says. */
RB_TEST(font_atlas, fill_follows_the_packed_rectangles) {
    static const u8 a[1] = {111};
    static const u8 b[2] = {222, 233};
    rb_glyph_image  images[3] = {{1, 1, a}, {0, 0, NULL}, {1, 2, b}};
    rb_rect         rects[3];
    u32             height = 0;
    RB_REQUIRE(rb_font_atlas_pack(images, 3, 3, 1, rects, &height));
    RB_REQUIRE_EQ_UINT(height, 2);
    u8 pixels[3 * 2 * 4];
    rb_font_atlas_fill(pixels, 3, 2, images, rects, 3);
    /* a at (0, 0); padding at x = 1; b at (2, 0) and (2, 1). */
    RB_EXPECT_EQ_UINT(pixels[(0 * 3 + 0) * 4 + 3], 111);
    RB_EXPECT_EQ_UINT(pixels[(0 * 3 + 1) * 4 + 3], 0);
    RB_EXPECT_EQ_UINT(pixels[(0 * 3 + 2) * 4 + 3], 222);
    RB_EXPECT_EQ_UINT(pixels[(1 * 3 + 2) * 4 + 3], 233);
    RB_EXPECT_EQ_UINT(pixels[(1 * 3 + 0) * 4 + 3], 0);
}
