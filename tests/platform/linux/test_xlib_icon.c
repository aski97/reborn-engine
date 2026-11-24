/* _NET_WM_ICON data (ADR 0020). Built on Linux only, no X server needed. */
#include "rb_test.h"

#include "platform/linux/xlib_icon.h"

RB_TEST(xlib_icon, length_counts_sizes_and_pixels) {
    static const u8 pixels[64 * 64 * 4] = {0};
    rb_image        images[] = {{16, 16, pixels}, {32, 32, pixels}};
    RB_EXPECT_EQ_UINT(xlib_icon_length(images, 2), 2 + 256 + 2 + 1024);
    RB_EXPECT_EQ_UINT(xlib_icon_length(images, 0), 0);
}

/* Each value is a whole `unsigned long`: 64 bits here, the pixel in the low 32. */
RB_TEST(xlib_icon, fill_writes_sizes_then_argb_pixels) {
    static const u8 a[2 * 1 * 4] = {0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD};
    static const u8 b[1 * 1 * 4] = {1, 2, 3, 255};
    rb_image        images[] = {{2, 1, a}, {1, 1, b}};
    unsigned long   out[2 + 2 + 2 + 1];
    RB_REQUIRE(xlib_icon_length(images, 2) == RB_ARRAY_COUNT(out));
    xlib_icon_fill(images, 2, out);
    RB_EXPECT_EQ_UINT(out[0], 2);
    RB_EXPECT_EQ_UINT(out[1], 1);
    RB_EXPECT_EQ_UINT(out[2], 0x44112233u);
    RB_EXPECT_EQ_UINT(out[3], 0xDDAABBCCu);
    RB_EXPECT_EQ_UINT(out[4], 1);
    RB_EXPECT_EQ_UINT(out[5], 1);
    RB_EXPECT_EQ_UINT(out[6], 0xFF010203u);
}
