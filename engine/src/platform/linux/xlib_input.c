/*
 * X11 keycode to rb_key. Every current Linux X server (Xorg with libinput or
 * evdev, XWayland, Xvfb) numbers keys as the kernel's evdev codes plus 8:
 * X reserves keycodes 0-7. The evdev codes are those of
 * <linux/input-event-codes.h>; for the keys of the original PC keyboard they
 * equal the PC "set 1" scancodes Win32 reports. A server with another
 * numbering needs XKB key names instead (ADR 0006, P2).
 */
#include "platform/linux/xlib_input.h"

#define XLIB_KEYCODE_OFFSET 8u
/* The highest evdev code mapped below is 127 (KEY_COMPOSE). */
#define XLIB_EVDEV_COUNT 128u

/* Indexed by evdev code; rb_key values fit in a byte (all below RB_KEY_COUNT). */
static const u8 XLIB_KEYS[XLIB_EVDEV_COUNT] = {
    [1] = RB_KEY_ESCAPE,
    [2] = RB_KEY_1,
    [3] = RB_KEY_2,
    [4] = RB_KEY_3,
    [5] = RB_KEY_4,
    [6] = RB_KEY_5,
    [7] = RB_KEY_6,
    [8] = RB_KEY_7,
    [9] = RB_KEY_8,
    [10] = RB_KEY_9,
    [11] = RB_KEY_0,
    [12] = RB_KEY_MINUS,
    [13] = RB_KEY_EQUALS,
    [14] = RB_KEY_BACKSPACE,
    [15] = RB_KEY_TAB,
    [16] = RB_KEY_Q,
    [17] = RB_KEY_W,
    [18] = RB_KEY_E,
    [19] = RB_KEY_R,
    [20] = RB_KEY_T,
    [21] = RB_KEY_Y,
    [22] = RB_KEY_U,
    [23] = RB_KEY_I,
    [24] = RB_KEY_O,
    [25] = RB_KEY_P,
    [26] = RB_KEY_LEFT_BRACKET,
    [27] = RB_KEY_RIGHT_BRACKET,
    [28] = RB_KEY_ENTER,
    [29] = RB_KEY_LEFT_CTRL,
    [30] = RB_KEY_A,
    [31] = RB_KEY_S,
    [32] = RB_KEY_D,
    [33] = RB_KEY_F,
    [34] = RB_KEY_G,
    [35] = RB_KEY_H,
    [36] = RB_KEY_J,
    [37] = RB_KEY_K,
    [38] = RB_KEY_L,
    [39] = RB_KEY_SEMICOLON,
    [40] = RB_KEY_APOSTROPHE,
    [41] = RB_KEY_GRAVE,
    [42] = RB_KEY_LEFT_SHIFT,
    [43] = RB_KEY_BACKSLASH,
    [44] = RB_KEY_Z,
    [45] = RB_KEY_X,
    [46] = RB_KEY_C,
    [47] = RB_KEY_V,
    [48] = RB_KEY_B,
    [49] = RB_KEY_N,
    [50] = RB_KEY_M,
    [51] = RB_KEY_COMMA,
    [52] = RB_KEY_PERIOD,
    [53] = RB_KEY_SLASH,
    [54] = RB_KEY_RIGHT_SHIFT,
    [55] = RB_KEY_KP_MULTIPLY,
    [56] = RB_KEY_LEFT_ALT,
    [57] = RB_KEY_SPACE,
    [58] = RB_KEY_CAPS_LOCK,
    [59] = RB_KEY_F1,
    [60] = RB_KEY_F2,
    [61] = RB_KEY_F3,
    [62] = RB_KEY_F4,
    [63] = RB_KEY_F5,
    [64] = RB_KEY_F6,
    [65] = RB_KEY_F7,
    [66] = RB_KEY_F8,
    [67] = RB_KEY_F9,
    [68] = RB_KEY_F10,
    [69] = RB_KEY_NUM_LOCK,
    [70] = RB_KEY_SCROLL_LOCK,
    [71] = RB_KEY_KP_7,
    [72] = RB_KEY_KP_8,
    [73] = RB_KEY_KP_9,
    [74] = RB_KEY_KP_MINUS,
    [75] = RB_KEY_KP_4,
    [76] = RB_KEY_KP_5,
    [77] = RB_KEY_KP_6,
    [78] = RB_KEY_KP_PLUS,
    [79] = RB_KEY_KP_1,
    [80] = RB_KEY_KP_2,
    [81] = RB_KEY_KP_3,
    [82] = RB_KEY_KP_0,
    [83] = RB_KEY_KP_PERIOD,
    [86] = RB_KEY_NON_US_BACKSLASH, /* KEY_102ND */
    [87] = RB_KEY_F11,
    [88] = RB_KEY_F12,
    [96] = RB_KEY_KP_ENTER,
    [97] = RB_KEY_RIGHT_CTRL,
    [98] = RB_KEY_KP_DIVIDE,
    [99] = RB_KEY_PRINT_SCREEN, /* KEY_SYSRQ */
    [100] = RB_KEY_RIGHT_ALT,
    [102] = RB_KEY_HOME,
    [103] = RB_KEY_UP,
    [104] = RB_KEY_PAGE_UP,
    [105] = RB_KEY_LEFT,
    [106] = RB_KEY_RIGHT,
    [107] = RB_KEY_END,
    [108] = RB_KEY_DOWN,
    [109] = RB_KEY_PAGE_DOWN,
    [110] = RB_KEY_INSERT,
    [111] = RB_KEY_DELETE,
    [119] = RB_KEY_PAUSE,
    [125] = RB_KEY_LEFT_SUPER,  /* KEY_LEFTMETA */
    [126] = RB_KEY_RIGHT_SUPER, /* KEY_RIGHTMETA */
    [127] = RB_KEY_APPLICATION, /* KEY_COMPOSE */
};

rb_key xlib_key_from_keycode(u32 keycode) {
    if (keycode < XLIB_KEYCODE_OFFSET || keycode - XLIB_KEYCODE_OFFSET >= XLIB_EVDEV_COUNT) {
        return RB_KEY_UNKNOWN;
    }
    return (rb_key)XLIB_KEYS[keycode - XLIB_KEYCODE_OFFSET];
}
