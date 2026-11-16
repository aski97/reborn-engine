/*
 * OS-independent input state (ADR 0006, Q1-U1): what the common code does with
 * the keys, buttons and mouse moves the backends report.
 */
#include "rb_test.h"

#include <string.h>

#include "platform/platform_internal.h"

/* ============================================================================
 * Helpers
 * ========================================================================== */

/* A live window in `common`, as a backend would open it. */
static u32 open_window(rb_platform_common *common) {
    u32 id = rb_handle_pool_alloc(&common->window_pool);
    rb_platform_common_window_opened(common, id, (rb_window_size){640, 480, 640, 480});
    return id;
}

/* A focused window with no events queued, ready to receive input. */
static u32 open_focused_window(rb_platform_common *common) {
    u32      id = open_window(common);
    rb_event ev;
    rb_platform_common_window_focused(common, id, true);
    while (rb_platform_common_poll_event(common, &ev)) {
    }
    return id;
}

static rb_event next_event(rb_platform_common *common) {
    rb_event ev = {0};
    rb_platform_common_poll_event(common, &ev);
    return ev;
}

static u32 count_events(rb_platform_common *common) {
    u32      count = 0;
    rb_event ev;
    while (rb_platform_common_poll_event(common, &ev)) {
        ++count;
    }
    return count;
}

static rb_mouse_position at(i32 x, i32 y) {
    return (rb_mouse_position){x, y};
}

/* ============================================================================
 * Keys
 * ========================================================================== */

RB_TEST(input, common_starts_with_nothing_down) {
    rb_platform_common common;
    memset(&common, 0xAB, sizeof(common)); /* init must not rely on zeroed memory */
    RB_REQUIRE(rb_platform_common_init(&common));

    RB_EXPECT(!common.input.keys_down[RB_KEY_W]);
    RB_EXPECT(!common.input.buttons_down[RB_MOUSE_BUTTON_LEFT]);
    RB_EXPECT_EQ_UINT(common.input.mouse_window, 0);
    RB_EXPECT_EQ_INT(common.input.mouse_position.x, 0);
}

RB_TEST(input, key_press_and_release_send_events_and_update_state) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_key(&common, id, RB_KEY_W, true);
    RB_EXPECT(common.input.keys_down[RB_KEY_W]);
    RB_EXPECT(common.input.keys_pressed[RB_KEY_W]);
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_KEY_PRESSED);
    RB_EXPECT_EQ_UINT(ev.window.id, id);
    RB_EXPECT_EQ_INT(ev.key.key, RB_KEY_W);
    RB_EXPECT(!ev.key.repeat);
    RB_EXPECT_EQ_UINT(ev.key.mods, RB_KEY_MOD_NONE);

    rb_platform_common_key(&common, id, RB_KEY_W, false);
    RB_EXPECT(!common.input.keys_down[RB_KEY_W]);
    RB_EXPECT(common.input.keys_released[RB_KEY_W]);
    ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_KEY_RELEASED);
    RB_EXPECT_EQ_INT(ev.key.key, RB_KEY_W);
}

RB_TEST(input, press_and_release_in_one_pump_report_both_edges) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    /* The flaw of a state-only API (ADR 0006, D3): a quick tap must not vanish. */
    rb_platform_common_begin_pump(&common);
    rb_platform_common_key(&common, id, RB_KEY_SPACE, true);
    rb_platform_common_key(&common, id, RB_KEY_SPACE, false);
    RB_EXPECT(!common.input.keys_down[RB_KEY_SPACE]);
    RB_EXPECT(common.input.keys_pressed[RB_KEY_SPACE]);
    RB_EXPECT(common.input.keys_released[RB_KEY_SPACE]);

    /* The next pump starts clean: edges belong to one pump only. */
    rb_platform_common_begin_pump(&common);
    RB_EXPECT(!common.input.keys_pressed[RB_KEY_SPACE]);
    RB_EXPECT(!common.input.keys_released[RB_KEY_SPACE]);
}

RB_TEST(input, held_key_repeats_without_a_new_press_edge) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_key(&common, id, RB_KEY_DOWN, true);
    rb_platform_common_begin_pump(&common);
    rb_platform_common_key(&common, id, RB_KEY_DOWN, true); /* OS auto-repeat */

    RB_EXPECT(common.input.keys_down[RB_KEY_DOWN]);
    RB_EXPECT(!common.input.keys_pressed[RB_KEY_DOWN]);
    RB_EXPECT(!next_event(&common).key.repeat);
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_KEY_PRESSED);
    RB_EXPECT(ev.key.repeat);
}

RB_TEST(input, release_of_a_key_never_pressed_sends_nothing) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    /* Pressed before the window had focus: the press was never reported. */
    rb_platform_common_key(&common, id, RB_KEY_A, false);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
    RB_EXPECT(!common.input.keys_released[RB_KEY_A]);
}

RB_TEST(input, mods_follow_either_key_of_a_pair) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_key(&common, id, RB_KEY_RIGHT_SHIFT, true);
    rb_platform_common_key(&common, id, RB_KEY_LEFT_CTRL, true);
    rb_platform_common_key(&common, id, RB_KEY_S, true);

    /* A modifier's own press already carries its bit. */
    RB_EXPECT_EQ_UINT(next_event(&common).key.mods, RB_KEY_MOD_SHIFT);
    RB_EXPECT_EQ_UINT(next_event(&common).key.mods, RB_KEY_MOD_SHIFT | RB_KEY_MOD_CTRL);
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.key.key, RB_KEY_S);
    RB_EXPECT_EQ_UINT(ev.key.mods, RB_KEY_MOD_SHIFT | RB_KEY_MOD_CTRL);

    /* ...and its release no longer does. */
    rb_platform_common_key(&common, id, RB_KEY_RIGHT_SHIFT, false);
    RB_EXPECT_EQ_UINT(next_event(&common).key.mods, RB_KEY_MOD_CTRL);
}

/* ============================================================================
 * Mouse
 * ========================================================================== */

RB_TEST(input, button_events_carry_position_and_mods) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_key(&common, id, RB_KEY_LEFT_ALT, true);
    count_events(&common);
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_RIGHT, true, at(10, 20));
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_BUTTON_PRESSED);
    RB_EXPECT_EQ_INT(ev.button.button, RB_MOUSE_BUTTON_RIGHT);
    RB_EXPECT_EQ_INT(ev.button.x, 10);
    RB_EXPECT_EQ_INT(ev.button.y, 20);
    RB_EXPECT_EQ_UINT(ev.button.mods, RB_KEY_MOD_ALT);
    RB_EXPECT(common.input.buttons_down[RB_MOUSE_BUTTON_RIGHT]);
    RB_EXPECT(common.input.buttons_pressed[RB_MOUSE_BUTTON_RIGHT]);

    /* Released outside the window while captured: negative coordinates are valid. */
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_RIGHT, false, at(-5, 30));
    ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_BUTTON_RELEASED);
    RB_EXPECT_EQ_INT(ev.button.x, -5);
    RB_EXPECT_EQ_INT(common.input.mouse_position.x, -5);
    RB_EXPECT(common.input.buttons_released[RB_MOUSE_BUTTON_RIGHT]);
}

RB_TEST(input, repeated_button_press_or_stray_release_sends_nothing) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_LEFT, false, at(0, 0));
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_LEFT, true, at(0, 0));
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_LEFT, true, at(0, 0));
    RB_EXPECT_EQ_UINT(count_events(&common), 1);
}

RB_TEST(input, mouse_moves_merge_and_unchanged_position_sends_nothing) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    for (i32 x = 1; x <= (i32)RB_EVENT_QUEUE_CAPACITY * 2; ++x) {
        rb_platform_common_mouse_moved(&common, id, at(x, 7));
    }
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_MOVED);
    RB_EXPECT_EQ_INT(ev.move.x, (i32)RB_EVENT_QUEUE_CAPACITY * 2);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
    RB_EXPECT_EQ_UINT(common.dropped_events, 0);

    /* What Win32 sends on activation or a cursor change. */
    rb_platform_common_mouse_moved(&common, id, at((i32)RB_EVENT_QUEUE_CAPACITY * 2, 7));
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
}

RB_TEST(input, mouse_moves_do_not_merge_across_a_click) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_mouse_moved(&common, id, at(1, 1));
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_LEFT, true, at(1, 1));
    rb_platform_common_mouse_moved(&common, id, at(2, 2));
    rb_platform_common_mouse_moved(&common, id, at(3, 3));

    RB_EXPECT_EQ_INT(next_event(&common).type, RB_EVENT_MOUSE_MOVED);
    RB_EXPECT_EQ_INT(next_event(&common).type, RB_EVENT_MOUSE_BUTTON_PRESSED);
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_MOVED);
    RB_EXPECT_EQ_INT(ev.move.x, 3);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
}

RB_TEST(input, wheel_events_sum_in_queue_and_state) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_begin_pump(&common);
    rb_platform_common_mouse_wheel(&common, id, (rb_mouse_wheel){0.0f, 1.0f});
    rb_platform_common_mouse_wheel(&common, id, (rb_mouse_wheel){0.0f, 0.5f});
    rb_platform_common_mouse_wheel(&common, id, (rb_mouse_wheel){-1.0f, 0.0f});

    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_WHEEL);
    RB_EXPECT_NEAR((f64)ev.wheel.x, -1.0, 1e-6);
    RB_EXPECT_NEAR((f64)ev.wheel.y, 1.5, 1e-6);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
    RB_EXPECT_NEAR((f64)common.input.wheel.y, 1.5, 1e-6);

    rb_platform_common_begin_pump(&common);
    RB_EXPECT_NEAR((f64)common.input.wheel.y, 0.0, 1e-6);
}

/* ============================================================================
 * Focus and window lifetime
 * ========================================================================== */

RB_TEST(input, focus_loss_releases_held_keys_and_buttons_first) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_key(&common, id, RB_KEY_LEFT_ALT, true);
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_LEFT, true, at(4, 5));
    count_events(&common);

    /* Alt+Tab: Alt's release will reach the other window, never this one. */
    rb_platform_common_window_focused(&common, id, false);
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_KEY_RELEASED);
    RB_EXPECT_EQ_INT(ev.key.key, RB_KEY_LEFT_ALT);
    RB_EXPECT_EQ_UINT(ev.window.id, id);
    ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_BUTTON_RELEASED);
    RB_EXPECT_EQ_INT(ev.button.x, 4);
    RB_EXPECT_EQ_INT(next_event(&common).type, RB_EVENT_WINDOW_FOCUS_LOST);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
    RB_EXPECT(!common.input.keys_down[RB_KEY_LEFT_ALT]);
    RB_EXPECT(!common.input.buttons_down[RB_MOUSE_BUTTON_LEFT]);
}

RB_TEST(input, release_input_with_nothing_held_sends_nothing) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    /* X11 keyboard grab: release everything, no focus event. */
    rb_platform_common_release_input(&common, id);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
}

RB_TEST(input, destroying_the_focused_window_clears_input_silently) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 id = open_focused_window(&common);

    rb_platform_common_key(&common, id, RB_KEY_W, true);
    rb_platform_common_mouse_button(&common, id, RB_MOUSE_BUTTON_LEFT, true, at(1, 2));
    count_events(&common);

    rb_platform_common_window_closed(&common, id);
    RB_EXPECT_EQ_UINT(count_events(&common), 0);
    RB_EXPECT(!common.input.keys_down[RB_KEY_W]);
    RB_EXPECT(!common.input.buttons_down[RB_MOUSE_BUTTON_LEFT]);
    RB_EXPECT_EQ_UINT(common.input.mouse_window, 0);
}

RB_TEST(input, destroying_another_window_keeps_input) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32 focused = open_focused_window(&common);
    u32 other = open_window(&common);

    rb_platform_common_key(&common, focused, RB_KEY_W, true);
    rb_platform_common_mouse_moved(&common, focused, at(1, 2));
    rb_platform_common_window_closed(&common, other);

    RB_EXPECT(common.input.keys_down[RB_KEY_W]);
    RB_EXPECT_EQ_UINT(common.input.mouse_window, focused);
}

/* ============================================================================
 * Names
 * ========================================================================== */

RB_TEST(input, key_and_button_names) {
    RB_EXPECT_EQ_STR(rb_key_name(RB_KEY_W), "W");
    RB_EXPECT_EQ_STR(rb_key_name(RB_KEY_LEFT_SHIFT), "Left Shift");
    RB_EXPECT_EQ_STR(rb_key_name(RB_KEY_UNKNOWN), "Unknown");
    RB_EXPECT_EQ_STR(rb_key_name((rb_key)0x32), "Unknown"); /* HID NON_US_HASH: not in the enum */
    RB_EXPECT_EQ_STR(rb_key_name((rb_key)RB_KEY_COUNT), "Unknown");
    RB_EXPECT_EQ_STR(rb_key_name((rb_key)-1), "Unknown");
    RB_EXPECT_EQ_STR(rb_mouse_button_name(RB_MOUSE_BUTTON_X2), "X2");
    RB_EXPECT_EQ_STR(rb_mouse_button_name(RB_MOUSE_BUTTON_COUNT), "Unknown");
}
