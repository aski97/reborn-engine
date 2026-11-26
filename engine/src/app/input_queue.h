/*
 * Injected input waiting for its step (ADR 0023). Commands run before the
 * steps, and each step's pump first clears the edges: input applied by a
 * command would be gone before the update reads rb_key_was_pressed. So the
 * commands queue it, and the step applies it right after its pump.
 */
#ifndef RB_INPUT_QUEUE_H
#define RB_INPUT_QUEUE_H

#include "platform/platform_internal.h"

/* A few commands' worth per step: an agent steps between them (ADR 0023, D1). */
#define RB_INPUT_QUEUE_CAPACITY 64u

typedef enum rb_injected_type {
    RB_INJECTED_KEY,
    RB_INJECTED_BUTTON,
    RB_INJECTED_MOVE,
    RB_INJECTED_WHEEL,
} rb_injected_type;

typedef struct rb_injected_input {
    u64               step; /* applied by this step, as rb_app_step_index counts it once it ran */
    rb_injected_type  type;
    bool              down;         /* key, button */
    bool              has_position; /* button: false keeps the mouse where it is */
    rb_key            key;
    rb_mouse_button   button;
    rb_mouse_position position; /* button, move */
    rb_mouse_wheel    wheel;
} rb_injected_input;

/* Entries in the order they were queued; not a ring: entries due later stay behind. */
typedef struct rb_input_queue {
    rb_injected_input items[RB_INPUT_QUEUE_CAPACITY];
    u32               count;
} rb_input_queue;

/* Room for `count` more entries: a tap queues two at once, all or nothing. */
bool rb_input_queue_has_room(const rb_input_queue *queue, u32 count);

/* The caller checks the room first: pushing into a full queue is a bug. */
void rb_input_queue_push(rb_input_queue *queue, const rb_injected_input *input);

/*
 * Applies, in queue order, every entry due by `step` to the window `id`, and
 * keeps the others in order. Entries of earlier steps are applied too: none
 * is ever lost.
 */
void rb_input_queue_apply(rb_input_queue *queue, rb_platform_common *common, u32 id, u64 step);

#endif /* RB_INPUT_QUEUE_H */
