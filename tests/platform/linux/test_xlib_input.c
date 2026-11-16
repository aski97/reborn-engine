/* X11 keycode to rb_key (ADR 0006, P1). Built on Linux only. */
#include "rb_test.h"

#include <string.h>

#include "platform/linux/xlib_input.h"

/* X11 keycodes are evdev codes + 8: KEY_Q is 16, so its keycode is 24. */
RB_TEST(xlib_input, letters_follow_the_qwerty_positions) {
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(24), RB_KEY_Q);
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(25), RB_KEY_W);
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(38), RB_KEY_A);
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(52), RB_KEY_Z);
}

RB_TEST(xlib_input, keys_beyond_the_pc_keyboard) {
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(37), RB_KEY_LEFT_CTRL);        /* KEY_LEFTCTRL 29 */
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(105), RB_KEY_RIGHT_CTRL);      /* KEY_RIGHTCTRL 97 */
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(94), RB_KEY_NON_US_BACKSLASH); /* KEY_102ND 86 */
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(111), RB_KEY_UP);              /* KEY_UP 103 */
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(135), RB_KEY_APPLICATION);     /* KEY_COMPOSE 127 */
}

RB_TEST(xlib_input, unmapped_and_out_of_range_keycodes_are_unknown) {
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(0), RB_KEY_UNKNOWN);
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(7), RB_KEY_UNKNOWN); /* X reserves 0-7 */
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(8), RB_KEY_UNKNOWN); /* evdev KEY_RESERVED */
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(136), RB_KEY_UNKNOWN);
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(255), RB_KEY_UNKNOWN);
    RB_EXPECT_EQ_INT(xlib_key_from_keycode(0xFFFFFFFFu), RB_KEY_UNKNOWN);
}

RB_TEST(xlib_input, every_named_key_has_exactly_one_keycode) {
    u32 hits[RB_KEY_COUNT] = {0};
    for (u32 keycode = 0; keycode < 256u; ++keycode) {
        ++hits[xlib_key_from_keycode(keycode)];
    }
    /* The first key with a wrong count: the failure message then prints its value. */
    u32 wrong_key = 0;
    for (u32 key = 1; key < RB_KEY_COUNT && wrong_key == 0; ++key) {
        bool named = strcmp(rb_key_name((rb_key)key), "Unknown") != 0;
        if (hits[key] != (named ? 1u : 0u)) {
            wrong_key = key;
        }
    }
    RB_EXPECT_EQ_UINT(wrong_key, 0);
}
