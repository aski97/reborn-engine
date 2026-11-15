/*
 * A failing assert stops the process, so only passing ones are tested here.
 * Testing failures needs a replaceable handler (backlog).
 */
#include "rb_test.h"

#include <reborn/assert.h>

RB_TEST(assert, enabled_in_debug_builds) {
#if defined(RB_DEBUG)
    RB_EXPECT_EQ_INT(RB_ASSERT_ENABLED, 1);
#else
    RB_EXPECT_EQ_INT(RB_ASSERT_ENABLED, 0);
#endif
}

RB_TEST(assert, passing_assert_continues) {
    int evaluated = 0;
    RB_ASSERT(++evaluated == 1);
    RB_ASSERT_MSG(evaluated >= 0, "never shown: %d", evaluated);
    RB_EXPECT_EQ_INT(evaluated, RB_ASSERT_ENABLED ? 1 : 0);
}
