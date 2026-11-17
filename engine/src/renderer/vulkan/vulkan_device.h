/*
 * Vulkan device (ADR 0013, L2, M1 and N1): the GPU the renderer works with.
 *
 * A physical device is a GPU as the installation lists it (a CPU
 * implementation such as lavapipe included): it is chosen, not created. The
 * logical device is the engine's session with it, opened with the features,
 * extensions and queues the engine uses; device-level functions belong to it.
 */
#ifndef RB_VULKAN_DEVICE_H
#define RB_VULKAN_DEVICE_H

#include <reborn/defines.h>
#include <reborn/memory.h>

#include "renderer/vulkan/vulkan_functions.h"
#include "renderer/vulkan/vulkan_instance.h"

typedef struct rb_vk_device {
    VkPhysicalDevice           physical;
    VkPhysicalDeviceProperties properties; /* name, type, version, limits */
    VkDevice                   handle;
    rb_vk_device_functions     vk;
    /* One family does graphics and presents to the surface (N1): one queue for both. */
    u32     queue_family;
    VkQueue queue;
} rb_vk_device;

/*
 * Picks the best GPU that can present to `surface` and opens a device on it
 * with VK_KHR_swapchain, dynamic rendering and synchronization2. `allocator`
 * serves the temporary lists (GPUs, queue families, extensions). False
 * (logged): no suitable GPU or a Vulkan error; *device is zeroed then.
 */
bool rb_vk_device_create(rb_vk_device *device, const rb_vk_instance *instance,
                         rb_allocator allocator, VkSurfaceKHR surface);

/* Waits for the device to go idle, destroys it, zeroes *device. A zeroed device is ignored. */
void rb_vk_device_destroy(rb_vk_device *device);

/*
 * Preference among suitable GPUs (L2), higher is better: discrete, integrated,
 * virtual, CPU, then anything else. Ties keep the loader's order.
 */
u32 rb_vk_device_type_rank(VkPhysicalDeviceType type);

/* "discrete", "integrated", "virtual", "CPU", "other": for logs. */
const char *rb_vk_device_type_name(VkPhysicalDeviceType type);

#endif /* RB_VULKAN_DEVICE_H */
