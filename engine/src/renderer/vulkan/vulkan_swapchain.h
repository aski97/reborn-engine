/*
 * Vulkan swapchain (ADR 0013, O2, P1 and Q1): the images the window shows.
 *
 * The presentation engine owns a small ring of images: the renderer acquires
 * one, draws into it and hands it back to be shown. Size, format and present
 * mode are fixed at creation, so a resized window needs a new swapchain.
 */
#ifndef RB_VULKAN_SWAPCHAIN_H
#define RB_VULKAN_SWAPCHAIN_H

#include <reborn/defines.h>
#include <reborn/memory.h>

#include "renderer/vulkan/vulkan_device.h"
#include "renderer/vulkan/vulkan_instance.h"

/* Room for every image: drivers give 2 to 4, the request is the minimum + 1. */
#define RB_VK_SWAPCHAIN_MAX_IMAGES 8u

typedef struct rb_vk_swapchain {
    VkSwapchainKHR     handle; /* VK_NULL_HANDLE: none (zero-sized window, failed creation) */
    VkSurfaceFormatKHR format;
    VkPresentModeKHR   present_mode;
    VkExtent2D         extent; /* pixels */
    u32                image_count;
    VkImage            images[RB_VK_SWAPCHAIN_MAX_IMAGES]; /* owned by the swapchain */
    VkImageView        views[RB_VK_SWAPCHAIN_MAX_IMAGES];  /* ours: one per image */
} rb_vk_swapchain;

/*
 * Builds the swapchain for `surface`, whose window is `width` x `height`
 * pixels; when *swapchain already holds one, it is replaced (passed as
 * oldSwapchain, then destroyed after the device is idle). A surface with no
 * pixels (minimized on Windows) gets no swapchain: true, with a NULL handle.
 * `allocator` serves the temporary lists of formats and modes. False (logged):
 * *swapchain is zeroed, the old one destroyed too.
 */
bool rb_vk_swapchain_create(rb_vk_swapchain *swapchain, const rb_vk_instance *instance,
                            const rb_vk_device *device, VkSurfaceKHR surface,
                            rb_allocator allocator, u32 width, u32 height, bool vsync);

/* Views, then the swapchain; zeroes *swapchain. A zeroed swapchain is ignored. */
void rb_vk_swapchain_destroy(rb_vk_swapchain *swapchain, const rb_vk_device *device);

/* ============================================================================
 * Choices, split out so tests can feed them any driver's answers
 * ========================================================================== */

/*
 * B8G8R8A8_SRGB, then R8G8B8A8_SRGB, with the sRGB nonlinear color space (P1);
 * otherwise the first offered. `count` > 0.
 */
VkSurfaceFormatKHR rb_vk_swapchain_choose_format(const VkSurfaceFormatKHR *formats, u32 count);

/* FIFO with vsync; otherwise MAILBOX, then IMMEDIATE, then FIFO, always available (O2). */
VkPresentModeKHR rb_vk_swapchain_choose_present_mode(const VkPresentModeKHR *modes, u32 count,
                                                     bool vsync);

/*
 * The surface's current extent when it states one, otherwise the window's
 * pixel size clamped to what the surface allows (Wayland lets the swapchain
 * decide, with 0xFFFFFFFF).
 */
VkExtent2D rb_vk_swapchain_choose_extent(const VkSurfaceCapabilitiesKHR *capabilities, u32 width,
                                         u32 height);

/*
 * The minimum + 1, so the renderer never waits for the driver to release an
 * image, within the surface's maximum (0: none) and RB_VK_SWAPCHAIN_MAX_IMAGES.
 */
u32 rb_vk_swapchain_choose_image_count(const VkSurfaceCapabilitiesKHR *capabilities);

/* "B8G8R8A8_SRGB", "FIFO"...; NULL for values the engine does not name. For logs. */
const char *rb_vk_format_name(VkFormat format);
const char *rb_vk_present_mode_name(VkPresentModeKHR mode);

#endif /* RB_VULKAN_SWAPCHAIN_H */
