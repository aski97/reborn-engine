/* Win32 scancode to rb_key (ADR 0006, F1). Built on Windows only. */
#include "rb_test.h"

#include <string.h>

#include "platform/win32/win32_input.h"

RB_TEST(win32_input, letters_follow_the_qwerty_positions) {
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x10), RB_KEY_Q);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x11), RB_KEY_W);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x1E), RB_KEY_A);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x2C), RB_KEY_Z);
}

RB_TEST(win32_input, extended_flag_tells_keys_apart) {
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x01D), RB_KEY_LEFT_CTRL);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x11D), RB_KEY_RIGHT_CTRL);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x01C), RB_KEY_ENTER);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x11C), RB_KEY_KP_ENTER);
    /* Keypad 8 and the arrow above the cursor block share 0x48. */
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x048), RB_KEY_KP_8);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x148), RB_KEY_UP);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x045), RB_KEY_PAUSE);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x145), RB_KEY_NUM_LOCK);
}

RB_TEST(win32_input, alternative_codes_map_to_the_same_key) {
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x054), RB_KEY_PRINT_SCREEN); /* Alt+Print Screen */
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x146), RB_KEY_PAUSE);        /* Ctrl+Pause */
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x136), RB_KEY_RIGHT_SHIFT);  /* CJK input method */
}

RB_TEST(win32_input, unmapped_and_out_of_range_codes_are_unknown) {
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x000), RB_KEY_UNKNOWN);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x15F), RB_KEY_UNKNOWN); /* Sleep */
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0x200), RB_KEY_UNKNOWN);
    RB_EXPECT_EQ_INT(win32_key_from_scancode(0xFFFFFFFFu), RB_KEY_UNKNOWN);
}

RB_TEST(win32_input, every_named_key_has_exactly_one_scancode) {
    u32 hits[RB_KEY_COUNT] = {0};
    for (u32 scancode = 0; scancode < 0x200u; ++scancode) {
        /* The alternative codes are second names on purpose: skip them. */
        if (scancode == 0x054 || scancode == 0x146 || scancode == 0x136) {
            continue;
        }
        ++hits[win32_key_from_scancode(scancode)];
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
