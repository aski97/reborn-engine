/* Blink's input layer (ADR 0014): platform events to the game's actions. */
#include "rb_test.h"

#include "blink_input.h"

#define EXPECT_F32_NEAR(a, b) RB_EXPECT_NEAR((f64)(a), (f64)(b), 1e-4)

/* The game's camera on a 1600x900 window: 100 pixels per unit, no bars. */
static rb_camera2d camera(void) {
    return rb_camera2d_make((rb_vec2){8.0f, 4.5f}, (rb_vec2){16.0f, 9.0f});
}

static const rb_vec2 target = {1600.0f, 900.0f};

static rb_event press(rb_mouse_button button, i32 x, i32 y) {
    return (rb_event){.type = RB_EVENT_MOUSE_BUTTON_PRESSED,
                      .button = {.button = button, .x = x, .y = y}};
}

static rb_event key(rb_event_type type, rb_key k, bool repeat) {
    return (rb_event){.type = type, .key = {.key = k, .repeat = repeat}};
}

RB_TEST(blink_input, left_press_is_a_click_in_world_units) {
    rb_camera2d  cam = camera();
    rb_event     ev = press(RB_MOUSE_BUTTON_LEFT, 850, 250);
    blink_action action;
    RB_EXPECT(blink_input_translate(&ev, &cam, target, &action));
    RB_EXPECT(action.type == BLINK_ACTION_CLICK);
    EXPECT_F32_NEAR(action.point.x, 8.5f);
    EXPECT_F32_NEAR(action.point.y, 2.5f);
}

RB_TEST(blink_input, other_buttons_are_ignored) {
    rb_camera2d  cam = camera();
    rb_event     ev = press(RB_MOUSE_BUTTON_RIGHT, 850, 250);
    blink_action action;
    RB_EXPECT(!blink_input_translate(&ev, &cam, target, &action));
}

RB_TEST(blink_input, space_press_is_a_start) {
    rb_camera2d  cam = camera();
    rb_event     ev = key(RB_EVENT_KEY_PRESSED, RB_KEY_SPACE, false);
    blink_action action;
    RB_EXPECT(blink_input_translate(&ev, &cam, target, &action));
    RB_EXPECT(action.type == BLINK_ACTION_START);
}

RB_TEST(blink_input, repeats_releases_and_other_keys_are_ignored) {
    rb_camera2d  cam = camera();
    blink_action action;
    rb_event     repeat = key(RB_EVENT_KEY_PRESSED, RB_KEY_SPACE, true);
    rb_event     release = key(RB_EVENT_KEY_RELEASED, RB_KEY_SPACE, false);
    rb_event     other = key(RB_EVENT_KEY_PRESSED, RB_KEY_R, false);
    rb_event     move = {.type = RB_EVENT_MOUSE_MOVED, .move = {10, 10}};
    RB_EXPECT(!blink_input_translate(&repeat, &cam, target, &action));
    RB_EXPECT(!blink_input_translate(&release, &cam, target, &action));
    RB_EXPECT(!blink_input_translate(&other, &cam, target, &action));
    RB_EXPECT(!blink_input_translate(&move, &cam, target, &action));
}
