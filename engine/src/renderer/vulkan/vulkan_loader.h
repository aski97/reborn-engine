/*
 * Vulkan loader, opened at run time (ADR 0013, B2).
 *
 * The loader (vulkan-1.dll, libvulkan.so.1) comes with the GPU driver: it
 * finds the drivers and layers installed on the machine and dispatches calls
 * to them. Opening it is the first step of the renderer; every other Vulkan
 * function comes from its vkGetInstanceProcAddr.
 */
#ifndef RB_VULKAN_LOADER_H
#define RB_VULKAN_LOADER_H

#include <reborn/defines.h>

#include "platform/platform_os.h"
#include "renderer/vulkan/vulkan_functions.h"

typedef struct rb_vk_loader {
    rb_platform_library      *library;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    rb_vk_global_functions    global;
} rb_vk_loader;

/* The loader's file name on this OS. */
#if defined(RB_PLATFORM_WINDOWS)
    #define RB_VK_LOADER_NAME "vulkan-1.dll"
#else
    /* Versioned: the unversioned libvulkan.so is installed only with the development package. */
    #define RB_VK_LOADER_NAME "libvulkan.so.1"
#endif

/*
 * Opens RB_VK_LOADER_NAME and fills the global table. False (logged) when
 * Vulkan is not installed or the loader lacks a required function; *loader is
 * then zeroed and needs no close.
 */
bool rb_vk_loader_open(rb_vk_loader *loader);

/* rb_vk_loader_open with another file name: lets tests exercise the failures. */
bool rb_vk_loader_open_library(rb_vk_loader *loader, const char *name);

/* Closes the library and zeroes *loader: every function taken from it becomes invalid. */
void rb_vk_loader_close(rb_vk_loader *loader);

/*
 * Highest Vulkan version the loader offers for instances, encoded as
 * VK_MAKE_API_VERSION (read with VK_API_VERSION_MAJOR / _MINOR / _PATCH). The
 * GPU's own version is per device and may be lower.
 */
u32 rb_vk_loader_instance_version(const rb_vk_loader *loader);

#endif /* RB_VULKAN_LOADER_H */
