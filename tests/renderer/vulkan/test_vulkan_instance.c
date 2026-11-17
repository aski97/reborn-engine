/*
 * Vulkan instance internals (ADR 0013): the instance table, and a real
 * validation error travelling from the layer to our counters.
 */
#include "rb_test.h"

#include "renderer/vulkan/vulkan_renderer.h"

static rb_renderer *start(bool validation) {
    return rb_renderer_startup(rb_heap_allocator(),
                               &(rb_renderer_config){.validation = validation});
}

RB_TEST(vulkan_instance, table_is_loaded) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_EXPECT(renderer->instance.handle != VK_NULL_HANDLE);
#define TEST_EXPECT_LOADED(fn) RB_EXPECT(renderer->instance.vk.fn != NULL);
    RB_VK_INSTANCE_FUNCTIONS(TEST_EXPECT_LOADED)
    RB_VK_DEBUG_UTILS_FUNCTIONS(TEST_EXPECT_LOADED)
#undef TEST_EXPECT_LOADED
    RB_EXPECT(renderer->instance.messenger != VK_NULL_HANDLE);
    rb_renderer_shutdown(renderer);
}

RB_TEST(vulkan_instance, no_messenger_without_validation) {
    rb_renderer *renderer = start(false);
    RB_REQUIRE(renderer != NULL);
    RB_EXPECT(renderer->instance.messenger == VK_NULL_HANDLE);
    RB_EXPECT(renderer->instance.vk.vkCreateDebugUtilsMessengerEXT == NULL);
    rb_renderer_shutdown(renderer);
}

/*
 * A messenger asking for no severity breaks a rule of the spec
 * (VUID-VkDebugUtilsMessengerCreateInfoEXT-messageSeverity-parameter):
 * the layer must report it through our callback, and the counter must see it.
 * Under a debugger this stops in the callback, as designed.
 */
RB_TEST(vulkan_instance, validation_error_is_counted) {
    rb_renderer *renderer = start(true);
    RB_REQUIRE(renderer != NULL);
    RB_REQUIRE(rb_renderer_get_validation_stats(renderer).enabled);

    VkDebugUtilsMessengerCreateInfoEXT info = rb_vk_debug_messenger_info(&renderer->debug);
    info.messageSeverity = 0;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkResult                 result = renderer->instance.vk.vkCreateDebugUtilsMessengerEXT(
        renderer->instance.handle, &info, NULL, &messenger);
    /* The layer reports and lets the call through: whatever was created is destroyed. */
    if (result == VK_SUCCESS && messenger != VK_NULL_HANDLE) {
        renderer->instance.vk.vkDestroyDebugUtilsMessengerEXT(renderer->instance.handle, messenger,
                                                              NULL);
    }

    rb_renderer_validation_stats stats = rb_renderer_get_validation_stats(renderer);
    RB_EXPECT(stats.errors >= 1);
    RB_EXPECT_EQ_UINT(renderer->debug.seen_count, 1);
    rb_renderer_shutdown(renderer);
}
