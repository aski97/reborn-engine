/*
 * Vulkan instance internals (ADR 0013): the instance table, the surface
 * extensions, and a real validation error travelling from the layer to our
 * counters. The instance is created directly, without a window, so these run
 * in the Linux container too.
 */
#include "rb_test.h"

#include <string.h>

#include "platform/platform_vulkan.h"
#include "renderer/vulkan/vulkan_instance.h"

typedef struct fixture {
    rb_vk_loader   loader;
    rb_vk_instance instance;
    rb_vk_debug    debug; /* must outlive the instance: static storage below */
} fixture;

static fixture fx;

static bool start(const char *surface_extension, bool validation) {
    memset(&fx, 0, sizeof(fx));
    if (!rb_vk_loader_open(&fx.loader)) {
        return false;
    }
    if (!rb_vk_instance_create(&fx.instance, &fx.loader, rb_heap_allocator(), surface_extension,
                               validation, &fx.debug)) {
        rb_vk_loader_close(&fx.loader);
        return false;
    }
    return true;
}

static void stop(void) {
    rb_vk_instance_destroy(&fx.instance);
    rb_vk_loader_close(&fx.loader);
}

RB_TEST(vulkan_instance, table_is_loaded) {
    RB_REQUIRE(start(rb_platform_vulkan_surface_extension(), true));
    RB_EXPECT(fx.instance.handle != VK_NULL_HANDLE);
#define TEST_EXPECT_LOADED(fn) RB_EXPECT(fx.instance.vk.fn != NULL);
    RB_VK_INSTANCE_FUNCTIONS(TEST_EXPECT_LOADED)
    RB_VK_SURFACE_FUNCTIONS(TEST_EXPECT_LOADED)
    RB_VK_DEBUG_UTILS_FUNCTIONS(TEST_EXPECT_LOADED)
#undef TEST_EXPECT_LOADED
    RB_EXPECT(fx.instance.messenger != VK_NULL_HANDLE);
    stop();
}

RB_TEST(vulkan_instance, no_surface_table_without_surface_extension) {
    RB_REQUIRE(start(NULL, false));
    RB_EXPECT(fx.instance.vk.vkDestroyInstance != NULL);
    RB_EXPECT(fx.instance.vk.vkDestroySurfaceKHR == NULL);
    stop();
}

RB_TEST(vulkan_instance, no_messenger_without_validation) {
    RB_REQUIRE(start(NULL, false));
    RB_EXPECT(fx.instance.messenger == VK_NULL_HANDLE);
    RB_EXPECT(fx.instance.vk.vkCreateDebugUtilsMessengerEXT == NULL);
    stop();
}

/* Unlike a missing validation layer, a missing surface extension leaves nothing to fall back to. */
RB_TEST(vulkan_instance, unknown_surface_extension_fails) {
    RB_REQUIRE(rb_vk_loader_open(&fx.loader));
    RB_EXPECT(!rb_vk_instance_create(&fx.instance, &fx.loader, rb_heap_allocator(),
                                     "VK_RB_no_such_surface", false, &fx.debug));
    RB_EXPECT(fx.instance.handle == VK_NULL_HANDLE);
    rb_vk_loader_close(&fx.loader);
}

/*
 * A messenger asking for no severity breaks a rule of the spec
 * (VUID-VkDebugUtilsMessengerCreateInfoEXT-messageSeverity-parameter):
 * the layer must report it through our callback, and the counter must see it.
 * Under a debugger this stops in the callback, as designed.
 */
RB_TEST(vulkan_instance, validation_error_is_counted) {
    RB_REQUIRE(start(NULL, true));
    RB_REQUIRE(fx.debug.stats.enabled);

    VkDebugUtilsMessengerCreateInfoEXT info = rb_vk_debug_messenger_info(&fx.debug);
    info.messageSeverity = 0;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkResult                 result =
        fx.instance.vk.vkCreateDebugUtilsMessengerEXT(fx.instance.handle, &info, NULL, &messenger);
    /* The layer reports and lets the call through: whatever was created is destroyed. */
    if (result == VK_SUCCESS && messenger != VK_NULL_HANDLE) {
        fx.instance.vk.vkDestroyDebugUtilsMessengerEXT(fx.instance.handle, messenger, NULL);
    }

    RB_EXPECT(fx.debug.stats.errors >= 1);
    RB_EXPECT_EQ_UINT(fx.debug.seen_count, 1);
    stop();
}
