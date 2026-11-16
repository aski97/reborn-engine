/*
 * Win32 scancode to rb_key. Scancodes are "set 1", the numbering of the
 * original IBM PC keyboard that Windows still reports; keys added later (arrows
 * block, right Ctrl/Alt, Windows keys) reuse old codes with an E0 prefix, which
 * Windows reports as the extended flag.
 */
#include "platform/win32/win32_input.h"

/* 9 bits: the 8-bit scancode plus the extended flag at 0x100. */
#define WIN32_SCANCODE_COUNT 0x200u

/* rb_key values fit in a byte (all below RB_KEY_COUNT): half the size of an int table. */
static const u8 WIN32_KEYS[WIN32_SCANCODE_COUNT] = {
    [0x001] = RB_KEY_ESCAPE,
    [0x002] = RB_KEY_1,
    [0x003] = RB_KEY_2,
    [0x004] = RB_KEY_3,
    [0x005] = RB_KEY_4,
    [0x006] = RB_KEY_5,
    [0x007] = RB_KEY_6,
    [0x008] = RB_KEY_7,
    [0x009] = RB_KEY_8,
    [0x00A] = RB_KEY_9,
    [0x00B] = RB_KEY_0,
    [0x00C] = RB_KEY_MINUS,
    [0x00D] = RB_KEY_EQUALS,
    [0x00E] = RB_KEY_BACKSPACE,
    [0x00F] = RB_KEY_TAB,
    [0x010] = RB_KEY_Q,
    [0x011] = RB_KEY_W,
    [0x012] = RB_KEY_E,
    [0x013] = RB_KEY_R,
    [0x014] = RB_KEY_T,
    [0x015] = RB_KEY_Y,
    [0x016] = RB_KEY_U,
    [0x017] = RB_KEY_I,
    [0x018] = RB_KEY_O,
    [0x019] = RB_KEY_P,
    [0x01A] = RB_KEY_LEFT_BRACKET,
    [0x01B] = RB_KEY_RIGHT_BRACKET,
    [0x01C] = RB_KEY_ENTER,
    [0x01D] = RB_KEY_LEFT_CTRL,
    [0x01E] = RB_KEY_A,
    [0x01F] = RB_KEY_S,
    [0x020] = RB_KEY_D,
    [0x021] = RB_KEY_F,
    [0x022] = RB_KEY_G,
    [0x023] = RB_KEY_H,
    [0x024] = RB_KEY_J,
    [0x025] = RB_KEY_K,
    [0x026] = RB_KEY_L,
    [0x027] = RB_KEY_SEMICOLON,
    [0x028] = RB_KEY_APOSTROPHE,
    [0x029] = RB_KEY_GRAVE,
    [0x02A] = RB_KEY_LEFT_SHIFT,
    [0x02B] = RB_KEY_BACKSLASH,
    [0x02C] = RB_KEY_Z,
    [0x02D] = RB_KEY_X,
    [0x02E] = RB_KEY_C,
    [0x02F] = RB_KEY_V,
    [0x030] = RB_KEY_B,
    [0x031] = RB_KEY_N,
    [0x032] = RB_KEY_M,
    [0x033] = RB_KEY_COMMA,
    [0x034] = RB_KEY_PERIOD,
    [0x035] = RB_KEY_SLASH,
    [0x036] = RB_KEY_RIGHT_SHIFT,
    [0x037] = RB_KEY_KP_MULTIPLY,
    [0x038] = RB_KEY_LEFT_ALT,
    [0x039] = RB_KEY_SPACE,
    [0x03A] = RB_KEY_CAPS_LOCK,
    [0x03B] = RB_KEY_F1,
    [0x03C] = RB_KEY_F2,
    [0x03D] = RB_KEY_F3,
    [0x03E] = RB_KEY_F4,
    [0x03F] = RB_KEY_F5,
    [0x040] = RB_KEY_F6,
    [0x041] = RB_KEY_F7,
    [0x042] = RB_KEY_F8,
    [0x043] = RB_KEY_F9,
    [0x044] = RB_KEY_F10,
    /* In key messages Pause is 0x45 and Num Lock 0x45 extended (raw scancodes differ). */
    [0x045] = RB_KEY_PAUSE,
    [0x046] = RB_KEY_SCROLL_LOCK,
    [0x047] = RB_KEY_KP_7,
    [0x048] = RB_KEY_KP_8,
    [0x049] = RB_KEY_KP_9,
    [0x04A] = RB_KEY_KP_MINUS,
    [0x04B] = RB_KEY_KP_4,
    [0x04C] = RB_KEY_KP_5,
    [0x04D] = RB_KEY_KP_6,
    [0x04E] = RB_KEY_KP_PLUS,
    [0x04F] = RB_KEY_KP_1,
    [0x050] = RB_KEY_KP_2,
    [0x051] = RB_KEY_KP_3,
    [0x052] = RB_KEY_KP_0,
    [0x053] = RB_KEY_KP_PERIOD,
    [0x056] = RB_KEY_NON_US_BACKSLASH,
    [0x057] = RB_KEY_F11,
    [0x058] = RB_KEY_F12,

    [0x11C] = RB_KEY_KP_ENTER,
    [0x11D] = RB_KEY_RIGHT_CTRL,
    [0x135] = RB_KEY_KP_DIVIDE,
    [0x137] = RB_KEY_PRINT_SCREEN,
    [0x138] = RB_KEY_RIGHT_ALT,
    [0x145] = RB_KEY_NUM_LOCK,
    [0x147] = RB_KEY_HOME,
    [0x148] = RB_KEY_UP,
    [0x149] = RB_KEY_PAGE_UP,
    [0x14B] = RB_KEY_LEFT,
    [0x14D] = RB_KEY_RIGHT,
    [0x14F] = RB_KEY_END,
    [0x150] = RB_KEY_DOWN,
    [0x151] = RB_KEY_PAGE_DOWN,
    [0x152] = RB_KEY_INSERT,
    [0x153] = RB_KEY_DELETE,
    [0x15B] = RB_KEY_LEFT_SUPER,
    [0x15C] = RB_KEY_RIGHT_SUPER,
    [0x15D] = RB_KEY_APPLICATION,
};

rb_key win32_key_from_scancode(u32 scancode) {
    /*
     * The same key can arrive with another code depending on what is held
     * with it; these are the variants Windows is known to send.
     */
    switch (scancode) {
    case 0x054: /* Alt+Print Screen sends the old SysRq code */
        scancode = 0x137;
        break;
    case 0x146: /* Ctrl+Pause sends the Break code */
        scancode = 0x045;
        break;
    case 0x136: /* some CJK input methods set the extended flag on Right Shift */
        scancode = 0x036;
        break;
    default:
        break;
    }
    if (scancode >= WIN32_SCANCODE_COUNT) {
        return RB_KEY_UNKNOWN;
    }
    return (rb_key)WIN32_KEYS[scancode];
}
