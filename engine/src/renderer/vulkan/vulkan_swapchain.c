/*
 * Vulkan swapchain (ADR 0013, O2, P1 and Q1).
 */
#include "renderer/vulkan/vulkan_swapchain.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Names, for logs
 * ========================================================================== */

const char *rb_vk_format_name(VkFormat format) {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return "A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return "A2R10G10B10_UNORM_PACK32";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    default:
        return NULL;
    }
}

const char *rb_vk_present_mode_name(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "FIFO_RELAXED";
    default:
        return NULL;
    }
}

/* ============================================================================
 * Choices
 * ========================================================================== */

VkSurfaceFormatKHR rb_vk_swapchain_choose_format(const VkSurfaceFormatKHR *formats, u32 count) {
    RB_ASSERT(formats != NULL && count > 0);
    /* BGRA first: it is what Windows drivers list, and the usual scan-out order. */
    static const VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    for (usize p = 0; p < RB_ARRAY_COUNT(preferred); ++p) {
        for (u32 i = 0; i < count; ++i) {
            if (formats[i].format == preferred[p] &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return formats[i];
            }
        }
    }
    return formats[0];
}

VkPresentModeKHR rb_vk_swapchain_choose_present_mode(const VkPresentModeKHR *modes, u32 count,
                                                     bool vsync) {
    RB_ASSERT(modes != NULL || count == 0);
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    /* MAILBOX: no wait and no tearing. IMMEDIATE: no wait, tearing. */
    static const VkPresentModeKHR preferred[] = {VK_PRESENT_MODE_MAILBOX_KHR,
                                                 VK_PRESENT_MODE_IMMEDIATE_KHR};
    for (usize p = 0; p < RB_ARRAY_COUNT(preferred); ++p) {
        for (u32 i = 0; i < count; ++i) {
            if (modes[i] == preferred[p]) {
                return modes[i];
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static u32 swapchain_clamp(u32 value, u32 min, u32 max) {
    return value < min ? min : value > max ? max : value;
}

VkExtent2D rb_vk_swapchain_choose_extent(const VkSurfaceCapabilitiesKHR *capabilities, u32 width,
                                         u32 height) {
    RB_ASSERT(capabilities != NULL);
    if (capabilities->currentExtent.width != UINT32_MAX) {
        return capabilities->currentExtent;
    }
    return (VkExtent2D){
        swapchain_clamp(width, capabilities->minImageExtent.width,
                        capabilities->maxImageExtent.width),
        swapchain_clamp(height, capabilities->minImageExtent.height,
                        capabilities->maxImageExtent.height),
    };
}

u32 rb_vk_swapchain_choose_image_count(const VkSurfaceCapabilitiesKHR *capabilities) {
    RB_ASSERT(capabilities != NULL);
    u32 count = capabilities->minImageCount + 1;
    if (capabilities->maxImageCount != 0 && count > capabilities->maxImageCount) {
        count = capabilities->maxImageCount;
    }
    return count < RB_VK_SWAPCHAIN_MAX_IMAGES ? count : RB_VK_SWAPCHAIN_MAX_IMAGES;
}

/* Opaque if the compositor allows it: the window never shows what is behind it. */
static VkCompositeAlphaFlagBitsKHR swapchain_composite_alpha(VkCompositeAlphaFlagsKHR supported) {
    static const VkCompositeAlphaFlagBitsKHR preferred[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    for (usize i = 0; i < RB_ARRAY_COUNT(preferred); ++i) {
        if ((supported & preferred[i]) != 0) {
            return preferred[i];
        }
    }
    /* The spec requires at least one bit: unreachable with a conforming driver. */
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

/* ============================================================================
 * What the surface offers
 *
 * Two-call pattern again; each query keeps the list only long enough to
 * choose. False when the list cannot be read or is empty (logged).
 * ========================================================================== */

static bool swapchain_pick_format(const rb_vk_instance *instance, const rb_vk_device *device,
                                  VkSurfaceKHR surface, rb_allocator allocator,
                                  VkSurfaceFormatKHR *format) {
    u32      count = 0;
    VkResult result =
        instance->vk.vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical, surface, &count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkGetPhysicalDeviceSurfaceFormatsKHR failed: %s", rb_vk_result_name(result));
        return false;
    }
    if (count == 0) {
        RB_LOG_ERROR("The window's surface offers no image format");
        return false;
    }
    VkSurfaceFormatKHR *formats =
        RB_ALLOC_ARRAY(allocator, VkSurfaceFormatKHR, count, RB_MEMORY_TAG_RENDERER);
    if (formats == NULL) {
        return false;
    }
    u32 listed = count;
    result = instance->vk.vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical, surface, &listed,
                                                               formats);
    bool ok = (result == VK_SUCCESS || result == VK_INCOMPLETE) && listed > 0;
    if (!ok) {
        RB_LOG_ERROR("vkGetPhysicalDeviceSurfaceFormatsKHR failed: %s", rb_vk_result_name(result));
    } else {
        *format = rb_vk_swapchain_choose_format(formats, listed);
        if (format->colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
            (format->format != VK_FORMAT_B8G8R8A8_SRGB &&
             format->format != VK_FORMAT_R8G8B8A8_SRGB)) {
            RB_LOG_WARN("The window offers no 8-bit sRGB format: using VkFormat %d, colors may "
                        "look wrong",
                        (int)format->format);
        }
    }
    RB_FREE_ARRAY(allocator, formats, VkSurfaceFormatKHR, count, RB_MEMORY_TAG_RENDERER);
    return ok;
}

static bool swapchain_pick_present_mode(const rb_vk_instance *instance, const rb_vk_device *device,
                                        VkSurfaceKHR surface, rb_allocator allocator, bool vsync,
                                        VkPresentModeKHR *mode) {
    /* FIFO needs no list: every surface supports it. */
    if (vsync) {
        *mode = VK_PRESENT_MODE_FIFO_KHR;
        return true;
    }
    u32      count = 0;
    VkResult result = instance->vk.vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical,
                                                                             surface, &count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkGetPhysicalDeviceSurfacePresentModesKHR failed: %s",
                     rb_vk_result_name(result));
        return false;
    }
    if (count == 0) {
        *mode = VK_PRESENT_MODE_FIFO_KHR;
        return true;
    }
    VkPresentModeKHR *modes =
        RB_ALLOC_ARRAY(allocator, VkPresentModeKHR, count, RB_MEMORY_TAG_RENDERER);
    if (modes == NULL) {
        return false;
    }
    u32 listed = count;
    result = instance->vk.vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical, surface,
                                                                    &listed, modes);
    bool ok = result == VK_SUCCESS || result == VK_INCOMPLETE;
    if (!ok) {
        RB_LOG_ERROR("vkGetPhysicalDeviceSurfacePresentModesKHR failed: %s",
                     rb_vk_result_name(result));
    } else {
        *mode = rb_vk_swapchain_choose_present_mode(modes, listed, false);
    }
    RB_FREE_ARRAY(allocator, modes, VkPresentModeKHR, count, RB_MEMORY_TAG_RENDERER);
    return ok;
}

/* ============================================================================
 * Creation and destruction
 * ========================================================================== */

/* One 2D color view per image: what rendering will target. */
static bool swapchain_create_views(rb_vk_swapchain *swapchain, const rb_vk_device *device) {
    for (u32 i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain->format.format,
            /* Zeroed components are VK_COMPONENT_SWIZZLE_IDENTITY: channels as they are. */
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        };
        VkResult result =
            device->vk.vkCreateImageView(device->handle, &info, NULL, &swapchain->views[i]);
        if (result != VK_SUCCESS) {
            RB_LOG_ERROR("vkCreateImageView failed: %s", rb_vk_result_name(result));
            swapchain->views[i] = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

/* The handle exists: fetch its images (fixed array) and make their views. */
static bool swapchain_finish(rb_vk_swapchain *swapchain, const rb_vk_device *device) {
    u32      count = 0;
    VkResult result =
        device->vk.vkGetSwapchainImagesKHR(device->handle, swapchain->handle, &count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkGetSwapchainImagesKHR failed: %s", rb_vk_result_name(result));
        return false;
    }
    /* Drivers may create more images than asked: the array has room for every sane answer. */
    if (count > RB_VK_SWAPCHAIN_MAX_IMAGES) {
        RB_LOG_ERROR("The swapchain has %u images, the engine handles up to %u", count,
                     RB_VK_SWAPCHAIN_MAX_IMAGES);
        return false;
    }
    result = device->vk.vkGetSwapchainImagesKHR(device->handle, swapchain->handle, &count,
                                                swapchain->images);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkGetSwapchainImagesKHR failed: %s", rb_vk_result_name(result));
        return false;
    }
    swapchain->image_count = count;
    return swapchain_create_views(swapchain, device);
}

bool rb_vk_swapchain_create(rb_vk_swapchain *swapchain, const rb_vk_instance *instance,
                            const rb_vk_device *device, VkSurfaceKHR surface,
                            rb_allocator allocator, u32 width, u32 height, bool vsync) {
    RB_ASSERT(swapchain != NULL && instance != NULL && device != NULL);
    RB_ASSERT(device->handle != VK_NULL_HANDLE && surface != VK_NULL_HANDLE);
    /* Taken out first: whatever happens below, *swapchain ends up new or zeroed. */
    rb_vk_swapchain old = *swapchain;
    bool            replacing = old.handle != VK_NULL_HANDLE;
    memset(swapchain, 0, sizeof(*swapchain));

    VkSurfaceCapabilitiesKHR capabilities;
    VkResult                 result = instance->vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device->physical, surface, &capabilities);
    bool ok = result == VK_SUCCESS;
    if (!ok) {
        RB_LOG_ERROR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %s",
                     rb_vk_result_name(result));
    }
    VkExtent2D extent =
        ok ? rb_vk_swapchain_choose_extent(&capabilities, width, height) : (VkExtent2D){0, 0};
    /* Vulkan forbids zero-sized images: nothing to show until the window has pixels again. */
    bool empty = ok && (extent.width == 0 || extent.height == 0);
    ok = ok && !empty &&
         swapchain_pick_format(instance, device, surface, allocator, &swapchain->format) &&
         swapchain_pick_present_mode(instance, device, surface, allocator, vsync,
                                     &swapchain->present_mode);

    if (ok) {
        swapchain->extent = extent;
        VkSwapchainCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = rb_vk_swapchain_choose_image_count(&capabilities),
            .imageFormat = swapchain->format.format,
            .imageColorSpace = swapchain->format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            /* One queue family draws and presents (N1): no sharing between families. */
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            /* The rotation the display already applies (phones): none on a desktop. */
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = swapchain_composite_alpha(capabilities.supportedCompositeAlpha),
            .presentMode = swapchain->present_mode,
            /* Pixels covered by other windows need not be drawn. */
            .clipped = VK_TRUE,
            /* Lets the driver hand resources over and keep showing the old images meanwhile. */
            .oldSwapchain = old.handle,
        };
        result = device->vk.vkCreateSwapchainKHR(device->handle, &info, NULL, &swapchain->handle);
        if (result != VK_SUCCESS) {
            RB_LOG_ERROR("vkCreateSwapchainKHR failed: %s", rb_vk_result_name(result));
            swapchain->handle = VK_NULL_HANDLE;
            ok = false;
        }
    }

    /*
     * Passed as oldSwapchain, the old one is retired even if the creation
     * failed: it can only be destroyed now, once the GPU is done with it.
     */
    rb_vk_swapchain_destroy(&old, device);

    ok = ok && swapchain_finish(swapchain, device);
    if (!ok) {
        rb_vk_swapchain_destroy(swapchain, device);
        if (empty) {
            RB_LOG_DEBUG("The window has no pixels: no swapchain until it has");
        }
        return empty;
    }

    const char *format_name = rb_vk_format_name(swapchain->format.format);
    const char *mode_name = rb_vk_present_mode_name(swapchain->present_mode);
    char        format_buffer[24];
    if (format_name == NULL) {
        snprintf(format_buffer, sizeof(format_buffer), "VkFormat %d",
                 (int)swapchain->format.format);
        format_name = format_buffer;
    }
    /* The first at INFO; a drag recreates it many times, those go to DEBUG. */
    if (!replacing) {
        RB_LOG_INFO("Swapchain created: %ux%u, %s, %s, %u images", extent.width, extent.height,
                    format_name, mode_name != NULL ? mode_name : "other mode",
                    swapchain->image_count);
    } else {
        RB_LOG_DEBUG("Swapchain recreated: %ux%u", extent.width, extent.height);
    }
    return true;
}

void rb_vk_swapchain_destroy(rb_vk_swapchain *swapchain, const rb_vk_device *device) {
    RB_ASSERT(swapchain != NULL && device != NULL);
    if (swapchain->handle != VK_NULL_HANDLE) {
        /* Images may still be in use by work in flight: wait before freeing anything. */
        device->vk.vkDeviceWaitIdle(device->handle);
        for (u32 i = 0; i < swapchain->image_count; ++i) {
            if (swapchain->views[i] != VK_NULL_HANDLE) {
                device->vk.vkDestroyImageView(device->handle, swapchain->views[i], NULL);
            }
        }
        /* The images belong to the swapchain: they go with it. */
        device->vk.vkDestroySwapchainKHR(device->handle, swapchain->handle, NULL);
    }
    memset(swapchain, 0, sizeof(*swapchain));
}
