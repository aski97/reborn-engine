/*
 * Vulkan instance (ADR 0013, D3 and E2): the connection between the engine and
 * the Vulkan installation, with the layers and extensions it enables.
 */
#ifndef RB_VULKAN_INSTANCE_H
#define RB_VULKAN_INSTANCE_H

#include <reborn/defines.h>
#include <reborn/memory.h>

#include "renderer/vulkan/vulkan_debug.h"
#include "renderer/vulkan/vulkan_functions.h"
#include "renderer/vulkan/vulkan_loader.h"

#define RB_VK_VALIDATION_LAYER "VK_LAYER_KHRONOS_validation"

typedef struct rb_vk_instance {
    VkInstance               handle;
    rb_vk_instance_functions vk;
    /* VK_NULL_HANDLE without validation. */
    VkDebugUtilsMessengerEXT messenger;
} rb_vk_instance;

/*
 * Creates a Vulkan 1.3 instance. With `validation`, enables the validation
 * layer and a messenger reporting to `debug` when both the layer and
 * VK_EXT_debug_utils are installed, otherwise warns and goes on without;
 * debug->stats.enabled tells which. `allocator` serves the temporary lists of
 * layers and extensions. `debug` must outlive the instance: it also receives
 * the messages of its destruction. False (logged): *instance is zeroed.
 */
bool rb_vk_instance_create(rb_vk_instance *instance, const rb_vk_loader *loader,
                           rb_allocator allocator, bool validation, rb_vk_debug *debug);

/* Messenger, then instance; zeroes *instance. */
void rb_vk_instance_destroy(rb_vk_instance *instance);

#endif /* RB_VULKAN_INSTANCE_H */
