/*
 * Validation messages (ADR 0013, F3).
 */
#include "renderer/vulkan/vulkan_debug.h"

#include <reborn/log.h>

#include "core/debug_break.h"
#include "platform/platform_os.h"

bool rb_vk_debug_record(rb_vk_debug *debug, VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                        VkDebugUtilsMessageTypeFlagsEXT types, i32 message_id) {
    /* General messages are the loader's and driver's notes, not mistakes of ours. */
    if ((types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) == 0) {
        return false;
    }
    if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        ++debug->stats.warnings;
        return false;
    }
    if (severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        return false;
    }
    ++debug->stats.errors;
    for (u32 i = 0; i < debug->seen_count; ++i) {
        if (debug->seen_ids[i] == message_id) {
            return false;
        }
    }
    /* Table full: stopping on every later error could mean a stop per frame. */
    if (debug->seen_count == RB_VK_DEBUG_SEEN_MAX) {
        return false;
    }
    debug->seen_ids[debug->seen_count++] = message_id;
    return true;
}

static rb_log_level debug_log_level(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                    VkDebugUtilsMessageTypeFlagsEXT        types) {
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return RB_LOG_LEVEL_ERROR;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return RB_LOG_LEVEL_WARN;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        /*
         * The loader's info narrates its search for layers and drivers, about a
         * hundred lines per instance: TRACE, or it would bury a DEBUG log.
         */
        return (types & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) ? RB_LOG_LEVEL_TRACE
                                                                     : RB_LOG_LEVEL_DEBUG;
    default:
        return RB_LOG_LEVEL_TRACE;
    }
}

static const char *debug_type_name(VkDebugUtilsMessageTypeFlagsEXT types) {
    if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        return "validation";
    }
    if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        return "performance";
    }
    return "general";
}

/*
 * Called by the layers inside the Vulkan call that triggered the message,
 * before it returns: a stop here leaves that call, and our line that made it,
 * on the debugger's call stack. VKAPI_ATTR / VKAPI_CALL give it the calling
 * convention Vulkan expects of function pointers.
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user_data) {
    rb_vk_debug *debug = user_data;
    const char  *message = data->pMessage != NULL ? data->pMessage : "(no message)";
    /* The location is this callback's, the same for every message: the text says what failed. */
    rb_log_write(debug_log_level(severity, types), __FILE__, __LINE__, __func__, "Vulkan %s: %s",
                 debug_type_name(types), message);
    if (rb_vk_debug_record(debug, severity, types, data->messageIdNumber) &&
        rb_platform_debugger_attached()) {
        /* Look one frame up for the Vulkan call, and up from there for ours. Continue to resume. */
        RB_DEBUG_BREAK();
    }
    /* VK_FALSE: the call goes on. VK_TRUE would abort it, which only layer tests should ask. */
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT rb_vk_debug_messenger_info(rb_vk_debug *debug) {
    return (VkDebugUtilsMessengerCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        /* Everything: the logger's levels decide what is shown. */
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = debug,
    };
}
