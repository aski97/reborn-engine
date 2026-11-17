/*
 * The only way engine code includes Vulkan (ADR 0013, A1 and B2).
 *
 * VK_NO_PROTOTYPES makes the header declare types and PFN_vk* function
 * pointer types only, not the functions: the engine never links the loader,
 * so a direct call such as vkCreateInstance(...) must fail to compile instead
 * of failing to link. Every call goes through a table (vulkan_functions.h).
 */
#ifndef RB_VULKAN_API_H
#define RB_VULKAN_API_H

#ifndef VK_NO_PROTOTYPES
    #define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

/* "VK_ERROR_OUT_OF_HOST_MEMORY", for logs; "VK_UNKNOWN_RESULT" for codes not listed. */
const char *rb_vk_result_name(VkResult result);

#endif /* RB_VULKAN_API_H */
