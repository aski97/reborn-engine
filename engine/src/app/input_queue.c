/* Injected input waiting for its step (ADR 0023). */
#include "app/input_queue.h"

#include <reborn/assert.h>

bool rb_input_queue_has_room(const rb_input_queue *queue, u32 count) {
    RB_ASSERT(queue != NULL);
    return count <= RB_INPUT_QUEUE_CAPACITY - queue->count;
}

void rb_input_queue_push(rb_input_queue *queue, const rb_injected_input *input) {
    RB_ASSERT(queue != NULL && input != NULL);
    RB_ASSERT(queue->count < RB_INPUT_QUEUE_CAPACITY);
    queue->items[queue->count++] = *input;
}

static void input_queue_apply_one(const rb_injected_input *input, rb_platform_common *common,
                                  u32 id) {
    switch (input->type) {
    case RB_INJECTED_KEY:
        rb_platform_common_inject_key(common, id, input->key, input->down);
        break;
    case RB_INJECTED_BUTTON: {
        /* Read when applied, not when queued: an earlier entry may have moved the mouse. */
        rb_mouse_position position =
            input->has_position ? input->position : common->input.mouse_position;
        rb_platform_common_inject_mouse_button(common, id, input->button, input->down, position);
        break;
    }
    case RB_INJECTED_MOVE:
        rb_platform_common_inject_mouse_moved(common, id, input->position);
        break;
    case RB_INJECTED_WHEEL:
        rb_platform_common_inject_mouse_wheel(common, id, input->wheel);
        break;
    }
}

void rb_input_queue_apply(rb_input_queue *queue, rb_platform_common *common, u32 id, u64 step) {
    RB_ASSERT(queue != NULL && common != NULL);
    u32 kept = 0;
    for (u32 i = 0; i < queue->count; ++i) {
        if (queue->items[i].step <= step) {
            input_queue_apply_one(&queue->items[i], common, id);
        } else {
            queue->items[kept++] = queue->items[i];
        }
    }
    queue->count = kept;
}
