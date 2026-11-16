/*
 * OS-independent input: keyboard and mouse state derived from what the
 * backends report, the events queued for it, the public queries and the key
 * names. Must not include any OS header (ADR 0006).
 */
#include "platform/platform_internal.h"

#include <reborn/assert.h>

#include <string.h>

/* ============================================================================
 * Names
 * ========================================================================== */

/* Designated initializers: every other entry is NULL, i.e. no key at that value. */
static const char *const RB_KEY_NAMES[RB_KEY_COUNT] = {
    [RB_KEY_A] = "A",
    [RB_KEY_B] = "B",
    [RB_KEY_C] = "C",
    [RB_KEY_D] = "D",
    [RB_KEY_E] = "E",
    [RB_KEY_F] = "F",
    [RB_KEY_G] = "G",
    [RB_KEY_H] = "H",
    [RB_KEY_I] = "I",
    [RB_KEY_J] = "J",
    [RB_KEY_K] = "K",
    [RB_KEY_L] = "L",
    [RB_KEY_M] = "M",
    [RB_KEY_N] = "N",
    [RB_KEY_O] = "O",
    [RB_KEY_P] = "P",
    [RB_KEY_Q] = "Q",
    [RB_KEY_R] = "R",
    [RB_KEY_S] = "S",
    [RB_KEY_T] = "T",
    [RB_KEY_U] = "U",
    [RB_KEY_V] = "V",
    [RB_KEY_W] = "W",
    [RB_KEY_X] = "X",
    [RB_KEY_Y] = "Y",
    [RB_KEY_Z] = "Z",
    [RB_KEY_1] = "1",
    [RB_KEY_2] = "2",
    [RB_KEY_3] = "3",
    [RB_KEY_4] = "4",
    [RB_KEY_5] = "5",
    [RB_KEY_6] = "6",
    [RB_KEY_7] = "7",
    [RB_KEY_8] = "8",
    [RB_KEY_9] = "9",
    [RB_KEY_0] = "0",
    [RB_KEY_ENTER] = "Enter",
    [RB_KEY_ESCAPE] = "Escape",
    [RB_KEY_BACKSPACE] = "Backspace",
    [RB_KEY_TAB] = "Tab",
    [RB_KEY_SPACE] = "Space",
    [RB_KEY_MINUS] = "Minus",
    [RB_KEY_EQUALS] = "Equals",
    [RB_KEY_LEFT_BRACKET] = "Left Bracket",
    [RB_KEY_RIGHT_BRACKET] = "Right Bracket",
    [RB_KEY_BACKSLASH] = "Backslash",
    [RB_KEY_SEMICOLON] = "Semicolon",
    [RB_KEY_APOSTROPHE] = "Apostrophe",
    [RB_KEY_GRAVE] = "Grave",
    [RB_KEY_COMMA] = "Comma",
    [RB_KEY_PERIOD] = "Period",
    [RB_KEY_SLASH] = "Slash",
    [RB_KEY_CAPS_LOCK] = "Caps Lock",
    [RB_KEY_F1] = "F1",
    [RB_KEY_F2] = "F2",
    [RB_KEY_F3] = "F3",
    [RB_KEY_F4] = "F4",
    [RB_KEY_F5] = "F5",
    [RB_KEY_F6] = "F6",
    [RB_KEY_F7] = "F7",
    [RB_KEY_F8] = "F8",
    [RB_KEY_F9] = "F9",
    [RB_KEY_F10] = "F10",
    [RB_KEY_F11] = "F11",
    [RB_KEY_F12] = "F12",
    [RB_KEY_PRINT_SCREEN] = "Print Screen",
    [RB_KEY_SCROLL_LOCK] = "Scroll Lock",
    [RB_KEY_PAUSE] = "Pause",
    [RB_KEY_INSERT] = "Insert",
    [RB_KEY_HOME] = "Home",
    [RB_KEY_PAGE_UP] = "Page Up",
    [RB_KEY_DELETE] = "Delete",
    [RB_KEY_END] = "End",
    [RB_KEY_PAGE_DOWN] = "Page Down",
    [RB_KEY_RIGHT] = "Right",
    [RB_KEY_LEFT] = "Left",
    [RB_KEY_DOWN] = "Down",
    [RB_KEY_UP] = "Up",
    [RB_KEY_NUM_LOCK] = "Num Lock",
    [RB_KEY_KP_DIVIDE] = "Keypad Divide",
    [RB_KEY_KP_MULTIPLY] = "Keypad Multiply",
    [RB_KEY_KP_MINUS] = "Keypad Minus",
    [RB_KEY_KP_PLUS] = "Keypad Plus",
    [RB_KEY_KP_ENTER] = "Keypad Enter",
    [RB_KEY_KP_1] = "Keypad 1",
    [RB_KEY_KP_2] = "Keypad 2",
    [RB_KEY_KP_3] = "Keypad 3",
    [RB_KEY_KP_4] = "Keypad 4",
    [RB_KEY_KP_5] = "Keypad 5",
    [RB_KEY_KP_6] = "Keypad 6",
    [RB_KEY_KP_7] = "Keypad 7",
    [RB_KEY_KP_8] = "Keypad 8",
    [RB_KEY_KP_9] = "Keypad 9",
    [RB_KEY_KP_0] = "Keypad 0",
    [RB_KEY_KP_PERIOD] = "Keypad Period",
    [RB_KEY_NON_US_BACKSLASH] = "Non-US Backslash",
    [RB_KEY_APPLICATION] = "Application",
    [RB_KEY_LEFT_CTRL] = "Left Ctrl",
    [RB_KEY_LEFT_SHIFT] = "Left Shift",
    [RB_KEY_LEFT_ALT] = "Left Alt",
    [RB_KEY_LEFT_SUPER] = "Left Super",
    [RB_KEY_RIGHT_CTRL] = "Right Ctrl",
    [RB_KEY_RIGHT_SHIFT] = "Right Shift",
    [RB_KEY_RIGHT_ALT] = "Right Alt",
    [RB_KEY_RIGHT_SUPER] = "Right Super",
};

static const char *const RB_MOUSE_BUTTON_NAMES[RB_MOUSE_BUTTON_COUNT] = {
    [RB_MOUSE_BUTTON_LEFT] = "Left",     [RB_MOUSE_BUTTON_RIGHT] = "Right",
    [RB_MOUSE_BUTTON_MIDDLE] = "Middle", [RB_MOUSE_BUTTON_X1] = "X1",
    [RB_MOUSE_BUTTON_X2] = "X2",
};

/*
 * The cast to u32 also rejects negative values: an enum may hold any int, and
 * a negative index would read before the array.
 */
static bool rb_key_in_range(rb_key key) {
    return (u32)key < RB_KEY_COUNT;
}

static bool rb_mouse_button_in_range(rb_mouse_button button) {
    return (u32)button < RB_MOUSE_BUTTON_COUNT;
}

const char *rb_key_name(rb_key key) {
    const char *name = rb_key_in_range(key) ? RB_KEY_NAMES[key] : NULL;
    return name != NULL ? name : "Unknown";
}

const char *rb_mouse_button_name(rb_mouse_button button) {
    return rb_mouse_button_in_range(button) ? RB_MOUSE_BUTTON_NAMES[button] : "Unknown";
}

/* ============================================================================
 * State updates from the backends
 * ========================================================================== */

/* Either key of a pair sets the bit (ADR 0006, R1). */
static u32 rb_input_mods(const rb_input_state *input) {
    const bool *down = input->keys_down;
    u32         mods = RB_KEY_MOD_NONE;
    if (down[RB_KEY_LEFT_SHIFT] || down[RB_KEY_RIGHT_SHIFT]) {
        mods |= RB_KEY_MOD_SHIFT;
    }
    if (down[RB_KEY_LEFT_CTRL] || down[RB_KEY_RIGHT_CTRL]) {
        mods |= RB_KEY_MOD_CTRL;
    }
    if (down[RB_KEY_LEFT_ALT] || down[RB_KEY_RIGHT_ALT]) {
        mods |= RB_KEY_MOD_ALT;
    }
    if (down[RB_KEY_LEFT_SUPER] || down[RB_KEY_RIGHT_SUPER]) {
        mods |= RB_KEY_MOD_SUPER;
    }
    return mods;
}

void rb_platform_common_begin_pump(rb_platform_common *common) {
    rb_input_state *input = &common->input;
    memset(input->keys_pressed, 0, sizeof(input->keys_pressed));
    memset(input->keys_released, 0, sizeof(input->keys_released));
    memset(input->buttons_pressed, 0, sizeof(input->buttons_pressed));
    memset(input->buttons_released, 0, sizeof(input->buttons_released));
    input->wheel = (rb_mouse_wheel){0};
}

void rb_platform_common_key(rb_platform_common *common, u32 id, rb_key key, bool down) {
    /* An out-of-range key would write outside the arrays: a mapping table bug. */
    RB_ASSERT(key != RB_KEY_UNKNOWN && rb_key_in_range(key));
    rb_input_state *input = &common->input;
    bool            repeat = down && input->keys_down[key];
    if (!down && !input->keys_down[key]) {
        return; /* went down before the window had focus: no press was ever sent */
    }
    if (!repeat) {
        input->keys_down[key] = down;
        if (down) {
            input->keys_pressed[key] = true;
        } else {
            input->keys_released[key] = true;
        }
    }
    /* After the state change, so the modifiers include a modifier's own press. */
    rb_event event = {
        .type = down ? RB_EVENT_KEY_PRESSED : RB_EVENT_KEY_RELEASED,
        .window = {id},
        .key = {.key = key, .mods = rb_input_mods(input), .repeat = repeat},
    };
    rb_platform_common_push_event(common, &event);
}

void rb_platform_common_mouse_button(rb_platform_common *common, u32 id, rb_mouse_button button,
                                     bool down, rb_mouse_position position) {
    RB_ASSERT(rb_mouse_button_in_range(button));
    rb_input_state *input = &common->input;
    /* Buttons do not repeat: a second press or a stray release changes nothing. */
    if (input->buttons_down[button] == down) {
        return;
    }
    input->buttons_down[button] = down;
    if (down) {
        input->buttons_pressed[button] = true;
    } else {
        input->buttons_released[button] = true;
    }
    input->mouse_window = id;
    input->mouse_position = position;
    rb_event event = {
        .type = down ? RB_EVENT_MOUSE_BUTTON_PRESSED : RB_EVENT_MOUSE_BUTTON_RELEASED,
        .window = {id},
        .button = {.button = button,
                   .x = position.x,
                   .y = position.y,
                   .mods = rb_input_mods(input)},
    };
    rb_platform_common_push_event(common, &event);
}

void rb_platform_common_mouse_moved(rb_platform_common *common, u32 id,
                                    rb_mouse_position position) {
    rb_input_state *input = &common->input;
    /* Win32 sends WM_MOUSEMOVE also when nothing moved (activation, cursor changes). */
    if (input->mouse_window == id && input->mouse_position.x == position.x &&
        input->mouse_position.y == position.y) {
        return;
    }
    input->mouse_window = id;
    input->mouse_position = position;
    rb_event event = {.type = RB_EVENT_MOUSE_MOVED, .window = {id}, .move = position};
    rb_platform_common_push_event(common, &event);
}

void rb_platform_common_mouse_wheel(rb_platform_common *common, u32 id, rb_mouse_wheel delta) {
    rb_input_state *input = &common->input;
    input->wheel.x += delta.x;
    input->wheel.y += delta.y;
    rb_event event = {.type = RB_EVENT_MOUSE_WHEEL, .window = {id}, .wheel = delta};
    rb_platform_common_push_event(common, &event);
}

void rb_platform_common_release_input(rb_platform_common *common, u32 id) {
    rb_input_state *input = &common->input;
    for (u32 key = 0; key < RB_KEY_COUNT; ++key) {
        if (input->keys_down[key]) {
            rb_platform_common_key(common, id, (rb_key)key, false);
        }
    }
    for (u32 button = 0; button < RB_MOUSE_BUTTON_COUNT; ++button) {
        if (input->buttons_down[button]) {
            rb_platform_common_mouse_button(common, id, (rb_mouse_button)button, false,
                                            input->mouse_position);
        }
    }
}

/* ============================================================================
 * Queries
 * ========================================================================== */

static const rb_input_state *rb_input_of(const rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    return &rb_platform_common_of_const(state)->input;
}

bool rb_key_is_down(const rb_platform_state *state, rb_key key) {
    const rb_input_state *input = rb_input_of(state);
    return rb_key_in_range(key) && input->keys_down[key];
}

bool rb_key_was_pressed(const rb_platform_state *state, rb_key key) {
    const rb_input_state *input = rb_input_of(state);
    return rb_key_in_range(key) && input->keys_pressed[key];
}

bool rb_key_was_released(const rb_platform_state *state, rb_key key) {
    const rb_input_state *input = rb_input_of(state);
    return rb_key_in_range(key) && input->keys_released[key];
}

u32 rb_key_mods(const rb_platform_state *state) {
    return rb_input_mods(rb_input_of(state));
}

bool rb_mouse_button_is_down(const rb_platform_state *state, rb_mouse_button button) {
    const rb_input_state *input = rb_input_of(state);
    return rb_mouse_button_in_range(button) && input->buttons_down[button];
}

bool rb_mouse_button_was_pressed(const rb_platform_state *state, rb_mouse_button button) {
    const rb_input_state *input = rb_input_of(state);
    return rb_mouse_button_in_range(button) && input->buttons_pressed[button];
}

bool rb_mouse_button_was_released(const rb_platform_state *state, rb_mouse_button button) {
    const rb_input_state *input = rb_input_of(state);
    return rb_mouse_button_in_range(button) && input->buttons_released[button];
}

rb_window rb_mouse_get_window(const rb_platform_state *state) {
    return (rb_window){rb_input_of(state)->mouse_window};
}

rb_mouse_position rb_mouse_get_position(const rb_platform_state *state) {
    return rb_input_of(state)->mouse_position;
}

rb_mouse_wheel rb_mouse_get_wheel(const rb_platform_state *state) {
    return rb_input_of(state)->wheel;
}
