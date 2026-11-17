/*
 * Vulkan instance (ADR 0013, D3, E2 and J1).
 */
#include "renderer/vulkan/vulkan_instance.h"

#include <reborn/assert.h>
#include <reborn/log.h>
#include <reborn/version.h>

#include <string.h>

/* ============================================================================
 * What the installation offers
 *
 * Both queries follow Vulkan's two-call pattern: ask for the count, allocate,
 * ask again for the items. False only when the list cannot be read (no
 * memory, logged); `*found` says whether `name` is in it.
 * ========================================================================== */

static bool instance_has_layer(const rb_vk_loader *loader, rb_allocator allocator, const char *name,
                               bool *found) {
    *found = false;
    u32      count = 0;
    VkResult result = loader->global.vkEnumerateInstanceLayerProperties(&count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkEnumerateInstanceLayerProperties failed: %s", rb_vk_result_name(result));
        return false;
    }
    if (count == 0) {
        return true;
    }
    VkLayerProperties *layers =
        RB_ALLOC_ARRAY(allocator, VkLayerProperties, count, RB_MEMORY_TAG_RENDERER);
    if (layers == NULL) {
        return false;
    }
    /* Fewer if a layer was uninstalled in between; VK_INCOMPLETE if one was installed. */
    u32 listed = count;
    result = loader->global.vkEnumerateInstanceLayerProperties(&listed, layers);
    bool ok = result == VK_SUCCESS || result == VK_INCOMPLETE;
    if (!ok) {
        RB_LOG_ERROR("vkEnumerateInstanceLayerProperties failed: %s", rb_vk_result_name(result));
    }
    for (u32 i = 0; ok && i < listed; ++i) {
        if (strcmp(layers[i].layerName, name) == 0) {
            *found = true;
            break;
        }
    }
    RB_FREE_ARRAY(allocator, layers, VkLayerProperties, count, RB_MEMORY_TAG_RENDERER);
    return ok;
}

/* Extensions of the loader and of the implicit layers, not those of optional layers. */
static bool instance_has_extension(const rb_vk_loader *loader, rb_allocator allocator,
                                   const char *name, bool *found) {
    *found = false;
    u32      count = 0;
    VkResult result = loader->global.vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkEnumerateInstanceExtensionProperties failed: %s",
                     rb_vk_result_name(result));
        return false;
    }
    if (count == 0) {
        return true;
    }
    VkExtensionProperties *extensions =
        RB_ALLOC_ARRAY(allocator, VkExtensionProperties, count, RB_MEMORY_TAG_RENDERER);
    if (extensions == NULL) {
        return false;
    }
    u32 listed = count;
    result = loader->global.vkEnumerateInstanceExtensionProperties(NULL, &listed, extensions);
    bool ok = result == VK_SUCCESS || result == VK_INCOMPLETE;
    if (!ok) {
        RB_LOG_ERROR("vkEnumerateInstanceExtensionProperties failed: %s",
                     rb_vk_result_name(result));
    }
    for (u32 i = 0; ok && i < listed; ++i) {
        if (strcmp(extensions[i].extensionName, name) == 0) {
            *found = true;
            break;
        }
    }
    RB_FREE_ARRAY(allocator, extensions, VkExtensionProperties, count, RB_MEMORY_TAG_RENDERER);
    return ok;
}

/*
 * Whether validation can run: the layer checks, VK_EXT_debug_utils delivers
 * its messages to us. Without the extension the layer would print to stdout,
 * outside the log and the counters. False only when a list cannot be read.
 */
static bool instance_validation_available(const rb_vk_loader *loader, rb_allocator allocator,
                                          bool *available) {
    *available = false;
    bool has_layer;
    bool has_extension;
    if (!instance_has_layer(loader, allocator, RB_VK_VALIDATION_LAYER, &has_layer) ||
        !instance_has_extension(loader, allocator, VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                &has_extension)) {
        return false;
    }
    if (!has_layer) {
        RB_LOG_WARN("Vulkan validation requested but " RB_VK_VALIDATION_LAYER
                    " is not installed (Vulkan SDK, vulkan-validationlayers): running without");
    } else if (!has_extension) {
        RB_LOG_WARN("Vulkan validation requested but " VK_EXT_DEBUG_UTILS_EXTENSION_NAME
                    " is missing: running without");
    }
    *available = has_layer && has_extension;
    return true;
}

/*
 * Whether the instance can present: VK_KHR_surface for the common surface
 * functions, `extension` for the OS. Unlike validation there is nothing to fall
 * back to, so a missing one is an error. False when an extension is missing or
 * the list cannot be read (logged).
 */
static bool instance_surface_available(const rb_vk_loader *loader, rb_allocator allocator,
                                       const char *extension) {
    const char *names[] = {VK_KHR_SURFACE_EXTENSION_NAME, extension};
    for (usize i = 0; i < RB_ARRAY_COUNT(names); ++i) {
        bool found;
        if (!instance_has_extension(loader, allocator, names[i], &found)) {
            return false;
        }
        if (!found) {
            RB_LOG_ERROR("Vulkan cannot present to a window: %s is missing (update the GPU "
                         "driver)",
                         names[i]);
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * Creation and destruction
 * ========================================================================== */

/* After vkCreateInstance: the instance table, then the messenger. False (logged) on failure. */
static bool instance_finish(rb_vk_instance *instance, const rb_vk_loader *loader, bool surface,
                            bool validation, rb_vk_debug *debug) {
    bool complete = true;
#define RB_VK_LOAD_INSTANCE(fn)                                                                    \
    instance->vk.fn = (PFN_##fn)loader->vkGetInstanceProcAddr(instance->handle, #fn);              \
    if (instance->vk.fn == NULL) {                                                                 \
        RB_LOG_ERROR("Vulkan instance lacks " #fn);                                                \
        complete = false;                                                                          \
    }
    RB_VK_INSTANCE_FUNCTIONS(RB_VK_LOAD_INSTANCE)
    if (surface) {
        RB_VK_SURFACE_FUNCTIONS(RB_VK_LOAD_INSTANCE)
    }
    if (validation) {
        RB_VK_DEBUG_UTILS_FUNCTIONS(RB_VK_LOAD_INSTANCE)
    }
#undef RB_VK_LOAD_INSTANCE
    if (!complete) {
        return false;
    }
    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT info = rb_vk_debug_messenger_info(debug);
        VkResult result = instance->vk.vkCreateDebugUtilsMessengerEXT(instance->handle, &info, NULL,
                                                                      &instance->messenger);
        if (result != VK_SUCCESS) {
            RB_LOG_ERROR("vkCreateDebugUtilsMessengerEXT failed: %s", rb_vk_result_name(result));
            instance->messenger = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

bool rb_vk_instance_create(rb_vk_instance *instance, const rb_vk_loader *loader,
                           rb_allocator allocator, const char *surface_extension, bool validation,
                           rb_vk_debug *debug) {
    RB_ASSERT(instance != NULL && loader != NULL && debug != NULL);
    memset(instance, 0, sizeof(*instance));
    debug->stats.enabled = false;

    u32 version = rb_vk_loader_instance_version(loader);
    if (version < VK_API_VERSION_1_3) {
        RB_LOG_ERROR("Vulkan 1.3 is required, the installed loader offers %u.%u: update the GPU "
                     "driver",
                     VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return false;
    }

    if (surface_extension != NULL &&
        !instance_surface_available(loader, allocator, surface_extension)) {
        return false;
    }

    /* Requested, and installed. */
    bool use_validation = false;
    if (validation && !instance_validation_available(loader, allocator, &use_validation)) {
        return false;
    }

    /* Drivers may tune themselves for known engines; the game's name will come with its config. */
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName = "Reborn",
        .engineVersion =
            VK_MAKE_API_VERSION(0, RB_VERSION_MAJOR, RB_VERSION_MINOR, RB_VERSION_PATCH),
        /* The highest version the engine uses: device functions above it are unavailable. */
        .apiVersion = VK_API_VERSION_1_3,
    };
    const char *layers[] = {RB_VK_VALIDATION_LAYER};
    const char *extensions[3];
    u32         extension_count = 0;
    if (surface_extension != NULL) {
        extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[extension_count++] = surface_extension;
    }
    if (use_validation) {
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
    /*
     * The messenger needs an instance to exist; chained here, the same callback
     * also checks vkCreateInstance and vkDestroyInstance themselves.
     */
    VkDebugUtilsMessengerCreateInfoEXT messenger_info = rb_vk_debug_messenger_info(debug);
    VkInstanceCreateInfo               info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = use_validation ? &messenger_info : NULL,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = use_validation ? (u32)RB_ARRAY_COUNT(layers) : 0,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };
    VkResult result = loader->global.vkCreateInstance(&info, NULL, &instance->handle);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateInstance failed: %s%s", rb_vk_result_name(result),
                     result == VK_ERROR_INCOMPATIBLE_DRIVER
                         ? " (no Vulkan 1.3 driver: install or update the GPU driver)"
                         : "");
        memset(instance, 0, sizeof(*instance));
        return false;
    }
    if (!instance_finish(instance, loader, surface_extension != NULL, use_validation, debug)) {
        rb_vk_instance_destroy(instance);
        return false;
    }
    debug->stats.enabled = use_validation;
    RB_LOG_INFO("Vulkan instance created: API 1.3, %s, validation %s",
                surface_extension != NULL ? surface_extension : "no surface",
                use_validation ? "on" : "off");
    return true;
}

void rb_vk_instance_destroy(rb_vk_instance *instance) {
    RB_ASSERT(instance != NULL);
    if (instance->messenger != VK_NULL_HANDLE) {
        instance->vk.vkDestroyDebugUtilsMessengerEXT(instance->handle, instance->messenger, NULL);
    }
    if (instance->handle != VK_NULL_HANDLE) {
        /* Missing only from a broken loader, already logged: the instance cannot be freed then. */
        if (instance->vk.vkDestroyInstance != NULL) {
            instance->vk.vkDestroyInstance(instance->handle, NULL);
        }
    }
    memset(instance, 0, sizeof(*instance));
}
