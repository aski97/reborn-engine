/*
 * Frames in flight (ADR 0013, S2, T2 and U1).
 *
 * While the GPU draws one frame, the CPU records the next: each frame being
 * recorded or drawn needs its own command buffer and synchronization objects,
 * so they come in RB_VK_FRAMES_IN_FLIGHT copies, used in turn. The fence of a
 * copy says when the GPU is done with it, so the CPU can reuse it.
 */
#ifndef RB_VULKAN_FRAME_H
#define RB_VULKAN_FRAME_H

#include <reborn/defines.h>

#include "renderer/vulkan/vulkan_device.h"

/* Two: the CPU records one frame while the GPU draws the previous one. */
#define RB_VK_FRAMES_IN_FLIGHT 2u

typedef struct rb_vk_frame {
    /* Reset whole at the start of the frame (U1): it frees what the last use recorded. */
    VkCommandPool   command_pool;
    VkCommandBuffer commands; /* allocated from command_pool, freed with it */
    /* Acquire signals it when the image is ready; the submit waits on it. */
    VkSemaphore image_acquired;
    /*
     * Signaled when the GPU has executed the frame's submit. Created signaled:
     * the first wait on a copy that was never submitted returns at once.
     */
    VkFence done;
} rb_vk_frame;

/* False (logged): *frame holds what was created, for rb_vk_frame_destroy. */
bool rb_vk_frame_create(rb_vk_frame *frame, const rb_vk_device *device);

/*
 * The GPU must be done with the frame (device idle, or its fence waited).
 * Zeroes *frame; VK_NULL_HANDLE members are skipped.
 */
void rb_vk_frame_destroy(rb_vk_frame *frame, const rb_vk_device *device);

#endif /* RB_VULKAN_FRAME_H */
