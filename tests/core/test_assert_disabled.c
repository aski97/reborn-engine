/*
 * Asserts as release builds see them, in any build: the override must come
 * before the header, which only defines RB_ASSERT_ENABLED when it is missing.
 */
#define RB_ASSERT_ENABLED 0

#include "rb_test.h"

#include <reborn/assert.h>

RB_TEST(assert, disabled_assert_never_evaluates_its_condition) {
    int evaluated = 0;
    RB_ASSERT(++evaluated == 100);
    RB_ASSERT_MSG(++evaluated == 100, "never shown: %d", 100);
    RB_EXPECT_EQ_INT(evaluated, 0);
}
