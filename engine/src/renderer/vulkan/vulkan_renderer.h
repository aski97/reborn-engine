/*
 * The renderer's state, Vulkan backend (ADR 0013). Internal: reborn/renderer.h
 * keeps it opaque; tests reach the Vulkan objects through it.
 */
#ifndef RB_VULKAN_RENDERER_H
#define RB_VULKAN_RENDERER_H

#include <reborn/color.h>
#include <reborn/memory.h>
#include <reborn/platform.h>
#include <reborn/renderer.h>

#include "renderer/vulkan/vulkan_capture.h"
#include "renderer/vulkan/vulkan_debug.h"
#include "renderer/vulkan/vulkan_device.h"
#include "renderer/vulkan/vulkan_frame.h"
#include "renderer/vulkan/vulkan_instance.h"
#include "renderer/vulkan/vulkan_loader.h"
#include "renderer/vulkan/vulkan_pipeline.h"
#include "renderer/vulkan/vulkan_swapchain.h"
#include "renderer/vulkan/vulkan_texture.h"

struct rb_renderer {
    rb_allocator allocator; /* the startup allocator: the state, temporary query lists */
    /* Where the swapchain's size comes from (ADR 0013, Q1). */
    const rb_platform_state *platform;
    rb_window                window;
    bool                     vsync;

    rb_vk_loader    loader;
    rb_vk_instance  instance;
    VkSurfaceKHR    surface;
    rb_vk_device    device;
    rb_vk_swapchain swapchain;
    /*
     * The window's pixel size the swapchain was last built for, even when that
     * failed or gave none: it is rebuilt only when the size changes again, so a
     * failure is logged once, not every frame.
     */
    u32 built_width;
    u32 built_height;
    /* Acquire or present said out of date or suboptimal: rebuild at the next begin. */
    bool swapchain_stale;

    /* Textures and what they share (ADR 0017): the pipeline is built with their set layout. */
    rb_vk_textures textures;
    /* Built for the swapchain's format once a swapchain exists (ADR 0015). */
    rb_vk_pipeline pipeline;

    rb_vk_frame frames[RB_VK_FRAMES_IN_FLIGHT];
    u32         frame_index; /* the copy the current or next frame uses */
    u32         image_index; /* the swapchain image acquired by the current frame */
    /* Between a successful begin and its end: an image is acquired and must be presented. */
    bool in_frame;
    /* The texture set the current frame last bound: a draw rebinds only on a change. */
    VkDescriptorSet bound_set;
    /*
     * A Vulkan call failed in a way a rebuild cannot fix (device lost, out of
     * memory): logged once, nothing is drawn any more.
     */
    bool     failed;
    rb_color clear_color; /* linear */
    /* Frame capture (ADR 0024): a booked capture and the last one's pixels. */
    rb_vk_capture capture;

    /* The messenger's user data: its address must not change while the instance lives. */
    rb_vk_debug debug;
};

#endif /* RB_VULKAN_RENDERER_H */
