/*
 * Renderer, Vulkan backend (ADR 0013): implements reborn/renderer.h and
 * renderer/renderer_internal.h.
 */
#include "renderer/vulkan/vulkan_renderer.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include "platform/platform_vulkan.h"
#include "renderer/camera2d_internal.h"
#include "renderer/renderer_internal.h"

/*
 * Every Vulkan object that exists, in reverse order of creation: each one
 * needs the one created before it. What was never created is zeroed and
 * skipped, so a startup that failed halfway uses this too.
 */
static void renderer_destroy_objects(rb_renderer *renderer) {
    if (renderer->device.handle != VK_NULL_HANDLE) {
        /* Frames still on the GPU use the frame objects and the swapchain's images. */
        renderer->device.vk.vkDeviceWaitIdle(renderer->device.handle);
        for (u32 i = 0; i < RB_VK_FRAMES_IN_FLIGHT; ++i) {
            rb_vk_frame_destroy(&renderer->frames[i], &renderer->device);
        }
        rb_vk_pipeline_destroy(&renderer->pipeline, &renderer->device);
        rb_vk_swapchain_destroy(&renderer->swapchain, &renderer->device);
        rb_vk_textures_destroy(&renderer->textures, &renderer->device);
    }
    rb_vk_device_destroy(&renderer->device);
    if (renderer->surface != VK_NULL_HANDLE) {
        renderer->instance.vk.vkDestroySurfaceKHR(renderer->instance.handle, renderer->surface,
                                                  NULL);
        renderer->surface = VK_NULL_HANDLE;
    }
    rb_vk_instance_destroy(&renderer->instance);
}

/*
 * The pipeline bakes in the format of the images it draws into: built with the
 * first swapchain, rebuilt only if a later one comes with another format. True
 * while there is no swapchain (nothing to draw into, nothing to build for).
 */
static bool renderer_fit_pipeline(rb_renderer *renderer) {
    VkFormat format = renderer->swapchain.format.format;
    if (renderer->swapchain.handle == VK_NULL_HANDLE || format == renderer->pipeline.color_format) {
        return true;
    }
    if (renderer->pipeline.handle != VK_NULL_HANDLE) {
        /* Frames still on the GPU may use it. Rare: a driver changing formats. */
        renderer->device.vk.vkDeviceWaitIdle(renderer->device.handle);
        rb_vk_pipeline_destroy(&renderer->pipeline, &renderer->device);
    }
    return rb_vk_pipeline_create(&renderer->pipeline, &renderer->device, format,
                                 renderer->textures.set_layout);
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
    /* Dark gray, not black: a window showing the clear differs from one that shows nothing. */
    renderer->clear_color = rb_color_srgb(0.1f, 0.1f, 0.1f, 1.0f);
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
    ok = ok && rb_vk_textures_create(&renderer->textures, &renderer->device);
    ok = ok && rb_vk_swapchain_create(&renderer->swapchain, &renderer->instance, &renderer->device,
                                      renderer->surface, allocator, size.pixel_width,
                                      size.pixel_height, renderer->vsync);
    ok = ok && renderer_fit_pipeline(renderer);
    for (u32 i = 0; ok && i < RB_VK_FRAMES_IN_FLIGHT; ++i) {
        ok = rb_vk_frame_create(&renderer->frames[i], &renderer->device);
    }
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

void rb_renderer_set_clear_color(rb_renderer *renderer, rb_color color) {
    RB_ASSERT(renderer != NULL);
    renderer->clear_color = color;
}

/*
 * Points the draws that follow at a space (ADR 0016): viewport and scissor on
 * the area it covers, the view that maps it there. NULL: the whole image, in
 * pixels.
 */
static void renderer_set_space(rb_renderer *renderer, const rb_camera2d *camera) {
    const rb_vk_device_functions *vk = &renderer->device.vk;
    VkCommandBuffer               commands = renderer->frames[renderer->frame_index].commands;
    VkExtent2D                    extent = renderer->swapchain.extent;
    rb_camera2d_field             field = {0, 0, extent.width, extent.height};
    rb_mat4 view = rb_mat4_ortho(0.0f, (f32)extent.width, 0.0f, (f32)extent.height);
    if (camera != NULL) {
        field = rb_camera2d_field_rect(camera, extent.width, extent.height);
        view = rb_camera2d_projection(camera);
    }
    /*
     * A viewport must not be empty, a scissor may: a field squeezed to no
     * pixels by an extreme aspect keeps a 1-pixel viewport and draws nothing.
     */
    VkViewport viewport = {
        .x = (f32)field.x,
        .y = (f32)field.y,
        .width = (f32)(field.width > 0 ? field.width : 1),
        .height = (f32)(field.height > 0 ? field.height : 1),
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = {(i32)field.x, (i32)field.y},
        .extent = {field.width, field.height},
    };
    vk->vkCmdSetViewport(commands, 0, 1, &viewport);
    vk->vkCmdSetScissor(commands, 0, 1, &scissor);
    /* Only the view: the quad's part is pushed with each draw. */
    vk->vkCmdPushConstants(commands, renderer->pipeline.layout, RB_VK_QUAD_PUSH_STAGES,
                           offsetof(rb_vk_quad_push, view), sizeof(view.m), view.m);
}

void rb_renderer_set_camera(rb_renderer *renderer, const rb_camera2d *camera) {
    RB_ASSERT(renderer != NULL);
    /* Outside a frame there is no command buffer to record into: a bug in the caller. */
    RB_ASSERT(renderer->in_frame);
    renderer_set_space(renderer, camera);
}

/*
 * A quad with `texture`'s pixels multiplied by `color`: its set bound if the
 * last draw used another, its push constants, six vertices. `source` is the
 * region of the texture it shows, in the texture's pixels (ADR 0018).
 */
static void renderer_draw(rb_renderer *renderer, f32 x, f32 y, f32 width, f32 height,
                          const rb_vk_texture *texture, rb_rect source, rb_color color) {
    /* Outside a frame there is no command buffer to record into: a bug in the caller. */
    RB_ASSERT(renderer->in_frame);
    const rb_vk_device_functions *vk = &renderer->device.vk;
    VkCommandBuffer               commands = renderer->frames[renderer->frame_index].commands;
    if (texture->set != renderer->bound_set) {
        /* Set 0 of the pipeline layout (ADR 0017, D1); it stays bound across draws. */
        vk->vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    renderer->pipeline.layout, 0, 1, &texture->set, 0, NULL);
        renderer->bound_set = texture->set;
    }
    /* Shaders sample in fractions of the texture: one division here instead of one per caller. */
    f32             texture_width = (f32)texture->width;
    f32             texture_height = (f32)texture->height;
    rb_vk_quad_push quad = {
        .position = {x, y},
        .size = {width, height},
        .uv_offset = {source.x / texture_width, source.y / texture_height},
        .uv_size = {source.width / texture_width, source.height / texture_height},
        .color = {color.r, color.g, color.b, color.a},
    };
    /* From `position` on: the view pushed by the last change of space stays (ADR 0016). */
    vk->vkCmdPushConstants(commands, renderer->pipeline.layout, RB_VK_QUAD_PUSH_STAGES,
                           RB_VK_QUAD_PUSH_OFFSET, RB_VK_QUAD_PUSH_SIZE, quad.position);
    /* Six vertices, two triangles: the vertex shader builds them from the quad (F1). */
    vk->vkCmdDraw(commands, 6, 1, 0, 0);
}

/* The region covering the whole texture. */
static rb_rect renderer_whole(const rb_vk_texture *texture) {
    return (rb_rect){0.0f, 0.0f, (f32)texture->width, (f32)texture->height};
}

void rb_renderer_draw_quad(rb_renderer *renderer, f32 x, f32 y, f32 width, f32 height,
                           rb_color color) {
    RB_ASSERT(renderer != NULL);
    /* White: the shader's texture times color leaves the color as it is (ADR 0017). */
    const rb_vk_texture *white = &renderer->textures.white;
    renderer_draw(renderer, x, y, width, height, white, renderer_whole(white), color);
}

void rb_renderer_draw_texture(rb_renderer *renderer, f32 x, f32 y, f32 width, f32 height,
                              rb_texture texture, rb_color tint) {
    RB_ASSERT(renderer != NULL);
    /* A zero or stale handle gets the white texture: a plain quad, never a freed set. */
    const rb_vk_texture *found = rb_vk_textures_get(&renderer->textures, texture.id);
    renderer_draw(renderer, x, y, width, height, found, renderer_whole(found), tint);
}

void rb_renderer_draw_texture_region(rb_renderer *renderer, f32 x, f32 y, f32 width, f32 height,
                                     rb_texture texture, rb_rect source, rb_color tint) {
    RB_ASSERT(renderer != NULL);
    /*
     * A zero or stale handle gets the white texture: the source lands far
     * outside its single pixel, but clamping to the edge reads that pixel
     * anyway, so it is still a plain quad.
     */
    renderer_draw(renderer, x, y, width, height,
                  rb_vk_textures_get(&renderer->textures, texture.id), source, tint);
}

/* ============================================================================
 * Textures (ADR 0017)
 * ========================================================================== */

rb_texture rb_renderer_create_texture(rb_renderer *renderer, const rb_texture_desc *desc) {
    RB_ASSERT(renderer != NULL);
    RB_ASSERT(desc != NULL && desc->pixels != NULL);
    /*
     * Allowed inside a frame: the upload is submitted and waited for on its
     * own, before the frame being recorded is submitted.
     */
    return (rb_texture){rb_vk_textures_add(&renderer->textures, &renderer->device, desc)};
}

void rb_renderer_destroy_texture(rb_renderer *renderer, rb_texture texture) {
    RB_ASSERT(renderer != NULL);
    /* The frame being recorded may already use it, and no wait covers unsubmitted work. */
    RB_ASSERT(!renderer->in_frame);
    if (!rb_handle_pool_valid(&renderer->textures.handles, texture.id)) {
        return;
    }
    /* Frames still on the GPU may use it (G1: a stall, fine for rare destructions). */
    renderer->device.vk.vkDeviceWaitIdle(renderer->device.handle);
    (void)rb_vk_textures_remove(&renderer->textures, &renderer->device, texture.id);
}

bool rb_renderer_texture_is_valid(const rb_renderer *renderer, rb_texture texture) {
    RB_ASSERT(renderer != NULL);
    return rb_handle_pool_valid(&renderer->textures.handles, texture.id);
}

/*
 * An error no rebuild can fix: logged once, then every begin returns false.
 * Out of date and suboptimal never get here.
 */
static void renderer_fail(rb_renderer *renderer, const char *call, VkResult result) {
    RB_LOG_ERROR("%s failed: %s; the renderer stops drawing", call, rb_vk_result_name(result));
    renderer->failed = true;
}

/*
 * The swapchain fits the window, or the window cannot have one. False when
 * there is nothing to draw into: minimized, no pixels, a failed rebuild.
 */
static bool renderer_fit_swapchain(rb_renderer *renderer) {
    /*
     * Windows reports a minimized window's surface as 0x0: rebuilding now would
     * drop the swapchain, only to build it again on restore at the same size.
     */
    if (rb_window_is_minimized(renderer->platform, renderer->window)) {
        return false;
    }
    /* As of the last pump: a resize whose event was dropped from the queue is still seen. */
    rb_window_size size = rb_window_get_size(renderer->platform, renderer->window);
    bool           resized =
        size.pixel_width != renderer->built_width || size.pixel_height != renderer->built_height;
    if (resized || renderer->swapchain_stale) {
        renderer->built_width = size.pixel_width;
        renderer->built_height = size.pixel_height;
        renderer->swapchain_stale = false;
        /* A failure is logged and leaves no swapchain: checked below. */
        (void)rb_vk_swapchain_create(&renderer->swapchain, &renderer->instance, &renderer->device,
                                     renderer->surface, renderer->allocator, size.pixel_width,
                                     size.pixel_height, renderer->vsync);
        if (!renderer_fit_pipeline(renderer)) {
            RB_LOG_ERROR("No pipeline for the new swapchain; the renderer stops drawing");
            renderer->failed = true;
            return false;
        }
    }
    return renderer->swapchain.handle != VK_NULL_HANDLE;
}

/*
 * A layout transition of the acquired image (synchronization2). The layout is
 * how the GPU arranges the pixels in memory: drawing and showing want
 * different ones. The barrier also orders the writes before it and the
 * accesses after it.
 */
static void renderer_transition_image(const rb_renderer *renderer, VkCommandBuffer commands,
                                      VkImageLayout old_layout, VkImageLayout new_layout,
                                      VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                      VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        /* One queue family (N1): no ownership transfer. */
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = renderer->swapchain.images[renderer->image_index],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
    };
    VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    renderer->device.vk.vkCmdPipelineBarrier2(commands, &dependency);
}

/* The image is acquired: from here on the frame must reach the present (ADR 0013, V1). */
static bool renderer_record_begin(rb_renderer *renderer, const rb_vk_frame *frame) {
    const rb_vk_device_functions *vk = &renderer->device.vk;
    VkCommandBufferBeginInfo      begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        /* Recorded every frame, submitted once: lets the driver skip reuse bookkeeping. */
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VkResult result = vk->vkBeginCommandBuffer(frame->commands, &begin);
    if (result != VK_SUCCESS) {
        renderer_fail(renderer, "vkBeginCommandBuffer", result);
        return false;
    }
    /*
     * UNDEFINED: the old content is not needed, the clear overwrites it. The
     * source stage is the one the submit waits for the acquire semaphore at:
     * the transition happens only once the presentation engine let go of the
     * image.
     */
    renderer_transition_image(renderer, frame->commands, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    rb_color                  c = renderer->clear_color;
    VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = renderer->swapchain.views[renderer->image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        /* Linear values: the sRGB image encodes them on write (ADR 0013, P1). */
        .clearValue = {.color = {.float32 = {c.r, c.g, c.b, c.a}}},
    };
    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = renderer->swapchain.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vk->vkCmdBeginRendering(frame->commands, &rendering);

    /* Every draw of the frame uses the one pipeline: bound once. */
    vk->vkCmdBindPipeline(frame->commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          renderer->pipeline.handle);
    /* A new command buffer has nothing bound: the first draw binds its texture. */
    renderer->bound_set = VK_NULL_HANDLE;
    /* Window pixels until the game sets a camera: a forgotten reset lasts one frame at most. */
    renderer_set_space(renderer, NULL);
    return true;
}

bool rb_renderer_begin_frame(rb_renderer *renderer) {
    RB_ASSERT(renderer != NULL);
    RB_ASSERT(!renderer->in_frame);
    if (renderer->failed || !renderer_fit_swapchain(renderer)) {
        return false;
    }
    const rb_vk_device_functions *vk = &renderer->device.vk;
    VkDevice                      device = renderer->device.handle;
    rb_vk_frame                  *frame = &renderer->frames[renderer->frame_index];

    /* The GPU must be done with this copy's last submit before it is reused. */
    VkResult result = vk->vkWaitForFences(device, 1, &frame->done, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        renderer_fail(renderer, "vkWaitForFences", result);
        return false;
    }

    /* Blocks while every image is taken: with FIFO, this is where vsync paces the loop. */
    result =
        vk->vkAcquireNextImageKHR(device, renderer->swapchain.handle, UINT64_MAX,
                                  frame->image_acquired, VK_NULL_HANDLE, &renderer->image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        /* No image and the semaphore stays unsignaled: skip, the next begin rebuilds. */
        renderer->swapchain_stale = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        /* Usable, but no longer a perfect fit: show this one, rebuild at the next begin. */
        renderer->swapchain_stale = true;
    } else if (result != VK_SUCCESS) {
        renderer_fail(renderer, "vkAcquireNextImageKHR", result);
        return false;
    }

    /*
     * Reset only now that a submit will follow: resetting before an acquire
     * that fails would leave a fence nobody signals, and the next wait on it
     * would never return.
     */
    result = vk->vkResetFences(device, 1, &frame->done);
    if (result == VK_SUCCESS) {
        result = vk->vkResetCommandPool(device, frame->command_pool, 0);
    }
    if (result != VK_SUCCESS) {
        renderer_fail(renderer, "Resetting the frame", result);
        return false;
    }
    if (!renderer_record_begin(renderer, frame)) {
        return false;
    }
    renderer->in_frame = true;
    return true;
}

void rb_renderer_end_frame(rb_renderer *renderer) {
    RB_ASSERT(renderer != NULL);
    RB_ASSERT(renderer->in_frame);
    renderer->in_frame = false;
    const rb_vk_device_functions *vk = &renderer->device.vk;
    rb_vk_frame                  *frame = &renderer->frames[renderer->frame_index];
    VkSemaphore                   rendered = renderer->swapchain.rendered[renderer->image_index];
    renderer->frame_index = (renderer->frame_index + 1) % RB_VK_FRAMES_IN_FLIGHT;

    vk->vkCmdEndRendering(frame->commands);
    /* Destination NONE: the present waits on the semaphore, which orders it after this. */
    renderer_transition_image(
        renderer, frame->commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    VkResult result = vk->vkEndCommandBuffer(frame->commands);
    if (result != VK_SUCCESS) {
        renderer_fail(renderer, "vkEndCommandBuffer", result);
        return;
    }

    VkSemaphoreSubmitInfo wait = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame->image_acquired,
        /* Only color output waits for the image: earlier stages may start at once. */
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphoreSubmitInfo signal = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = rendered,
        /* Everything, the final layout transition included, before the present. */
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkCommandBufferSubmitInfo commands = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame->commands,
    };
    VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commands,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    result = vk->vkQueueSubmit2(renderer->device.queue, 1, &submit, frame->done);
    if (result != VK_SUCCESS) {
        renderer_fail(renderer, "vkQueueSubmit2", result);
        return;
    }

    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &rendered,
        .swapchainCount = 1,
        .pSwapchains = &renderer->swapchain.handle,
        .pImageIndices = &renderer->image_index,
    };
    result = vk->vkQueuePresentKHR(renderer->device.queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        renderer->swapchain_stale = true;
    } else if (result != VK_SUCCESS) {
        renderer_fail(renderer, "vkQueuePresentKHR", result);
    }
}
