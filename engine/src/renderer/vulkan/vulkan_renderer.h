/*
 * The renderer's state, Vulkan backend (ADR 0013). Internal: reborn/renderer.h
 * keeps it opaque; tests reach the Vulkan objects through it.
 */
#ifndef RB_VULKAN_RENDERER_H
#define RB_VULKAN_RENDERER_H

#include <reborn/memory.h>
#include <reborn/renderer.h>

#include "renderer/vulkan/vulkan_debug.h"
#include "renderer/vulkan/vulkan_instance.h"
#include "renderer/vulkan/vulkan_loader.h"

struct rb_renderer {
    rb_allocator   allocator; /* the startup allocator, to free the state */
    rb_vk_loader   loader;
    rb_vk_instance instance;
    /* The messenger's user data: its address must not change while the instance lives. */
    rb_vk_debug debug;
};

#endif /* RB_VULKAN_RENDERER_H */
