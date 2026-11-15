/*
 * State and helpers shared by every platform backend (ADR 0006).
 *
 * Each backend defines its own `struct rb_platform_state` and must embed
 * rb_platform_common as its FIRST member:
 *
 *   struct rb_platform_state {
 *       rb_platform_common common;   // must stay first
 *       HINSTANCE          instance;  // backend fields
 *   };
 *
 * C guarantees that a pointer to a struct, converted, points to its first
 * member: the OS-independent code in platform.c relies on it to reach the
 * shared part without knowing the backend's struct.
 */
#ifndef RB_PLATFORM_INTERNAL_H
#define RB_PLATFORM_INTERNAL_H

#include <reborn/platform.h>

#include "containers/handle_pool.h"
#include "containers/ring.h"

/* Sized so that it never fills in practice: a full queue means a stalled frame (ADR 0006). */
#define RB_EVENT_QUEUE_CAPACITY 256u

typedef struct rb_platform_common {
    rb_handle_pool window_pool;
    u16            window_generations[RB_MAX_WINDOWS];
    u32            window_free_slots[RB_MAX_WINDOWS];

    rb_ring  event_ring;
    rb_event events[RB_EVENT_QUEUE_CAPACITY];
    /* Events lost because the queue was full, since startup. */
    u32 dropped_events;
    /* The last push was dropped: a burst of drops logs one warning, not one per event. */
    bool dropping_events;
} rb_platform_common;

RB_INLINE rb_platform_common *rb_platform_common_of(rb_platform_state *state) {
    return (rb_platform_common *)(void *)state;
}

RB_INLINE const rb_platform_common *rb_platform_common_of_const(const rb_platform_state *state) {
    return (const rb_platform_common *)(const void *)state;
}

bool rb_platform_common_init(rb_platform_common *common);

/* Drops the event, counts it and warns once per burst when the queue is full. */
void rb_platform_common_push_event(rb_platform_common *common, const rb_event *event);

bool rb_platform_common_poll_event(rb_platform_common *common, rb_event *out);

#endif /* RB_PLATFORM_INTERNAL_H */
