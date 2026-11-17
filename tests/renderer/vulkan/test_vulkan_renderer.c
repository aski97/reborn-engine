/*
 * Device and swapchain on a real window (ADR 0013, J–Q). Built on Windows
 * only, like test_renderer.c: the Linux container has no X server.
 */
#include "rb_test.h"

#include <reborn/platform.h>

#include "renderer/renderer_internal.h"
#include "renderer/vulkan/vulkan_renderer.h"

static rb_platform_state *platform;
static rb_window          window;

static rb_renderer *start(bool vsync) {
    platform = rb_platform_startup(rb_heap_allocator());
    if (platform == NULL) {
        return NULL;
    }
    window = rb_window_create(platform, &(rb_window_desc){"Vulkan renderer test", 320, 240});
    rb_renderer *renderer = NULL;
    if (rb_window_is_valid(platform, window)) {
        renderer = rb_renderer_startup(rb_heap_allocator(),
                                       &(rb_renderer_config){.validation = true, .vsync = vsync},
                                       platform, window);
        if (renderer != NULL) {
            return renderer;
        }
        rb_window_destroy(platform, window);
    }
    rb_platform_shutdown(platform);
    return NULL;
}

static void stop(rb_renderer *renderer) {
    rb_renderer_shutdown(renderer);
    rb_window_destroy(platform, window);
    rb_platform_shutdown(platform);
}

static void expect_no_validation_messages(const rb_renderer *renderer) {
    rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
    RB_EXPECT_EQ_UINT(stats.errors, 0);
    RB_EXPECT_EQ_UINT(stats.warnings, 0);
}

RB_TEST(vulkan_renderer, device_table_and_queue_are_loaded) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_EXPECT(renderer->surface != VK_NULL_HANDLE);
    RB_EXPECT(renderer->device.handle != VK_NULL_HANDLE);
    RB_EXPECT(renderer->device.queue != VK_NULL_HANDLE);
    RB_EXPECT(renderer->device.properties.apiVersion >= VK_API_VERSION_1_3);
#define TEST_EXPECT_LOADED(fn) RB_EXPECT(renderer->device.vk.fn != NULL);
    RB_VK_DEVICE_FUNCTIONS(TEST_EXPECT_LOADED)
    RB_VK_SWAPCHAIN_FUNCTIONS(TEST_EXPECT_LOADED)
#undef TEST_EXPECT_LOADED
    stop(renderer);
}

RB_TEST(vulkan_renderer, swapchain_matches_the_window) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    rb_window_size         size = rb_window_get_size(platform, window);
    const rb_vk_swapchain *swapchain = &renderer->swapchain;
    RB_EXPECT(swapchain->handle != VK_NULL_HANDLE);
    RB_EXPECT_EQ_UINT(swapchain->extent.width, size.pixel_width);
    RB_EXPECT_EQ_UINT(swapchain->extent.height, size.pixel_height);
    RB_EXPECT_EQ_INT(swapchain->present_mode, VK_PRESENT_MODE_FIFO_KHR);
    RB_EXPECT_EQ_INT(swapchain->format.colorSpace, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    RB_EXPECT(swapchain->image_count >= 2);
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        RB_EXPECT(swapchain->images[i] != VK_NULL_HANDLE);
        RB_EXPECT(swapchain->views[i] != VK_NULL_HANDLE);
    }
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, recreating_replaces_the_swapchain) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkSwapchainKHR first = renderer->swapchain.handle;
    rb_window_size size = rb_window_get_size(platform, window);
    RB_EXPECT(rb_vk_swapchain_create(&renderer->swapchain, &renderer->instance, &renderer->device,
                                     renderer->surface, rb_heap_allocator(), size.pixel_width,
                                     size.pixel_height, true));
    RB_EXPECT(renderer->swapchain.handle != VK_NULL_HANDLE);
    RB_EXPECT(renderer->swapchain.handle != first);
    expect_no_validation_messages(renderer);
    stop(renderer);
}

RB_TEST(vulkan_renderer, prepare_frame_shows_a_visible_window) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkSwapchainKHR first = renderer->swapchain.handle;
    RB_EXPECT(rb_renderer_prepare_frame(renderer));
    /* Same size: nothing to rebuild. */
    RB_EXPECT(renderer->swapchain.handle == first);
    stop(renderer);
}

/* Pretends the swapchain was built for another size, as after a resize. */
RB_TEST(vulkan_renderer, prepare_frame_rebuilds_when_the_size_changed) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    VkSwapchainKHR first = renderer->swapchain.handle;
    renderer->built_width += 1;
    RB_EXPECT(rb_renderer_prepare_frame(renderer));
    RB_EXPECT(renderer->swapchain.handle != first);
    rb_window_size size = rb_window_get_size(platform, window);
    RB_EXPECT_EQ_UINT(renderer->built_width, size.pixel_width);
    expect_no_validation_messages(renderer);
    stop(renderer);
}
