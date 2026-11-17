/*
 * GPU ranking (ADR 0013, L2 and M1). Pure: no GPU needed. Choosing a real
 * device needs a window, see test_vulkan_renderer.c.
 */
#include "rb_test.h"

#include "renderer/vulkan/vulkan_device.h"

RB_TEST(vulkan_device, discrete_before_integrated_before_virtual_before_cpu) {
    RB_EXPECT(rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) >
              rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU));
    RB_EXPECT(rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) >
              rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU));
    RB_EXPECT(rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) >
              rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_CPU));
    RB_EXPECT(rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_CPU) >
              rb_vk_device_type_rank(VK_PHYSICAL_DEVICE_TYPE_OTHER));
}

RB_TEST(vulkan_device, type_names) {
    RB_EXPECT_EQ_STR(rb_vk_device_type_name(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU), "discrete");
    RB_EXPECT_EQ_STR(rb_vk_device_type_name(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU), "integrated");
    RB_EXPECT_EQ_STR(rb_vk_device_type_name(VK_PHYSICAL_DEVICE_TYPE_CPU), "CPU");
    RB_EXPECT_EQ_STR(rb_vk_device_type_name(VK_PHYSICAL_DEVICE_TYPE_OTHER), "other");
}
