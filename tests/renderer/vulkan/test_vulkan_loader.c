#include "rb_test.h"

#include "renderer/vulkan/vulkan_loader.h"

#include <string.h>

/*
 * The success tests need the Vulkan loader installed: it comes with every GPU
 * driver that supports Vulkan and, in the Linux build container, with
 * `libvulkan-dev`. No GPU is needed: these functions are the loader's own.
 */

static bool loader_is_zero(const rb_vk_loader *loader) {
    static const rb_vk_loader zero;
    return memcmp(loader, &zero, sizeof(zero)) == 0;
}

RB_TEST(vulkan_loader, opens_and_fills_global_table) {
    rb_vk_loader loader;
    RB_REQUIRE(rb_vk_loader_open(&loader));
    RB_EXPECT(loader.library != NULL);
    RB_EXPECT(loader.vkGetInstanceProcAddr != NULL);
#define TEST_EXPECT_LOADED(fn) RB_EXPECT(loader.global.fn != NULL);
    RB_VK_GLOBAL_FUNCTIONS(TEST_EXPECT_LOADED)
#undef TEST_EXPECT_LOADED
    rb_vk_loader_close(&loader);
}

RB_TEST(vulkan_loader, instance_version_is_at_least_1_0) {
    rb_vk_loader loader;
    RB_REQUIRE(rb_vk_loader_open(&loader));
    u32 version = rb_vk_loader_instance_version(&loader);
    RB_EXPECT(version >= VK_API_VERSION_1_0);
    /* Variant 0 is Vulkan; other variants are derived APIs (Vulkan SC). */
    RB_EXPECT_EQ_UINT(VK_API_VERSION_VARIANT(version), 0);
    RB_EXPECT_EQ_UINT(VK_API_VERSION_MAJOR(version), 1);
    rb_vk_loader_close(&loader);
}

RB_TEST(vulkan_loader, close_zeroes) {
    rb_vk_loader loader;
    RB_REQUIRE(rb_vk_loader_open(&loader));
    rb_vk_loader_close(&loader);
    RB_EXPECT(loader_is_zero(&loader));
}

/* ============================================================================
 * Failures: the loader is left zeroed, nothing to close
 * ========================================================================== */

RB_TEST(vulkan_loader, missing_library_fails) {
    rb_vk_loader loader;
    memset(&loader, 0xAB, sizeof(loader));
#if defined(RB_PLATFORM_WINDOWS)
    RB_EXPECT(!rb_vk_loader_open_library(&loader, "rb_no_such_vulkan.dll"));
#else
    RB_EXPECT(!rb_vk_loader_open_library(&loader, "librb_no_such_vulkan.so"));
#endif
    RB_EXPECT(loader_is_zero(&loader));
}

/* A real library without vkGetInstanceProcAddr: opened, then closed again. */
RB_TEST(vulkan_loader, library_without_entry_point_fails) {
    rb_vk_loader loader;
    memset(&loader, 0xAB, sizeof(loader));
#if defined(RB_PLATFORM_WINDOWS)
    RB_EXPECT(!rb_vk_loader_open_library(&loader, "ucrtbase.dll"));
#else
    RB_EXPECT(!rb_vk_loader_open_library(&loader, "libc.so.6"));
#endif
    RB_EXPECT(loader_is_zero(&loader));
}
