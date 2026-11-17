/*
 * Swapchain choices (ADR 0013, O2 and P1), fed with made-up driver answers.
 * Pure: no GPU needed. A real swapchain needs a window, see
 * test_vulkan_renderer.c.
 */
#include "rb_test.h"

#include "renderer/vulkan/vulkan_swapchain.h"

/* ============================================================================
 * Format
 * ========================================================================== */

RB_TEST(vulkan_swapchain, format_prefers_bgra_srgb) {
    VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    VkSurfaceFormatKHR chosen = rb_vk_swapchain_choose_format(formats, 3);
    RB_EXPECT_EQ_INT(chosen.format, VK_FORMAT_B8G8R8A8_SRGB);
}

RB_TEST(vulkan_swapchain, format_falls_back_to_rgba_srgb) {
    VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    VkSurfaceFormatKHR chosen = rb_vk_swapchain_choose_format(formats, 2);
    RB_EXPECT_EQ_INT(chosen.format, VK_FORMAT_R8G8B8A8_SRGB);
}

/* sRGB pixels in an HDR color space would be shown with the wrong curve. */
RB_TEST(vulkan_swapchain, format_needs_the_srgb_color_space) {
    VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_HDR10_ST2084_EXT},
    };
    VkSurfaceFormatKHR chosen = rb_vk_swapchain_choose_format(formats, 2);
    RB_EXPECT_EQ_INT(chosen.format, VK_FORMAT_R16G16B16A16_SFLOAT);
}

RB_TEST(vulkan_swapchain, format_without_srgb_takes_the_first) {
    VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    VkSurfaceFormatKHR chosen = rb_vk_swapchain_choose_format(formats, 2);
    RB_EXPECT_EQ_INT(chosen.format, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
}

/* ============================================================================
 * Present mode
 * ========================================================================== */

static const VkPresentModeKHR all_modes[] = {
    VK_PRESENT_MODE_IMMEDIATE_KHR,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR,
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_FIFO_KHR,
};

RB_TEST(vulkan_swapchain, vsync_is_fifo_whatever_is_offered) {
    RB_EXPECT_EQ_INT(rb_vk_swapchain_choose_present_mode(all_modes, 4, true),
                     VK_PRESENT_MODE_FIFO_KHR);
}

RB_TEST(vulkan_swapchain, no_vsync_prefers_mailbox) {
    RB_EXPECT_EQ_INT(rb_vk_swapchain_choose_present_mode(all_modes, 4, false),
                     VK_PRESENT_MODE_MAILBOX_KHR);
}

RB_TEST(vulkan_swapchain, no_vsync_without_mailbox_is_immediate) {
    VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    RB_EXPECT_EQ_INT(rb_vk_swapchain_choose_present_mode(modes, 2, false),
                     VK_PRESENT_MODE_IMMEDIATE_KHR);
}

RB_TEST(vulkan_swapchain, no_vsync_with_fifo_only_is_fifo) {
    VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    RB_EXPECT_EQ_INT(rb_vk_swapchain_choose_present_mode(modes, 2, false),
                     VK_PRESENT_MODE_FIFO_KHR);
}

/* ============================================================================
 * Extent and image count
 * ========================================================================== */

RB_TEST(vulkan_swapchain, extent_follows_the_surface_when_stated) {
    VkSurfaceCapabilitiesKHR caps = {
        .currentExtent = {800, 600},
        .minImageExtent = {1, 1},
        .maxImageExtent = {4096, 4096},
    };
    VkExtent2D extent = rb_vk_swapchain_choose_extent(&caps, 1280, 720);
    RB_EXPECT_EQ_UINT(extent.width, 800);
    RB_EXPECT_EQ_UINT(extent.height, 600);
}

/* Zero is what Windows states for a minimized window: no swapchain then. */
RB_TEST(vulkan_swapchain, extent_of_a_minimized_surface_is_zero) {
    VkSurfaceCapabilitiesKHR caps = {.currentExtent = {0, 0}, .maxImageExtent = {4096, 4096}};
    VkExtent2D               extent = rb_vk_swapchain_choose_extent(&caps, 1280, 720);
    RB_EXPECT_EQ_UINT(extent.width, 0);
    RB_EXPECT_EQ_UINT(extent.height, 0);
}

RB_TEST(vulkan_swapchain, undefined_extent_takes_the_window_clamped) {
    VkSurfaceCapabilitiesKHR caps = {
        .currentExtent = {UINT32_MAX, UINT32_MAX},
        .minImageExtent = {64, 64},
        .maxImageExtent = {1024, 1024},
    };
    VkExtent2D extent = rb_vk_swapchain_choose_extent(&caps, 1280, 32);
    RB_EXPECT_EQ_UINT(extent.width, 1024);
    RB_EXPECT_EQ_UINT(extent.height, 64);
    extent = rb_vk_swapchain_choose_extent(&caps, 300, 200);
    RB_EXPECT_EQ_UINT(extent.width, 300);
    RB_EXPECT_EQ_UINT(extent.height, 200);
}

RB_TEST(vulkan_swapchain, image_count_is_one_above_the_minimum) {
    VkSurfaceCapabilitiesKHR caps = {.minImageCount = 2, .maxImageCount = 0};
    RB_EXPECT_EQ_UINT(rb_vk_swapchain_choose_image_count(&caps), 3);
}

RB_TEST(vulkan_swapchain, image_count_stays_within_the_maximum) {
    VkSurfaceCapabilitiesKHR caps = {.minImageCount = 3, .maxImageCount = 3};
    RB_EXPECT_EQ_UINT(rb_vk_swapchain_choose_image_count(&caps), 3);
}

RB_TEST(vulkan_swapchain, image_count_fits_the_array) {
    VkSurfaceCapabilitiesKHR caps = {.minImageCount = 16, .maxImageCount = 0};
    RB_EXPECT_EQ_UINT(rb_vk_swapchain_choose_image_count(&caps), RB_VK_SWAPCHAIN_MAX_IMAGES);
}

/* ============================================================================
 * Names
 * ========================================================================== */

RB_TEST(vulkan_swapchain, names_for_logs) {
    RB_EXPECT_EQ_STR(rb_vk_format_name(VK_FORMAT_B8G8R8A8_SRGB), "B8G8R8A8_SRGB");
    RB_EXPECT(rb_vk_format_name(VK_FORMAT_D32_SFLOAT) == NULL);
    RB_EXPECT_EQ_STR(rb_vk_present_mode_name(VK_PRESENT_MODE_MAILBOX_KHR), "MAILBOX");
    RB_EXPECT(rb_vk_present_mode_name(VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR) == NULL);
}
