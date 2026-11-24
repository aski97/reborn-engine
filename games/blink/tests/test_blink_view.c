/* Blink's view layer without a window: the HUD's scale (ADR 0021) and the window icon (ADR 0020).
 */
#include "rb_test.h"

#include "blink_view.h"

#define EXPECT_F32_EQ(a, b) RB_EXPECT_NEAR((f64)(a), (f64)(b), 0.0)

/* Whole numbers only: the builtin font's pixels must cover whole window pixels. */
RB_TEST(blink_view, hud_scale_is_one_per_180_pixels) {
    EXPECT_F32_EQ(blink_view_hud_scale(720.0f), 4.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(1080.0f), 6.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(1440.0f), 8.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(2160.0f), 12.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(719.0f), 3.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(562.0f), 3.0f);
}

/* A tiny or minimized window still gets readable text, not a scale of 0. */
RB_TEST(blink_view, hud_scale_is_at_least_one) {
    EXPECT_F32_EQ(blink_view_hud_scale(359.0f), 1.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(100.0f), 1.0f);
    EXPECT_F32_EQ(blink_view_hud_scale(0.0f), 1.0f);
}

static const u8 *pixel(const u8 *pixels, u32 side, u32 x, u32 y) {
    return &pixels[(y * side + x) * 4];
}

RB_TEST(blink_view, icon_is_a_rounded_tile_with_the_square_in_the_middle) {
    static u8 pixels[32 * 32 * 4];
    blink_view_draw_icon(32, pixels);
    /* Outside the rounded corner: transparent. */
    RB_EXPECT_EQ_UINT(pixel(pixels, 32, 0, 0)[3], 0);
    /* The middle: the orange square, opaque. */
    const u8 *middle = pixel(pixels, 32, 16, 16);
    RB_EXPECT_EQ_UINT(middle[0], 250);
    RB_EXPECT_EQ_UINT(middle[1], 179);
    RB_EXPECT_EQ_UINT(middle[2], 51);
    RB_EXPECT_EQ_UINT(middle[3], 255);
    /* The middle of the left edge: the light border, opaque. */
    const u8 *edge = pixel(pixels, 32, 0, 16);
    RB_EXPECT_EQ_UINT(edge[0], 96);
    RB_EXPECT_EQ_UINT(edge[3], 255);
    /* Between border and square: the dark tile. */
    RB_EXPECT_EQ_UINT(pixel(pixels, 32, 4, 16)[0], 24);
}

/* Same picture at every size: the small one is not a crop of the large one. */
RB_TEST(blink_view, icon_scales_with_its_side) {
    static u8 small[16 * 16 * 4];
    static u8 large[64 * 64 * 4];
    blink_view_draw_icon(16, small);
    blink_view_draw_icon(64, large);
    RB_EXPECT_EQ_UINT(pixel(small, 16, 8, 8)[0], 250);
    RB_EXPECT_EQ_UINT(pixel(large, 64, 32, 32)[0], 250);
    RB_EXPECT_EQ_UINT(pixel(small, 16, 0, 0)[3], 0);
    RB_EXPECT_EQ_UINT(pixel(large, 64, 0, 0)[3], 0);
}
