/*
 * Renderer, Vulkan backend (ADR 0013): implements reborn/renderer.h and
 * renderer/renderer_internal.h.
 */
#include "renderer/vulkan/vulkan_renderer.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include "platform/platform_vulkan.h"
#include "renderer/renderer_internal.h"

/*
 * Every Vulkan object that exists, in reverse order of creation: each one
 * needs the one created before it. What was never created is zeroed and
 * skipped, so a startup that failed halfway uses this too.
 */
static void renderer_destroy_objects(rb_renderer *renderer) {
    if (renderer->device.handle != VK_NULL_HANDLE) {
        rb_vk_swapchain_destroy(&renderer->swapchain, &renderer->device);
    }
    rb_vk_device_destroy(&renderer->device);
    if (renderer->surface != VK_NULL_HANDLE) {
        renderer->instance.vk.vkDestroySurfaceKHR(renderer->instance.handle, renderer->surface,
                                                  NULL);
        renderer->surface = VK_NULL_HANDLE;
    }
    rb_vk_instance_destroy(&renderer->instance);
}

rb_renderer *rb_renderer_startup(rb_allocator allocator, const rb_renderer_config *config,
                                 const rb_platform_state *platform, rb_window window) {
    RB_ASSERT(allocator.vtable != NULL);
    RB_ASSERT(config != NULL);
    RB_ASSERT(platform != NULL);
    /* Zeroed (ADR 0010, H1): no stats, no seen errors, every handle VK_NULL_HANDLE. */
    rb_renderer *renderer = RB_ALLOC_TYPE(allocator, rb_renderer, RB_MEMORY_TAG_RENDERER);
    if (renderer == NULL) {
        return NULL;
    }
    renderer->allocator = allocator;
    renderer->platform = platform;
    renderer->window = window;
    renderer->vsync = config->vsync;
    rb_window_size size = rb_window_get_size(platform, window);
    renderer->built_width = size.pixel_width;
    renderer->built_height = size.pixel_height;

    /* Each step runs only if the previous ones succeeded; each failure is logged where it happens.
     */
    bool ok = rb_vk_loader_open(&renderer->loader);
    ok = ok && rb_vk_instance_create(&renderer->instance, &renderer->loader, allocator,
                                     rb_platform_vulkan_surface_extension(), config->validation,
                                     &renderer->debug);
    ok = ok && rb_platform_vulkan_create_surface(platform, window, renderer->instance.handle,
                                                 renderer->loader.vkGetInstanceProcAddr,
                                                 &renderer->surface);
    ok = ok &&
         rb_vk_device_create(&renderer->device, &renderer->instance, allocator, renderer->surface);
    ok = ok && rb_vk_swapchain_create(&renderer->swapchain, &renderer->instance, &renderer->device,
                                      renderer->surface, allocator, size.pixel_width,
                                      size.pixel_height, renderer->vsync);
    if (ok) {
        return renderer;
    }
    renderer_destroy_objects(renderer);
    rb_vk_loader_close(&renderer->loader);
    RB_FREE_TYPE(allocator, renderer, rb_renderer, RB_MEMORY_TAG_RENDERER);
    return NULL;
}

/* Errors are bugs; warnings deserve a look; a clean run says so, proving validation ran. */
static void renderer_log_validation(const rb_vk_debug *debug) {
    const rb_renderer_validation_stats *stats = &debug->stats;
    if (!stats->enabled) {
        return;
    }
    if (stats->errors > 0) {
        RB_LOG_ERROR("Vulkan validation: %u errors, %u warnings", stats->errors, stats->warnings);
    } else if (stats->warnings > 0) {
        RB_LOG_WARN("Vulkan validation: %u warnings", stats->warnings);
    } else {
        RB_LOG_INFO("Vulkan validation: no errors or warnings");
    }
}

void rb_renderer_shutdown(rb_renderer *renderer) {
    RB_ASSERT(renderer != NULL);
    /* Destruction first: its own messages count too. */
    renderer_destroy_objects(renderer);
    renderer_log_validation(&renderer->debug);
    rb_vk_loader_close(&renderer->loader);
    RB_LOG_DEBUG("Renderer shut down");
    /* Copied out first: the free ends the state's life, allocator field included. */
    rb_allocator allocator = renderer->allocator;
    RB_FREE_TYPE(allocator, renderer, rb_renderer, RB_MEMORY_TAG_RENDERER);
}

rb_renderer_validation_stats rb_renderer_get_validation_stats(const rb_renderer *renderer) {
    RB_ASSERT(renderer != NULL);
    return renderer->debug.stats;
}

/* ============================================================================
 * Frames
 * ========================================================================== */

bool rb_renderer_prepare_frame(rb_renderer *renderer) {
    RB_ASSERT(renderer != NULL);
    /*
     * Windows reports a minimized window's surface as 0x0: rebuilding now would
     * drop the swapchain, only to build it again on restore at the same size.
     */
    if (rb_window_is_minimized(renderer->platform, renderer->window)) {
        return false;
    }
    /* As of the last pump: a resize whose event was dropped from the queue is still seen. */
    rb_window_size size = rb_window_get_size(renderer->platform, renderer->window);
    if (size.pixel_width != renderer->built_width || size.pixel_height != renderer->built_height) {
        renderer->built_width = size.pixel_width;
        renderer->built_height = size.pixel_height;
        /* A failure is logged and leaves no swapchain: checked below. */
        (void)rb_vk_swapchain_create(&renderer->swapchain, &renderer->instance, &renderer->device,
                                     renderer->surface, renderer->allocator, size.pixel_width,
                                     size.pixel_height, renderer->vsync);
    }
    return renderer->swapchain.handle != VK_NULL_HANDLE;
}
