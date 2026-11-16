/*
 * Keys, mouse buttons and modifiers (ADR 0006, F1 and O1). Included by
 * reborn/platform.h, which has the input events and queries.
 *
 * A key is a physical position, not a character: RB_KEY_W is the key where a
 * US QWERTY keyboard prints W, wherever the current layout puts that letter
 * (on AZERTY it prints Z). WASD bindings stay in place on every layout. Text
 * input is a separate concern (not yet available).
 */
#ifndef RB_INPUT_H
#define RB_INPUT_H

#include <reborn/defines.h>

/* ============================================================================
 * Keys
 * ========================================================================== */

/*
 * Values are USB HID usage IDs (keyboard page): a published standard, so the
 * list is not ours to invent. They are sparse and all below RB_KEY_COUNT.
 */
typedef enum rb_key {
    RB_KEY_UNKNOWN = 0,

    RB_KEY_A = 0x04,
    RB_KEY_B = 0x05,
    RB_KEY_C = 0x06,
    RB_KEY_D = 0x07,
    RB_KEY_E = 0x08,
    RB_KEY_F = 0x09,
    RB_KEY_G = 0x0A,
    RB_KEY_H = 0x0B,
    RB_KEY_I = 0x0C,
    RB_KEY_J = 0x0D,
    RB_KEY_K = 0x0E,
    RB_KEY_L = 0x0F,
    RB_KEY_M = 0x10,
    RB_KEY_N = 0x11,
    RB_KEY_O = 0x12,
    RB_KEY_P = 0x13,
    RB_KEY_Q = 0x14,
    RB_KEY_R = 0x15,
    RB_KEY_S = 0x16,
    RB_KEY_T = 0x17,
    RB_KEY_U = 0x18,
    RB_KEY_V = 0x19,
    RB_KEY_W = 0x1A,
    RB_KEY_X = 0x1B,
    RB_KEY_Y = 0x1C,
    RB_KEY_Z = 0x1D,

    /* Top row digits; the keypad ones are RB_KEY_KP_*. */
    RB_KEY_1 = 0x1E,
    RB_KEY_2 = 0x1F,
    RB_KEY_3 = 0x20,
    RB_KEY_4 = 0x21,
    RB_KEY_5 = 0x22,
    RB_KEY_6 = 0x23,
    RB_KEY_7 = 0x24,
    RB_KEY_8 = 0x25,
    RB_KEY_9 = 0x26,
    RB_KEY_0 = 0x27,

    RB_KEY_ENTER = 0x28,
    RB_KEY_ESCAPE = 0x29,
    RB_KEY_BACKSPACE = 0x2A,
    RB_KEY_TAB = 0x2B,
    RB_KEY_SPACE = 0x2C,
    RB_KEY_MINUS = 0x2D,         /* right of 0 */
    RB_KEY_EQUALS = 0x2E,        /* left of Backspace */
    RB_KEY_LEFT_BRACKET = 0x2F,  /* right of P */
    RB_KEY_RIGHT_BRACKET = 0x30, /* two right of P */
    /*
     * Above Enter on ANSI keyboards, left of it on ISO ones: both send the same
     * scancode, so the OS cannot tell them apart (HID's NON_US_HASH, 0x32, is
     * never reported).
     */
    RB_KEY_BACKSLASH = 0x31,
    RB_KEY_SEMICOLON = 0x33,  /* right of L */
    RB_KEY_APOSTROPHE = 0x34, /* two right of L */
    RB_KEY_GRAVE = 0x35,      /* left of 1 */
    RB_KEY_COMMA = 0x36,      /* right of M */
    RB_KEY_PERIOD = 0x37,     /* two right of M */
    RB_KEY_SLASH = 0x38,      /* three right of M */
    RB_KEY_CAPS_LOCK = 0x39,

    RB_KEY_F1 = 0x3A,
    RB_KEY_F2 = 0x3B,
    RB_KEY_F3 = 0x3C,
    RB_KEY_F4 = 0x3D,
    RB_KEY_F5 = 0x3E,
    RB_KEY_F6 = 0x3F,
    RB_KEY_F7 = 0x40,
    RB_KEY_F8 = 0x41,
    RB_KEY_F9 = 0x42,
    RB_KEY_F10 = 0x43,
    RB_KEY_F11 = 0x44,
    RB_KEY_F12 = 0x45,

    RB_KEY_PRINT_SCREEN = 0x46,
    RB_KEY_SCROLL_LOCK = 0x47,
    RB_KEY_PAUSE = 0x48,
    RB_KEY_INSERT = 0x49,
    RB_KEY_HOME = 0x4A,
    RB_KEY_PAGE_UP = 0x4B,
    RB_KEY_DELETE = 0x4C,
    RB_KEY_END = 0x4D,
    RB_KEY_PAGE_DOWN = 0x4E,
    RB_KEY_RIGHT = 0x4F,
    RB_KEY_LEFT = 0x50,
    RB_KEY_DOWN = 0x51,
    RB_KEY_UP = 0x52,

    /* Keypad keys are positions too: with Num Lock off they still report RB_KEY_KP_*. */
    RB_KEY_NUM_LOCK = 0x53,
    RB_KEY_KP_DIVIDE = 0x54,
    RB_KEY_KP_MULTIPLY = 0x55,
    RB_KEY_KP_MINUS = 0x56,
    RB_KEY_KP_PLUS = 0x57,
    RB_KEY_KP_ENTER = 0x58,
    RB_KEY_KP_1 = 0x59,
    RB_KEY_KP_2 = 0x5A,
    RB_KEY_KP_3 = 0x5B,
    RB_KEY_KP_4 = 0x5C,
    RB_KEY_KP_5 = 0x5D,
    RB_KEY_KP_6 = 0x5E,
    RB_KEY_KP_7 = 0x5F,
    RB_KEY_KP_8 = 0x60,
    RB_KEY_KP_9 = 0x61,
    RB_KEY_KP_0 = 0x62,
    RB_KEY_KP_PERIOD = 0x63,

    /* ISO keyboards only: between Left Shift and Z. */
    RB_KEY_NON_US_BACKSLASH = 0x64,
    /* The context menu key, right of Right Super. */
    RB_KEY_APPLICATION = 0x65,

    RB_KEY_LEFT_CTRL = 0xE0,
    RB_KEY_LEFT_SHIFT = 0xE1,
    RB_KEY_LEFT_ALT = 0xE2,
    RB_KEY_LEFT_SUPER = 0xE3, /* Windows key, Command on Apple keyboards */
    RB_KEY_RIGHT_CTRL = 0xE4,
    RB_KEY_RIGHT_SHIFT = 0xE5,
    RB_KEY_RIGHT_ALT = 0xE6, /* AltGr on many layouts */
    RB_KEY_RIGHT_SUPER = 0xE7,
} rb_key;

/* Every rb_key is below this: arrays indexed by rb_key have this many entries. */
#define RB_KEY_COUNT 256

/*
 * The enum's name for the position, as on US QWERTY ("W", "Left Shift"), for
 * logs: not the label printed on the user's keyboard. "Unknown" for values
 * that name no key.
 */
RB_API const char *rb_key_name(rb_key key);

/* ============================================================================
 * Modifiers
 * ========================================================================== */

/* Bit flags: either key of a pair sets the bit (Left or Right Shift both give SHIFT). */
typedef enum rb_key_mod {
    RB_KEY_MOD_NONE = 0,
    RB_KEY_MOD_SHIFT = 1u << 0,
    RB_KEY_MOD_CTRL = 1u << 1,
    RB_KEY_MOD_ALT = 1u << 2,
    RB_KEY_MOD_SUPER = 1u << 3,
} rb_key_mod;

/* ============================================================================
 * Mouse
 * ========================================================================== */

typedef enum rb_mouse_button {
    RB_MOUSE_BUTTON_LEFT = 0,
    RB_MOUSE_BUTTON_RIGHT,
    RB_MOUSE_BUTTON_MIDDLE,
    RB_MOUSE_BUTTON_X1, /* side button, usually "back" */
    RB_MOUSE_BUTTON_X2, /* side button, usually "forward" */
    RB_MOUSE_BUTTON_COUNT,
} rb_mouse_button;

/* "Left", "Right"...; "Unknown" for values that name no button. */
RB_API const char *rb_mouse_button_name(rb_mouse_button button);

#endif /* RB_INPUT_H */
