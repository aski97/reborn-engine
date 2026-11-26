/*
 * Frame capture without a GPU (ADR 0024): the pixel conversion. Captures on a
 * real window are in test_vulkan_renderer.c.
 */
#include "rb_test.h"

#include "renderer/vulkan/vulkan_capture.h"

RB_TEST(vulkan_capture, bgra_is_swapped_and_made_opaque) {
    u8 pixels[8] = {1, 2, 3, 0, 10, 20, 30, 128};
    RB_REQUIRE(rb_vk_capture_convert(pixels, 2, VK_FORMAT_B8G8R8A8_SRGB));
    static const u8 expected[8] = {3, 2, 1, 255, 30, 20, 10, 255};
    for (int i = 0; i < 8; ++i) {
        RB_EXPECT_EQ_UINT(pixels[i], expected[i]);
    }
}

RB_TEST(vulkan_capture, rgba_keeps_its_order) {
    u8 pixels[4] = {1, 2, 3, 4};
    RB_REQUIRE(rb_vk_capture_convert(pixels, 1, VK_FORMAT_R8G8B8A8_UNORM));
    RB_EXPECT_EQ_UINT(pixels[0], 1u);
    RB_EXPECT_EQ_UINT(pixels[2], 3u);
    RB_EXPECT_EQ_UINT(pixels[3], 255u);
}

RB_TEST(vulkan_capture, other_formats_are_refused_untouched) {
    u8 pixels[4] = {1, 2, 3, 4};
    RB_EXPECT(!rb_vk_capture_supports(VK_FORMAT_A2B10G10R10_UNORM_PACK32));
    RB_EXPECT(!rb_vk_capture_convert(pixels, 1, VK_FORMAT_R16G16B16A16_SFLOAT));
    RB_EXPECT_EQ_UINT(pixels[0], 1u);
    RB_EXPECT_EQ_UINT(pixels[3], 4u);
}
