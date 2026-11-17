/*
 * The renderer's state, Vulkan backend (ADR 0013). Internal: reborn/renderer.h
 * keeps it opaque; tests reach the Vulkan objects through it.
 */
#ifndef RB_VULKAN_RENDERER_H
#define RB_VULKAN_RENDERER_H

#include <reborn/memory.h>
#include <reborn/platform.h>
#include <reborn/renderer.h>

#include "renderer/vulkan/vulkan_debug.h"
#include "renderer/vulkan/vulkan_device.h"
#include "renderer/vulkan/vulkan_instance.h"
#include "renderer/vulkan/vulkan_loader.h"
#include "renderer/vulkan/vulkan_swapchain.h"

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

    /* The messenger's user data: its address must not change while the instance lives. */
    rb_vk_debug debug;
};

#endif /* RB_VULKAN_RENDERER_H */
