/*
 * Vulkan device (ADR 0013, L2, M1 and N1).
 */
#include "renderer/vulkan/vulkan_device.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

/* ============================================================================
 * Ranking
 * ========================================================================== */

u32 rb_vk_device_type_rank(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    /* Last resort (M1): it runs, slowly, where nothing else does. */
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 1;
    default:
        return 0;
    }
}

const char *rb_vk_device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "other";
    }
}

/* ============================================================================
 * What a GPU offers
 *
 * Same two-call pattern as the instance queries. False only when a list
 * cannot be read (no memory, a Vulkan error: logged).
 * ========================================================================== */

static bool device_has_extension(const rb_vk_instance *instance, rb_allocator allocator,
                                 VkPhysicalDevice physical, const char *name, bool *found) {
    *found = false;
    u32      count = 0;
    VkResult result =
        instance->vk.vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkEnumerateDeviceExtensionProperties failed: %s", rb_vk_result_name(result));
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
    result = instance->vk.vkEnumerateDeviceExtensionProperties(physical, NULL, &listed, extensions);
    bool ok = result == VK_SUCCESS || result == VK_INCOMPLETE;
    if (!ok) {
        RB_LOG_ERROR("vkEnumerateDeviceExtensionProperties failed: %s", rb_vk_result_name(result));
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
 * The first family that does graphics and presents to `surface` (N1), or
 * UINT32_MAX. Graphics families can also run compute and transfer work.
 */
static bool device_find_queue_family(const rb_vk_instance *instance, rb_allocator allocator,
                                     VkPhysicalDevice physical, VkSurfaceKHR surface, u32 *family) {
    *family = UINT32_MAX;
    u32 count = 0;
    instance->vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    if (count == 0) {
        return true;
    }
    VkQueueFamilyProperties *families =
        RB_ALLOC_ARRAY(allocator, VkQueueFamilyProperties, count, RB_MEMORY_TAG_RENDERER);
    if (families == NULL) {
        return false;
    }
    u32 listed = count;
    instance->vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &listed, families);
    bool ok = true;
    for (u32 i = 0; i < listed; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            continue;
        }
        VkBool32 present = VK_FALSE;
        VkResult result =
            instance->vk.vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
        if (result != VK_SUCCESS) {
            RB_LOG_ERROR("vkGetPhysicalDeviceSurfaceSupportKHR failed: %s",
                         rb_vk_result_name(result));
            ok = false;
            break;
        }
        if (present) {
            *family = i;
            break;
        }
    }
    RB_FREE_ARRAY(allocator, families, VkQueueFamilyProperties, count, RB_MEMORY_TAG_RENDERER);
    return ok;
}

/*
 * Sets *reason to why `physical` cannot be used, or NULL when it can, with
 * *family its queue family. False only when a query fails.
 */
static bool device_evaluate(const rb_vk_instance *instance, rb_allocator allocator,
                            VkPhysicalDevice physical, const VkPhysicalDeviceProperties *properties,
                            VkSurfaceKHR surface, u32 *family, const char **reason) {
    *family = UINT32_MAX;
    /* Dynamic rendering and synchronization2 are mandatory in 1.3: the version says enough. */
    if (properties->apiVersion < VK_API_VERSION_1_3) {
        *reason = "below Vulkan 1.3";
        return true;
    }
    bool has_swapchain;
    if (!device_has_extension(instance, allocator, physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                              &has_swapchain)) {
        return false;
    }
    if (!has_swapchain) {
        *reason = "no " VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        return true;
    }
    if (!device_find_queue_family(instance, allocator, physical, surface, family)) {
        return false;
    }
    if (*family == UINT32_MAX) {
        *reason = "no queue family that draws and presents to the window";
        return true;
    }
    *reason = NULL;
    return true;
}

/* ============================================================================
 * Choice
 * ========================================================================== */

/*
 * The heaps and the memory types on top of them (ADR 0017): a discrete GPU
 * shows its own memory and the part of system memory it can reach; integrated
 * GPUs and lavapipe often one heap that is both.
 */
static void device_log_memory(const VkPhysicalDeviceMemoryProperties *memory) {
    for (u32 h = 0; h < memory->memoryHeapCount; ++h) {
        const VkMemoryHeap *heap = &memory->memoryHeaps[h];
        RB_LOG_DEBUG("Memory heap %u: %llu MiB%s", h, (unsigned long long)(heap->size >> 20),
                     (heap->flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 ? ", device-local" : "");
    }
    for (u32 t = 0; t < memory->memoryTypeCount; ++t) {
        VkMemoryPropertyFlags flags = memory->memoryTypes[t].propertyFlags;
        RB_LOG_DEBUG("Memory type %u: heap %u%s%s%s%s", t, memory->memoryTypes[t].heapIndex,
                     (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 ? ", device-local" : "",
                     (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 ? ", host-visible" : "",
                     (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 ? ", host-coherent" : "",
                     (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0 ? ", host-cached" : "");
    }
}

/*
 * Fills device->physical, ->properties, ->memory and ->queue_family with the best
 * suitable GPU (L2). Every GPU is logged with its verdict: when none fits,
 * the lines above the error say why.
 */
static bool device_pick(rb_vk_device *device, const rb_vk_instance *instance,
                        rb_allocator allocator, VkSurfaceKHR surface) {
    u32      count = 0;
    VkResult result = instance->vk.vkEnumeratePhysicalDevices(instance->handle, &count, NULL);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkEnumeratePhysicalDevices failed: %s", rb_vk_result_name(result));
        return false;
    }
    if (count == 0) {
        RB_LOG_ERROR("No GPU supports Vulkan: install or update the GPU driver");
        return false;
    }
    VkPhysicalDevice *physicals =
        RB_ALLOC_ARRAY(allocator, VkPhysicalDevice, count, RB_MEMORY_TAG_RENDERER);
    if (physicals == NULL) {
        return false;
    }
    /* Fewer if a GPU went away in between; VK_INCOMPLETE if one appeared. */
    u32 listed = count;
    result = instance->vk.vkEnumeratePhysicalDevices(instance->handle, &listed, physicals);
    bool ok = result == VK_SUCCESS || result == VK_INCOMPLETE;
    if (!ok) {
        RB_LOG_ERROR("vkEnumeratePhysicalDevices failed: %s", rb_vk_result_name(result));
    }

    u32 best_rank = 0;
    for (u32 i = 0; ok && i < listed; ++i) {
        VkPhysicalDeviceProperties properties;
        instance->vk.vkGetPhysicalDeviceProperties(physicals[i], &properties);
        u32         family;
        const char *reason;
        ok = device_evaluate(instance, allocator, physicals[i], &properties, surface, &family,
                             &reason);
        if (!ok) {
            break;
        }
        RB_LOG_INFO("GPU %u: %s (%s, Vulkan %u.%u.%u): %s", i, properties.deviceName,
                    rb_vk_device_type_name(properties.deviceType),
                    VK_API_VERSION_MAJOR(properties.apiVersion),
                    VK_API_VERSION_MINOR(properties.apiVersion),
                    VK_API_VERSION_PATCH(properties.apiVersion),
                    reason != NULL ? reason : "suitable");
        /* +1: an unknown type (rank 0) beats nothing. Strict >: ties keep the first. */
        u32 rank = rb_vk_device_type_rank(properties.deviceType) + 1;
        if (reason == NULL && rank > best_rank) {
            best_rank = rank;
            device->physical = physicals[i];
            device->properties = properties;
            device->queue_family = family;
        }
    }
    RB_FREE_ARRAY(allocator, physicals, VkPhysicalDevice, count, RB_MEMORY_TAG_RENDERER);
    if (!ok) {
        return false;
    }
    if (best_rank == 0) {
        RB_LOG_ERROR("No GPU can run the engine (Vulkan 1.3 and presentation to the window): "
                     "update the GPU driver");
        return false;
    }
    if (device->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        RB_LOG_WARN("Using %s, which runs Vulkan on the CPU: no GPU fits, expect a low frame rate",
                    device->properties.deviceName);
    }
    instance->vk.vkGetPhysicalDeviceMemoryProperties(device->physical, &device->memory);
    device_log_memory(&device->memory);
    return true;
}

/* ============================================================================
 * Creation and destruction
 * ========================================================================== */

/* vkCreateDevice on the picked GPU, then the device table and the queue. */
static bool device_open(rb_vk_device *device, const rb_vk_instance *instance) {
    f32                     priority = 1.0f; /* only matters between queues of one device */
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = device->queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    /*
     * Supported by every 1.3 device, but off until enabled: the next steps
     * draw without render passes and synchronize with the 1.3 barriers.
     */
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const char        *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = (u32)RB_ARRAY_COUNT(extensions),
        .ppEnabledExtensionNames = extensions,
    };
    VkResult result = instance->vk.vkCreateDevice(device->physical, &info, NULL, &device->handle);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateDevice failed: %s", rb_vk_result_name(result));
        device->handle = VK_NULL_HANDLE;
        return false;
    }

    bool complete = true;
#define RB_VK_LOAD_DEVICE(fn)                                                                      \
    device->vk.fn = (PFN_##fn)instance->vk.vkGetDeviceProcAddr(device->handle, #fn);               \
    if (device->vk.fn == NULL) {                                                                   \
        RB_LOG_ERROR("Vulkan device lacks " #fn);                                                  \
        complete = false;                                                                          \
    }
    RB_VK_DEVICE_FUNCTIONS(RB_VK_LOAD_DEVICE)
    RB_VK_SWAPCHAIN_FUNCTIONS(RB_VK_LOAD_DEVICE)
#undef RB_VK_LOAD_DEVICE
    if (!complete) {
        return false;
    }
    device->vk.vkGetDeviceQueue(device->handle, device->queue_family, 0, &device->queue);
    return true;
}

bool rb_vk_device_create(rb_vk_device *device, const rb_vk_instance *instance,
                         rb_allocator allocator, VkSurfaceKHR surface) {
    RB_ASSERT(device != NULL && instance != NULL);
    RB_ASSERT(instance->vk.vkGetPhysicalDeviceSurfaceSupportKHR != NULL);
    memset(device, 0, sizeof(*device));
    if (!device_pick(device, instance, allocator, surface) || !device_open(device, instance)) {
        rb_vk_device_destroy(device);
        return false;
    }
    RB_LOG_INFO("Vulkan device created on %s, queue family %u", device->properties.deviceName,
                device->queue_family);
    return true;
}

void rb_vk_device_destroy(rb_vk_device *device) {
    RB_ASSERT(device != NULL);
    if (device->handle != VK_NULL_HANDLE) {
        /* NULL only from a driver missing them, already logged: nothing can be done then. */
        if (device->vk.vkDeviceWaitIdle != NULL) {
            device->vk.vkDeviceWaitIdle(device->handle);
        }
        if (device->vk.vkDestroyDevice != NULL) {
            device->vk.vkDestroyDevice(device->handle, NULL);
        }
    }
    memset(device, 0, sizeof(*device));
}
