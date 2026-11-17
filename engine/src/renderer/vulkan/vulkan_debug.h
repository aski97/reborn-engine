/*
 * Validation messages (ADR 0013, F3): the debug messenger's callback logs
 * every message, counts the validation ones and stops in an attached debugger
 * on the first error of each kind.
 */
#ifndef RB_VULKAN_DEBUG_H
#define RB_VULKAN_DEBUG_H

#include <reborn/defines.h>
#include <reborn/renderer.h>

#include "renderer/vulkan/vulkan_api.h"

/* Distinct error IDs remembered; past this, new errors are logged and counted without stopping. */
#define RB_VK_DEBUG_SEEN_MAX 64

/* Lives in the renderer state: the callback receives it as pUserData. */
typedef struct rb_vk_debug {
    rb_renderer_validation_stats stats;
    /* messageIdNumber of the errors already stopped on: a hash of the VUID. */
    i32 seen_ids[RB_VK_DEBUG_SEEN_MAX];
    u32 seen_count;
} rb_vk_debug;

/*
 * Counts one message and tells whether it deserves a stop in the debugger:
 * true for a validation error whose ID was never seen. Split from the callback
 * so tests can feed it messages.
 */
bool rb_vk_debug_record(rb_vk_debug *debug, VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                        VkDebugUtilsMessageTypeFlagsEXT types, i32 message_id);

/*
 * Every severity and the general, validation and performance types, with
 * `debug` as the callback's user data. Used for the messenger and, chained to
 * VkInstanceCreateInfo, for instance creation and destruction.
 */
VkDebugUtilsMessengerCreateInfoEXT rb_vk_debug_messenger_info(rb_vk_debug *debug);

#endif /* RB_VULKAN_DEBUG_H */
