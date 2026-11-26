/*
 * Frame capture (ADR 0024): the image of a frame copied back to the CPU.
 *
 * The swapchain image is copied at the end of the frame, before the present
 * hands it to the presentation engine (after that it must not be read), into
 * a host-visible buffer made for the capture. Once the frame's fence says the
 * GPU is done, the pixels are read into the capture's own RGBA memory and the
 * buffer is destroyed.
 *
 *   rb_vk_capture_record(...)   // in the frame's commands, after the drawing
 *   ... submit, present, wait for the frame's fence ...
 *   rb_vk_capture_finish(...)   // capture->image now holds the pixels
 */
#ifndef RB_VULKAN_CAPTURE_H
#define RB_VULKAN_CAPTURE_H

#include <reborn/defines.h>
#include <reborn/image.h>

#include "renderer/vulkan/vulkan_device.h"
#include "renderer/vulkan/vulkan_memory.h"

typedef struct rb_vk_capture {
    bool requested; /* booked for the frame being recorded */
    /* The copy's destination, between record and finish; zeroed otherwise. */
    rb_vk_buffer buffer;
    VkFormat     format; /* of the image being copied */
    VkExtent2D   extent;
    /*
     * The last finished capture: RGBA from the heap (tag RB_MEMORY_TAG_IMAGE),
     * reused while the size stays the same. `valid` false: none, or the last
     * one failed.
     */
    u8      *pixels;
    usize    capacity;
    rb_image image;
    bool     valid;
} rb_vk_capture;

/* Whether captures can turn images of `format` into RGBA. */
bool rb_vk_capture_supports(VkFormat format);

/*
 * Records into `commands` the copy of `image` (`extent` pixels, `format`), in
 * COLOR_ATTACHMENT_OPTIMAL after the frame's drawing, leaving it in
 * PRESENT_SRC_KHR for the present. False (logged), nothing recorded: the
 * format is not supported or the buffer could not be made; the caller then
 * records the frame's usual transition.
 */
bool rb_vk_capture_record(rb_vk_capture *capture, const rb_vk_device *device,
                          VkCommandBuffer commands, VkImage image, VkFormat format,
                          VkExtent2D extent);

/*
 * After the GPU has executed the recorded copy (the frame's fence waited):
 * reads the pixels into capture->image and destroys the buffer. False (logged)
 * on failure, capture->valid false. Without a recorded copy: false, quietly.
 */
bool rb_vk_capture_finish(rb_vk_capture *capture, const rb_vk_device *device);

/* Drops a recorded copy that will never run (a failed submit): the GPU must not use it. */
void rb_vk_capture_cancel(rb_vk_capture *capture, const rb_vk_device *device);

/* Frees everything; zeroes *capture. The GPU must be done with any recorded copy. */
void rb_vk_capture_destroy(rb_vk_capture *capture, const rb_vk_device *device);

/*
 * Pixels as copied from an image of `format` (4 bytes each), turned in place
 * into RGBA with alpha 255: B and R swapped for BGRA formats. False, untouched,
 * for a format captures do not support. Split out for tests.
 */
bool rb_vk_capture_convert(u8 *pixels, usize count, VkFormat format);

#endif /* RB_VULKAN_CAPTURE_H */
