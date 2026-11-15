#include "rb_test.h"

#include <stdio.h>

#include <reborn/version.h>

/* The header describes what we compiled against, the functions what we linked. */
RB_TEST(version, string_matches_header) {
    RB_EXPECT_EQ_STR(rb_version_string(), RB_VERSION_STRING);
}

RB_TEST(version, number_matches_header) {
    RB_EXPECT_EQ_UINT(rb_version_number(), RB_VERSION_NUMBER);
}

/* Same 10.10.12 layout as VK_MAKE_API_VERSION: Vulkan will decode it this way. */
RB_TEST(version, number_unpacks_to_components) {
    u32 number = rb_version_number();
    RB_EXPECT_EQ_UINT(number >> 22, RB_VERSION_MAJOR);
    RB_EXPECT_EQ_UINT((number >> 12) & 0x3FFu, RB_VERSION_MINOR);
    RB_EXPECT_EQ_UINT(number & 0xFFFu, RB_VERSION_PATCH);
}

RB_TEST(version, string_matches_components) {
    char expected[32];
    snprintf(expected, sizeof expected, "%d.%d.%d", RB_VERSION_MAJOR, RB_VERSION_MINOR,
             RB_VERSION_PATCH);
    RB_EXPECT_EQ_STR(rb_version_string(), expected);
}
