/*
 * Counting validation messages and choosing when to stop (ADR 0013, F3). The
 * callback itself is exercised end to end in test_vulkan_instance.c.
 */
#include "rb_test.h"

#include "renderer/vulkan/vulkan_debug.h"

#define ERROR_BIT   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
#define WARNING_BIT VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
#define INFO_BIT    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
#define VALIDATION  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
#define GENERAL     VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
#define PERFORMANCE VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT

RB_TEST(vulkan_debug, validation_errors_and_warnings_are_counted) {
    rb_vk_debug debug = {0};
    rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION, 1);
    rb_vk_debug_record(&debug, WARNING_BIT, VALIDATION, 2);
    rb_vk_debug_record(&debug, WARNING_BIT, VALIDATION, 2);
    RB_EXPECT_EQ_UINT(debug.stats.errors, 1);
    RB_EXPECT_EQ_UINT(debug.stats.warnings, 2);
}

RB_TEST(vulkan_debug, info_and_other_types_are_not_counted) {
    rb_vk_debug debug = {0};
    RB_EXPECT(!rb_vk_debug_record(&debug, INFO_BIT, VALIDATION, 1));
    RB_EXPECT(!rb_vk_debug_record(&debug, ERROR_BIT, GENERAL, 2));
    RB_EXPECT(!rb_vk_debug_record(&debug, WARNING_BIT, PERFORMANCE, 3));
    RB_EXPECT_EQ_UINT(debug.stats.errors, 0);
    RB_EXPECT_EQ_UINT(debug.stats.warnings, 0);
}

/* A message can carry several type bits: validation among them is enough. */
RB_TEST(vulkan_debug, validation_bit_among_others_counts) {
    rb_vk_debug debug = {0};
    RB_EXPECT(rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION | PERFORMANCE, 1));
    RB_EXPECT_EQ_UINT(debug.stats.errors, 1);
}

RB_TEST(vulkan_debug, stops_once_per_error_id) {
    rb_vk_debug debug = {0};
    RB_EXPECT(rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION, 42));
    RB_EXPECT(!rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION, 42));
    RB_EXPECT(rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION, -7));
    RB_EXPECT_EQ_UINT(debug.stats.errors, 3);
}

RB_TEST(vulkan_debug, warnings_never_stop) {
    rb_vk_debug debug = {0};
    RB_EXPECT(!rb_vk_debug_record(&debug, WARNING_BIT, VALIDATION, 5));
    RB_EXPECT_EQ_UINT(debug.seen_count, 0);
}

RB_TEST(vulkan_debug, full_table_still_counts_without_stopping) {
    rb_vk_debug debug = {0};
    for (i32 id = 0; id < RB_VK_DEBUG_SEEN_MAX; ++id) {
        RB_EXPECT(rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION, id));
    }
    RB_EXPECT(!rb_vk_debug_record(&debug, ERROR_BIT, VALIDATION, RB_VK_DEBUG_SEEN_MAX));
    RB_EXPECT_EQ_UINT(debug.stats.errors, RB_VK_DEBUG_SEEN_MAX + 1);
}

RB_TEST(vulkan_debug, messenger_info_points_back_to_the_state) {
    rb_vk_debug                        debug = {0};
    VkDebugUtilsMessengerCreateInfoEXT info = rb_vk_debug_messenger_info(&debug);
    RB_EXPECT(info.sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    RB_EXPECT(info.pUserData == &debug);
    RB_EXPECT(info.pfnUserCallback != NULL);
    RB_EXPECT((info.messageSeverity & ERROR_BIT) != 0);
    RB_EXPECT((info.messageType & VALIDATION) != 0);
}
