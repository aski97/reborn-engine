/*
 * Renderer, Vulkan backend (ADR 0013): implements reborn/renderer.h.
 */
#include "renderer/vulkan/vulkan_renderer.h"

#include <reborn/assert.h>
#include <reborn/log.h>

rb_renderer *rb_renderer_startup(rb_allocator allocator, const rb_renderer_config *config) {
    RB_ASSERT(allocator.vtable != NULL);
    RB_ASSERT(config != NULL);
    /* Zeroed (ADR 0010, H1): no stats, no seen errors. Failure is logged by the allocator. */
    rb_renderer *renderer = RB_ALLOC_TYPE(allocator, rb_renderer, RB_MEMORY_TAG_RENDERER);
    if (renderer == NULL) {
        return NULL;
    }
    renderer->allocator = allocator;
    if (rb_vk_loader_open(&renderer->loader)) {
        if (rb_vk_instance_create(&renderer->instance, &renderer->loader, allocator,
                                  config->validation, &renderer->debug)) {
            return renderer;
        }
        rb_vk_loader_close(&renderer->loader);
    }
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
    rb_vk_instance_destroy(&renderer->instance);
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
