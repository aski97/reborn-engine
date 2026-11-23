/*
 * Device memory (ADR 0017, B1).
 */
#include "renderer/vulkan/vulkan_memory.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

u32 rb_vk_memory_find_type(const VkPhysicalDeviceMemoryProperties *memory, u32 allowed_types,
                           VkMemoryPropertyFlags required) {
    RB_ASSERT(memory != NULL);
    for (u32 i = 0; i < memory->memoryTypeCount; ++i) {
        bool allowed = (allowed_types & (1u << i)) != 0;
        if (allowed && (memory->memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return RB_VK_MEMORY_TYPE_NONE;
}

bool rb_vk_memory_allocate(const rb_vk_device *device, const VkMemoryRequirements *requirements,
                           VkMemoryPropertyFlags preferred, VkMemoryPropertyFlags required,
                           const char *what, VkDeviceMemory *memory) {
    RB_ASSERT(device != NULL && requirements != NULL && what != NULL && memory != NULL);
    RB_ASSERT((preferred & required) == required);
    *memory = VK_NULL_HANDLE;
    u32 type = rb_vk_memory_find_type(&device->memory, requirements->memoryTypeBits, preferred);
    if (type == RB_VK_MEMORY_TYPE_NONE) {
        type = rb_vk_memory_find_type(&device->memory, requirements->memoryTypeBits, required);
    }
    if (type == RB_VK_MEMORY_TYPE_NONE) {
        RB_LOG_ERROR("No memory type fits the %s (allowed types 0x%x)", what,
                     requirements->memoryTypeBits);
        return false;
    }
    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        /* Includes the padding the resource needs: may exceed the bytes the caller asked for. */
        .allocationSize = requirements->size,
        .memoryTypeIndex = type,
    };
    VkResult result = device->vk.vkAllocateMemory(device->handle, &info, NULL, memory);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkAllocateMemory (%s, %llu bytes) failed: %s", what,
                     (unsigned long long)requirements->size, rb_vk_result_name(result));
        *memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* ============================================================================
 * Buffers
 * ========================================================================== */

bool rb_vk_buffer_create_host(rb_vk_buffer *buffer, const rb_vk_device *device, VkDeviceSize size,
                              VkBufferUsageFlags usage) {
    RB_ASSERT(buffer != NULL && device != NULL && device->handle != VK_NULL_HANDLE);
    RB_ASSERT(size > 0);
    memset(buffer, 0, sizeof(*buffer));
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        /* One queue family (ADR 0013, N1). */
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = device->vk.vkCreateBuffer(device->handle, &info, NULL, &buffer->handle);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateBuffer (%llu bytes) failed: %s", (unsigned long long)size,
                     rb_vk_result_name(result));
        buffer->handle = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements requirements;
    device->vk.vkGetBufferMemoryRequirements(device->handle, buffer->handle, &requirements);
    /* Coherent: CPU writes reach the GPU without a flush. Vulkan guarantees such a type. */
    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!rb_vk_memory_allocate(device, &requirements, host, host, "buffer", &buffer->memory)) {
        rb_vk_buffer_destroy(buffer, device);
        return false;
    }
    result = device->vk.vkBindBufferMemory(device->handle, buffer->handle, buffer->memory, 0);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkBindBufferMemory failed: %s", rb_vk_result_name(result));
        rb_vk_buffer_destroy(buffer, device);
        return false;
    }
    buffer->size = size;
    return true;
}

void rb_vk_buffer_destroy(rb_vk_buffer *buffer, const rb_vk_device *device) {
    RB_ASSERT(buffer != NULL && device != NULL);
    /* The buffer first: memory must not be freed while a resource is bound to it. */
    if (buffer->handle != VK_NULL_HANDLE) {
        device->vk.vkDestroyBuffer(device->handle, buffer->handle, NULL);
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        device->vk.vkFreeMemory(device->handle, buffer->memory, NULL);
    }
    memset(buffer, 0, sizeof(*buffer));
}

bool rb_vk_buffer_write(const rb_vk_buffer *buffer, const rb_vk_device *device, const void *data,
                        VkDeviceSize size) {
    RB_ASSERT(buffer != NULL && device != NULL && data != NULL);
    RB_ASSERT(buffer->memory != VK_NULL_HANDLE && size <= buffer->size);
    void    *mapped = NULL;
    VkResult result =
        device->vk.vkMapMemory(device->handle, buffer->memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkMapMemory failed: %s", rb_vk_result_name(result));
        return false;
    }
    memcpy(mapped, data, (usize)size);
    device->vk.vkUnmapMemory(device->handle, buffer->memory);
    return true;
}
