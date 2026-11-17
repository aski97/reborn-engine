/*
 * Frames in flight (ADR 0013, S2 and U1).
 */
#include "renderer/vulkan/vulkan_frame.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

static bool frame_create_commands(rb_vk_frame *frame, const rb_vk_device *device) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        /*
         * TRANSIENT: buffers live one frame, a hint for the driver's memory.
         * No RESET_COMMAND_BUFFER_BIT: the pool is reset whole (U1).
         */
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device->queue_family,
    };
    VkResult result =
        device->vk.vkCreateCommandPool(device->handle, &pool_info, NULL, &frame->command_pool);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateCommandPool failed: %s", rb_vk_result_name(result));
        frame->command_pool = VK_NULL_HANDLE;
        return false;
    }
    VkCommandBufferAllocateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = frame->command_pool,
        /* Primary: submitted to a queue (secondary ones are called from a primary). */
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    result = device->vk.vkAllocateCommandBuffers(device->handle, &buffer_info, &frame->commands);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkAllocateCommandBuffers failed: %s", rb_vk_result_name(result));
        frame->commands = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool frame_create_sync(rb_vk_frame *frame, const rb_vk_device *device) {
    VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkResult              result =
        device->vk.vkCreateSemaphore(device->handle, &semaphore_info, NULL, &frame->image_acquired);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateSemaphore failed: %s", rb_vk_result_name(result));
        frame->image_acquired = VK_NULL_HANDLE;
        return false;
    }
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    result = device->vk.vkCreateFence(device->handle, &fence_info, NULL, &frame->done);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateFence failed: %s", rb_vk_result_name(result));
        frame->done = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool rb_vk_frame_create(rb_vk_frame *frame, const rb_vk_device *device) {
    RB_ASSERT(frame != NULL && device != NULL && device->handle != VK_NULL_HANDLE);
    memset(frame, 0, sizeof(*frame));
    return frame_create_commands(frame, device) && frame_create_sync(frame, device);
}

void rb_vk_frame_destroy(rb_vk_frame *frame, const rb_vk_device *device) {
    RB_ASSERT(frame != NULL && device != NULL);
    if (frame->done != VK_NULL_HANDLE) {
        device->vk.vkDestroyFence(device->handle, frame->done, NULL);
    }
    if (frame->image_acquired != VK_NULL_HANDLE) {
        device->vk.vkDestroySemaphore(device->handle, frame->image_acquired, NULL);
    }
    /* The command buffer goes with its pool. */
    if (frame->command_pool != VK_NULL_HANDLE) {
        device->vk.vkDestroyCommandPool(device->handle, frame->command_pool, NULL);
    }
    memset(frame, 0, sizeof(*frame));
}
