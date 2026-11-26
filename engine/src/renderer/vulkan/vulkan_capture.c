/*
 * Frame capture (ADR 0024).
 */
#include "renderer/vulkan/vulkan_capture.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

#include "renderer/vulkan/vulkan_swapchain.h"

static bool format_is_bgra(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
}

bool rb_vk_capture_supports(VkFormat format) {
    return format_is_bgra(format) || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_R8G8B8A8_UNORM;
}

bool rb_vk_capture_convert(u8 *pixels, usize count, VkFormat format) {
    RB_ASSERT(pixels != NULL || count == 0);
    if (!rb_vk_capture_supports(format)) {
        return false;
    }
    bool swap = format_is_bgra(format);
    for (usize i = 0; i < count; ++i) {
        u8 *p = pixels + i * 4;
        if (swap) {
            u8 blue = p[0];
            p[0] = p[2];
            p[2] = blue;
        }
        /*
         * What the frame left in alpha (blending writes it too) is not part of
         * what the window shows: the window is opaque.
         */
        p[3] = 255;
    }
    return true;
}

bool rb_vk_capture_record(rb_vk_capture *capture, const rb_vk_device *device,
                          VkCommandBuffer commands, VkImage image, VkFormat format,
                          VkExtent2D extent) {
    RB_ASSERT(capture != NULL && device != NULL && commands != VK_NULL_HANDLE);
    RB_ASSERT(capture->buffer.handle == VK_NULL_HANDLE);
    if (!rb_vk_capture_supports(format)) {
        const char *name = rb_vk_format_name(format);
        RB_LOG_ERROR("Capture: images in %s cannot be read back",
                     name != NULL ? name : "this format");
        return false;
    }
    VkDeviceSize size = (VkDeviceSize)extent.width * extent.height * 4;
    if (!rb_vk_buffer_create_readback(&capture->buffer, device, size)) {
        return false;
    }
    capture->format = format;
    capture->extent = extent;

    const rb_vk_device_functions *vk = &device->vk;
    VkImageSubresourceRange       range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };
    /* The drawing's writes done and visible to the copy, in the layout copies read from. */
    VkImageMemoryBarrier2 to_copy = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
    vk->vkCmdPipelineBarrier2(commands, &(VkDependencyInfo){
                                            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                            .imageMemoryBarrierCount = 1,
                                            .pImageMemoryBarriers = &to_copy,
                                        });

    /* Row length 0: rows packed one after the other, width * 4 bytes each. */
    VkBufferImageCopy region = {
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {extent.width, extent.height, 1},
    };
    vk->vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               capture->buffer.handle, 1, &region);

    /*
     * The image goes on to the present once the copy has read it (nothing to
     * make visible: a read writes nothing). The buffer's bytes are made
     * visible to the CPU, which reads them after the fence.
     */
    VkImageMemoryBarrier2 to_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
    VkBufferMemoryBarrier2 to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = capture->buffer.handle,
        .size = VK_WHOLE_SIZE,
    };
    vk->vkCmdPipelineBarrier2(commands, &(VkDependencyInfo){
                                            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                            .bufferMemoryBarrierCount = 1,
                                            .pBufferMemoryBarriers = &to_host,
                                            .imageMemoryBarrierCount = 1,
                                            .pImageMemoryBarriers = &to_present,
                                        });
    return true;
}

bool rb_vk_capture_finish(rb_vk_capture *capture, const rb_vk_device *device) {
    RB_ASSERT(capture != NULL && device != NULL);
    if (capture->buffer.handle == VK_NULL_HANDLE) {
        return false;
    }
    usize count = (usize)capture->extent.width * capture->extent.height;
    usize size = count * 4;
    /*
     * The heap, not the renderer's allocator: that one may be an arena (the
     * app's), and captures come and go at any size while the game runs.
     */
    rb_allocator heap = rb_heap_allocator();
    if (size != capture->capacity) {
        RB_FREE_ARRAY(heap, capture->pixels, u8, capture->capacity, RB_MEMORY_TAG_IMAGE);
        capture->capacity = 0;
        capture->pixels = RB_ALLOC_ARRAY(heap, u8, size, RB_MEMORY_TAG_IMAGE);
        if (capture->pixels != NULL) {
            capture->capacity = size;
        }
    }
    bool ok = capture->pixels != NULL;
    if (!ok) {
        RB_LOG_ERROR("Capture: no memory for %ux%u pixels", capture->extent.width,
                     capture->extent.height);
    }
    ok = ok && rb_vk_buffer_read(&capture->buffer, device, capture->pixels, size);
    ok = ok && rb_vk_capture_convert(capture->pixels, count, capture->format);
    rb_vk_buffer_destroy(&capture->buffer, device);
    capture->valid = ok;
    capture->image = ok ? (rb_image){capture->extent.width, capture->extent.height, capture->pixels}
                        : (rb_image){0};
    return ok;
}

void rb_vk_capture_cancel(rb_vk_capture *capture, const rb_vk_device *device) {
    RB_ASSERT(capture != NULL && device != NULL);
    rb_vk_buffer_destroy(&capture->buffer, device);
    capture->valid = false;
    capture->image = (rb_image){0};
}

void rb_vk_capture_destroy(rb_vk_capture *capture, const rb_vk_device *device) {
    RB_ASSERT(capture != NULL && device != NULL);
    rb_vk_buffer_destroy(&capture->buffer, device);
    RB_FREE_ARRAY(rb_heap_allocator(), capture->pixels, u8, capture->capacity, RB_MEMORY_TAG_IMAGE);
    memset(capture, 0, sizeof(*capture));
}
