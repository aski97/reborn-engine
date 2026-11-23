/*
 * Memory type choice (ADR 0017, B1): pure, no GPU needed.
 */
#include "rb_test.h"

#include "renderer/vulkan/vulkan_memory.h"

#define DEVICE_LOCAL  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
#define HOST_VISIBLE  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
#define HOST_COHERENT VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
#define HOST_CACHED   VK_MEMORY_PROPERTY_HOST_CACHED_BIT

/* Shaped like a discrete GPU: its own memory first, then system memory it can reach. */
static VkPhysicalDeviceMemoryProperties discrete_gpu(void) {
    VkPhysicalDeviceMemoryProperties memory = {
        .memoryTypeCount = 4,
        .memoryTypes =
            {
                {DEVICE_LOCAL, 0},
                {HOST_VISIBLE | HOST_COHERENT, 1},
                {HOST_VISIBLE | HOST_COHERENT | HOST_CACHED, 1},
                {DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT, 0}, /* the small BAR window */
            },
        .memoryHeapCount = 2,
    };
    return memory;
}

RB_TEST(vulkan_memory, takes_the_first_type_with_the_flags) {
    VkPhysicalDeviceMemoryProperties memory = discrete_gpu();
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xF, DEVICE_LOCAL), 0);
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xF, HOST_VISIBLE | HOST_COHERENT), 1);
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xF, HOST_CACHED), 2);
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xF, DEVICE_LOCAL | HOST_VISIBLE), 3);
}

RB_TEST(vulkan_memory, skips_types_the_resource_does_not_allow) {
    VkPhysicalDeviceMemoryProperties memory = discrete_gpu();
    /* Types 1 and 3 only: the device-local one is type 3. */
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xA, DEVICE_LOCAL), 3);
    /* Type 2 only. */
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0x4, HOST_VISIBLE), 2);
}

RB_TEST(vulkan_memory, no_flags_take_the_first_allowed_type) {
    VkPhysicalDeviceMemoryProperties memory = discrete_gpu();
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xF, 0), 0);
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0x6, 0), 1);
}

RB_TEST(vulkan_memory, none_fits) {
    VkPhysicalDeviceMemoryProperties memory = discrete_gpu();
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0x1, HOST_VISIBLE), RB_VK_MEMORY_TYPE_NONE);
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0x0, 0), RB_VK_MEMORY_TYPE_NONE);
    /* Bits beyond memoryTypeCount name no type. */
    RB_EXPECT_EQ_UINT(rb_vk_memory_find_type(&memory, 0xF0, 0), RB_VK_MEMORY_TYPE_NONE);
}
