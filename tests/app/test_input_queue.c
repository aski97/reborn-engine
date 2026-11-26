/* Injected input waiting for its step (ADR 0023). */
#include "rb_test.h"

#include "app/input_queue.h"

/* ============================================================================
 * Helpers
 * ========================================================================== */

/* A platform with one focused window and no events queued. */
static u32 open_window(rb_platform_common *common) {
    u32 id = rb_handle_pool_alloc(&common->window_pool);
    rb_platform_common_window_opened(common, id, (rb_window_size){640, 480, 640, 480},
                                     (rb_window_position){0, 0});
    rb_platform_common_window_focused(common, id, true);
    rb_event ev;
    while (rb_platform_common_poll_event(common, &ev)) {
    }
    return id;
}

static rb_injected_input key_at(u64 step, rb_key key, bool down) {
    return (rb_injected_input){.step = step, .type = RB_INJECTED_KEY, .key = key, .down = down};
}

static rb_event next_event(rb_platform_common *common) {
    rb_event ev = {0};
    rb_platform_common_poll_event(common, &ev);
    return ev;
}

/* ============================================================================
 * Tests
 * ========================================================================== */

RB_TEST(input_queue, room_counts_what_is_left) {
    rb_input_queue queue = {0};
    RB_EXPECT(rb_input_queue_has_room(&queue, RB_INPUT_QUEUE_CAPACITY));
    RB_EXPECT(!rb_input_queue_has_room(&queue, RB_INPUT_QUEUE_CAPACITY + 1));
    for (u32 i = 0; i < RB_INPUT_QUEUE_CAPACITY - 1; ++i) {
        rb_injected_input input = key_at(1, RB_KEY_A, true);
        rb_input_queue_push(&queue, &input);
    }
    RB_EXPECT(rb_input_queue_has_room(&queue, 1));
    RB_EXPECT(!rb_input_queue_has_room(&queue, 2));
}

/* A tap: the press in its step, the release kept for the next one. */
RB_TEST(input_queue, applies_only_what_is_due_and_keeps_the_rest) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32               id = open_window(&common);
    rb_input_queue    queue = {0};
    rb_injected_input press = key_at(5, RB_KEY_SPACE, true);
    rb_injected_input release = key_at(6, RB_KEY_SPACE, false);
    rb_input_queue_push(&queue, &press);
    rb_input_queue_push(&queue, &release);

    rb_input_queue_apply(&queue, &common, id, 4);
    RB_EXPECT_EQ_UINT(queue.count, 2);
    RB_EXPECT(!common.input.keys_down[RB_KEY_SPACE]);

    rb_input_queue_apply(&queue, &common, id, 5);
    RB_EXPECT_EQ_UINT(queue.count, 1);
    RB_EXPECT(common.input.keys_pressed[RB_KEY_SPACE]);
    RB_EXPECT(common.input.keys_injected[RB_KEY_SPACE]);

    rb_platform_common_begin_pump(&common);
    rb_input_queue_apply(&queue, &common, id, 6);
    RB_EXPECT_EQ_UINT(queue.count, 0);
    RB_EXPECT(common.input.keys_released[RB_KEY_SPACE]);
    RB_EXPECT(!common.input.keys_down[RB_KEY_SPACE]);
}

RB_TEST(input_queue, applies_in_queue_order_and_never_drops_late_entries) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32               id = open_window(&common);
    rb_input_queue    queue = {0};
    rb_injected_input inputs[] = {
        key_at(3, RB_KEY_A, true),
        key_at(9, RB_KEY_C, true),
        key_at(2, RB_KEY_B, true),
    };
    for (u32 i = 0; i < RB_ARRAY_COUNT(inputs); ++i) {
        rb_input_queue_push(&queue, &inputs[i]);
    }

    /* Step 4: both entries due by then, even the one for step 2, in the order queued. */
    rb_input_queue_apply(&queue, &common, id, 4);
    RB_EXPECT_EQ_INT(next_event(&common).key.key, RB_KEY_A);
    RB_EXPECT_EQ_INT(next_event(&common).key.key, RB_KEY_B);
    RB_EXPECT_EQ_UINT(queue.count, 1);
    RB_EXPECT_EQ_INT(queue.items[0].key, RB_KEY_C);
}

RB_TEST(input_queue, a_button_without_a_point_presses_where_the_mouse_is) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32               id = open_window(&common);
    rb_input_queue    queue = {0};
    rb_injected_input move = {
        .step = 1, .type = RB_INJECTED_MOVE, .position = (rb_mouse_position){30, 40}};
    rb_injected_input press = {
        .step = 1, .type = RB_INJECTED_BUTTON, .button = RB_MOUSE_BUTTON_RIGHT, .down = true};
    rb_input_queue_push(&queue, &move);
    rb_input_queue_push(&queue, &press);

    rb_input_queue_apply(&queue, &common, id, 1);
    RB_EXPECT_EQ_INT(next_event(&common).type, RB_EVENT_MOUSE_MOVED);
    rb_event ev = next_event(&common);
    RB_EXPECT_EQ_INT(ev.type, RB_EVENT_MOUSE_BUTTON_PRESSED);
    RB_EXPECT_EQ_INT(ev.button.button, RB_MOUSE_BUTTON_RIGHT);
    RB_EXPECT_EQ_INT(ev.button.x, 30);
    RB_EXPECT_EQ_INT(ev.button.y, 40);
}

RB_TEST(input_queue, wheel_is_summed_for_the_step) {
    rb_platform_common common;
    RB_REQUIRE(rb_platform_common_init(&common));
    u32               id = open_window(&common);
    rb_input_queue    queue = {0};
    rb_injected_input wheel = {
        .step = 1, .type = RB_INJECTED_WHEEL, .wheel = (rb_mouse_wheel){0.5f, -1.0f}};
    rb_input_queue_push(&queue, &wheel);
    rb_input_queue_push(&queue, &wheel);

    rb_input_queue_apply(&queue, &common, id, 1);
    RB_EXPECT_NEAR((f64)common.input.wheel.x, 1.0, 0.0);
    RB_EXPECT_NEAR((f64)common.input.wheel.y, -2.0, 0.0);
}
