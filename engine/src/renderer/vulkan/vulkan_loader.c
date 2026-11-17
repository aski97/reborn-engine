/*
 * Vulkan loader, opened at run time (ADR 0013, B2).
 */
#include "renderer/vulkan/vulkan_loader.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

bool rb_vk_loader_open(rb_vk_loader *loader) {
    return rb_vk_loader_open_library(loader, RB_VK_LOADER_NAME);
}

bool rb_vk_loader_open_library(rb_vk_loader *loader, const char *name) {
    RB_ASSERT(loader != NULL && name != NULL);
    memset(loader, 0, sizeof(*loader));

    loader->library = rb_platform_library_open(name);
    if (loader->library == NULL) {
        RB_LOG_ERROR("Vulkan is not available: install a GPU driver that supports it");
        return false;
    }

    /* The one function every loader exports by name: the rest come from it. */
    loader->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)rb_platform_library_symbol(
        loader->library, "vkGetInstanceProcAddr");
    if (loader->vkGetInstanceProcAddr == NULL) {
        RB_LOG_ERROR("\"%s\" does not export vkGetInstanceProcAddr: not a Vulkan loader", name);
        rb_vk_loader_close(loader);
        return false;
    }

    /*
     * Expands to one block per function in RB_VK_GLOBAL_FUNCTIONS. A NULL
     * instance asks for global-level functions. The cast turns the generic
     * PFN_vkVoidFunction into the function's real type. Every missing function
     * is logged before failing, not only the first.
     */
    bool complete = true;
#define RB_VK_LOAD_GLOBAL(fn)                                                                      \
    loader->global.fn = (PFN_##fn)loader->vkGetInstanceProcAddr(NULL, #fn);                        \
    if (loader->global.fn == NULL) {                                                               \
        RB_LOG_ERROR("Vulkan loader lacks " #fn);                                                  \
        complete = false;                                                                          \
    }
    RB_VK_GLOBAL_FUNCTIONS(RB_VK_LOAD_GLOBAL)
#undef RB_VK_LOAD_GLOBAL
    if (!complete) {
        rb_vk_loader_close(loader);
        return false;
    }

    loader->global.vkEnumerateInstanceVersion =
        (PFN_vkEnumerateInstanceVersion)loader->vkGetInstanceProcAddr(NULL,
                                                                      "vkEnumerateInstanceVersion");

    u32 version = rb_vk_loader_instance_version(loader);
    RB_LOG_INFO("Vulkan loader opened (%s): instance version %u.%u.%u", name,
                VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                VK_API_VERSION_PATCH(version));
    return true;
}

void rb_vk_loader_close(rb_vk_loader *loader) {
    RB_ASSERT(loader != NULL);
    if (loader->library != NULL) {
        rb_platform_library_close(loader->library);
    }
    memset(loader, 0, sizeof(*loader));
}

u32 rb_vk_loader_instance_version(const rb_vk_loader *loader) {
    RB_ASSERT(loader != NULL && loader->library != NULL);
    PFN_vkEnumerateInstanceVersion enumerate = loader->global.vkEnumerateInstanceVersion;
    if (enumerate == NULL) {
        return VK_API_VERSION_1_0;
    }
    u32 version;
    /* Can only fail out of host memory, and then nothing newer than 1.0 can be counted on. */
    if (enumerate(&version) != VK_SUCCESS) {
        return VK_API_VERSION_1_0;
    }
    return version;
}
