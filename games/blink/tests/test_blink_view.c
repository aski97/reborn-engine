/* Blink's view layer: the window icon (ADR 0020), drawn without a window. */
#include "rb_test.h"

#include "blink_view.h"

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
