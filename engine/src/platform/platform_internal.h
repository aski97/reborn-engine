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

/*
 * What the engine knows about a window, updated from OS messages. Not a cache
 * with expiry: the OS reports every change, so it is current as of the last pump.
 */
typedef struct rb_window_state {
    rb_window_size size;
    bool           focused;
    bool           minimized;
} rb_window_state;

typedef struct rb_platform_common {
    rb_handle_pool  window_pool;
    u16             window_generations[RB_MAX_WINDOWS];
    u32             window_free_slots[RB_MAX_WINDOWS];
    rb_window_state window_states[RB_MAX_WINDOWS]; /* indexed by rb_handle_index */

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

/* ============================================================================
 * Window state
 *
 * Backends report what the OS says through these; they update the state and
 * queue an event only on a real change, so redundant OS messages (WM_SIZE with
 * the same size, ConfigureNotify for a move) produce nothing. The state changes
 * even when the queue is full, so queries stay right if an event is dropped.
 * `id` must be a live window handle.
 * ========================================================================== */

/* Initial state of a new window: `size`, not focused, not minimized. No event. */
void rb_platform_common_window_opened(rb_platform_common *common, u32 id, rb_window_size size);

void rb_platform_common_window_resized(rb_platform_common *common, u32 id, rb_window_size size);
void rb_platform_common_window_focused(rb_platform_common *common, u32 id, bool focused);
void rb_platform_common_window_minimized(rb_platform_common *common, u32 id, bool minimized);

/* NULL for stale or zero handles. */
const rb_window_state *rb_platform_common_window_state(const rb_platform_common *common, u32 id);

#endif /* RB_PLATFORM_INTERNAL_H */
