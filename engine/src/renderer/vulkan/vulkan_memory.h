/*
 * Device memory (ADR 0017, B1): one vkAllocateMemory per resource.
 *
 * A GPU offers a few memory types (device-local, host-visible...) on top of
 * its heaps; each buffer or image says which types it can live in, as a bit
 * mask. Allocations are slow and capped in number (4096 guaranteed): fine for
 * the tens of resources a small game has, not for thousands (block allocator
 * in the backlog).
 */
#ifndef RB_VULKAN_MEMORY_H
#define RB_VULKAN_MEMORY_H

#include <reborn/defines.h>

#include "renderer/vulkan/vulkan_device.h"

/* Returned by rb_vk_memory_find_type when no type fits. */
#define RB_VK_MEMORY_TYPE_NONE UINT32_MAX

/*
 * The first memory type allowed by `allowed_types` (bit i set: type i
 * allowed, VkMemoryRequirements.memoryTypeBits) that has every flag in
 * `required`. Vulkan lists the faster types first, so the first fit is the
 * one to take. RB_VK_MEMORY_TYPE_NONE when none fits.
 */
u32 rb_vk_memory_find_type(const VkPhysicalDeviceMemoryProperties *memory, u32 allowed_types,
                           VkMemoryPropertyFlags required);

/*
 * Allocates memory for a resource with `requirements`, in a type with the
 * `preferred` flags, else in any type that has the `required` ones (a subset
 * of `preferred`). False (logged with `what`): *memory is VK_NULL_HANDLE.
 */
bool rb_vk_memory_allocate(const rb_vk_device *device, const VkMemoryRequirements *requirements,
                           VkMemoryPropertyFlags preferred, VkMemoryPropertyFlags required,
                           const char *what, VkDeviceMemory *memory);

/* A buffer with its own memory. */
typedef struct rb_vk_buffer {
    VkBuffer       handle;
    VkDeviceMemory memory;
    VkDeviceSize   size;
} rb_vk_buffer;

/*
 * A buffer of `size` bytes for `usage`, in host-visible, coherent memory: the
 * CPU writes it with rb_vk_buffer_write, and its writes reach the GPU without
 * a flush. False (logged): *buffer is zeroed.
 */
bool rb_vk_buffer_create_host(rb_vk_buffer *buffer, const rb_vk_device *device, VkDeviceSize size,
                              VkBufferUsageFlags usage);

/*
 * A buffer of `size` bytes the GPU copies into and the CPU reads
 * (TRANSFER_DST): host-visible and coherent, cached by the CPU when the device
 * offers such memory. False (logged): *buffer is zeroed.
 */
bool rb_vk_buffer_create_readback(rb_vk_buffer *buffer, const rb_vk_device *device,
                                  VkDeviceSize size);

/* The GPU must be done with it. Zeroes *buffer; a zeroed buffer is ignored. */
void rb_vk_buffer_destroy(rb_vk_buffer *buffer, const rb_vk_device *device);

/*
 * Copies `size` bytes from `data` to the start of a host-visible buffer. False
 * (logged) when the memory cannot be mapped.
 */
bool rb_vk_buffer_write(const rb_vk_buffer *buffer, const rb_vk_device *device, const void *data,
                        VkDeviceSize size);

/*
 * Copies the first `size` bytes of a host-visible buffer into `data`. The GPU's
 * writes must be done and made visible to the host first (a barrier to the
 * HOST stage, then the fence). False (logged) when the memory cannot be mapped.
 */
bool rb_vk_buffer_read(const rb_vk_buffer *buffer, const rb_vk_device *device, void *data,
                       VkDeviceSize size);

#endif /* RB_VULKAN_MEMORY_H */
