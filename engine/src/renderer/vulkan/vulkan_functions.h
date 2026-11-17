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
#define RB_VK_INSTANCE_FUNCTIONS(X) X(vkDestroyInstance)

/* VK_EXT_debug_utils: loaded only when the extension is enabled (validation). */
#define RB_VK_DEBUG_UTILS_FUNCTIONS(X)                                                             \
    X(vkCreateDebugUtilsMessengerEXT)                                                              \
    X(vkDestroyDebugUtilsMessengerEXT)

typedef struct rb_vk_instance_functions {
    RB_VK_INSTANCE_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
    /* NULL without validation. */
    RB_VK_DEBUG_UTILS_FUNCTIONS(RB_VK_DECLARE_FUNCTION)
} rb_vk_instance_functions;

#endif /* RB_VULKAN_FUNCTIONS_H */
