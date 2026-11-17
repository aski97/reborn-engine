/*
 * Vulkan function tables (ADR 0013, B2 and C2).
 *
 * Each list is an X-macro: it takes the name of another macro, X, and applies
 * it to every function, so a function is named once and X decides what the
 * name becomes (a table field, a loading statement). Tables keep the full
 * Vulkan names: `loader->global.vkCreateInstance(...)`.
 *
 * A function missing from its list does not exist for the engine: using it is
 * a compile error (VK_NO_PROTOTYPES, vulkan_api.h).
 */
#ifndef RB_VULKAN_FUNCTIONS_H
#define RB_VULKAN_FUNCTIONS_H

#include "renderer/vulkan/vulkan_api.h"

/* `PFN_vkCreateInstance vkCreateInstance;` */
#define RB_VK_DECLARE_FUNCTION(name) PFN_##name name;

/* ============================================================================
 * Global level: callable before an instance exists, loaded with
 * vkGetInstanceProcAddr(NULL, name)
 * ========================================================================== */

/* Required: a loader without one of these is unusable. */
#define RB_VK_GLOBAL_FUNCTIONS(X)                                                                  \
    X(vkCreateInstance)                                                                            \
    X(vkEnumerateInstanceExtensionProperties)                                                      \
    X(vkEnumerateInstanceLayerProperties)

typedef struct rb_vk_global_functions {
    RB_VK_GLOBAL_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
    /* Vulkan 1.1. NULL from a 1.0 loader, which means instance version 1.0. */
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
} rb_vk_global_functions;

/* ============================================================================
 * Instance level: loaded with vkGetInstanceProcAddr(instance, name) once the
 * instance exists
 * ========================================================================== */

/* Core 1.0–1.3: required. */
#define RB_VK_INSTANCE_FUNCTIONS(X)                                                                \
    X(vkDestroyInstance)                                                                           \
    X(vkEnumeratePhysicalDevices)                                                                  \
    X(vkGetPhysicalDeviceProperties)                                                               \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                    \
    X(vkEnumerateDeviceExtensionProperties)                                                        \
    X(vkCreateDevice)                                                                              \
    X(vkGetDeviceProcAddr)

/* VK_KHR_surface: loaded only when the instance presents to a window. */
#define RB_VK_SURFACE_FUNCTIONS(X)                                                                 \
    X(vkDestroySurfaceKHR)                                                                         \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                        \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                   \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                        \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

/* VK_EXT_debug_utils: loaded only when the extension is enabled (validation). */
#define RB_VK_DEBUG_UTILS_FUNCTIONS(X)                                                             \
    X(vkCreateDebugUtilsMessengerEXT)                                                              \
    X(vkDestroyDebugUtilsMessengerEXT)

typedef struct rb_vk_instance_functions {
    RB_VK_INSTANCE_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
    /* NULL without a surface extension. */
    RB_VK_SURFACE_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
    /* NULL without validation. */
    RB_VK_DEBUG_UTILS_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
} rb_vk_instance_functions;

/* ============================================================================
 * Device level: loaded with vkGetDeviceProcAddr(device, name). They belong to
 * one device and call its driver directly, without the loader's dispatch.
 * ========================================================================== */

/* Core 1.0–1.3: required. */
#define RB_VK_DEVICE_FUNCTIONS(X)                                                                  \
    X(vkDestroyDevice)                                                                             \
    X(vkDeviceWaitIdle)                                                                            \
    X(vkGetDeviceQueue)                                                                            \
    X(vkCreateImageView)                                                                           \
    X(vkDestroyImageView)                                                                          \
    X(vkCreateCommandPool)                                                                         \
    X(vkDestroyCommandPool)                                                                        \
    X(vkResetCommandPool)                                                                          \
    X(vkAllocateCommandBuffers)                                                                    \
    X(vkBeginCommandBuffer)                                                                        \
    X(vkEndCommandBuffer)                                                                          \
    X(vkCreateSemaphore)                                                                           \
    X(vkDestroySemaphore)                                                                          \
    X(vkCreateFence)                                                                               \
    X(vkDestroyFence)                                                                              \
    X(vkWaitForFences)                                                                             \
    X(vkResetFences)                                                                               \
    X(vkQueueSubmit2)                                                                              \
    X(vkCmdPipelineBarrier2)                                                                       \
    X(vkCmdBeginRendering)                                                                         \
    X(vkCmdEndRendering)

/* VK_KHR_swapchain: always enabled, the renderer always presents to a window (ADR 0013, K2). */
#define RB_VK_SWAPCHAIN_FUNCTIONS(X)                                                               \
    X(vkCreateSwapchainKHR)                                                                        \
    X(vkDestroySwapchainKHR)                                                                       \
    X(vkGetSwapchainImagesKHR)                                                                     \
    X(vkAcquireNextImageKHR)                                                                       \
    X(vkQueuePresentKHR)

typedef struct rb_vk_device_functions {
    RB_VK_DEVICE_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
    RB_VK_SWAPCHAIN_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
} rb_vk_device_functions;

#endif /* RB_VULKAN_FUNCTIONS_H */
