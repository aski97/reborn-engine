/* Window icon helpers for Win32 (ADR 0020). Built on Windows only. */
#include "rb_test.h"

#include "platform/win32/win32_icon.h"

static const u8 PIXEL[4] = {0};

static rb_image square(u32 side) {
    return (rb_image){.width = side, .height = side, .pixels = PIXEL};
}

RB_TEST(win32_icon, pick_takes_the_smallest_that_fits) {
    rb_image images[] = {square(64), square(16), square(32)};
    RB_EXPECT_EQ_UINT(win32_icon_pick(images, 3, 16), 1);
    RB_EXPECT_EQ_UINT(win32_icon_pick(images, 3, 20), 2); /* 125%: 32, shrunk */
    RB_EXPECT_EQ_UINT(win32_icon_pick(images, 3, 32), 2);
    RB_EXPECT_EQ_UINT(win32_icon_pick(images, 3, 48), 0);
}

RB_TEST(win32_icon, pick_takes_the_largest_when_none_fits) {
    rb_image images[] = {square(16), square(32)};
    RB_EXPECT_EQ_UINT(win32_icon_pick(images, 2, 64), 1);
    rb_image one[] = {square(16)};
    RB_EXPECT_EQ_UINT(win32_icon_pick(one, 1, 64), 0);
}

RB_TEST(win32_icon, to_bgra_swaps_red_and_blue) {
    static const u8 rgba[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    rb_image        image = {.width = 2, .height = 1, .pixels = rgba};
    u8              bgra[8];
    win32_icon_to_bgra(&image, bgra);
    static const u8 expected[8] = {30, 20, 10, 40, 70, 60, 50, 80};
    for (u32 i = 0; i < 8; ++i) {
        RB_EXPECT_EQ_UINT(bgra[i], expected[i]);
    }
}
