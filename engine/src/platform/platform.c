/*
 * OS-independent part of the platform layer: event queue and window handle
 * validation. Must not include any OS header (ADR 0006).
 */
#include "platform/platform_internal.h"

#include <reborn/assert.h>
#include <reborn/log.h>

/* ============================================================================
 * Shared state
 * ========================================================================== */

bool rb_platform_common_init(rb_platform_common *common) {
    if (!rb_handle_pool_init(&common->window_pool, common->window_generations,
                             common->window_free_slots, RB_MAX_WINDOWS)) {
        return false;
    }
    rb_ring_init(&common->event_ring, RB_EVENT_QUEUE_CAPACITY);
    common->dropped_events = 0;
    common->dropping_events = false;
    return true;
}

/* ============================================================================
 * Events
 * ========================================================================== */

void rb_platform_common_push_event(rb_platform_common *common, const rb_event *event) {
    u32 index;
    if (!rb_ring_push_index(&common->event_ring, &index)) {
        /*
         * Dropping the newest keeps the order of what is already queued.
         * Any drop can leave a key stuck: revisit if this is ever non-zero.
         */
        ++common->dropped_events;
        if (!common->dropping_events) {
            common->dropping_events = true;
            RB_LOG_WARN("Event queue full (%u slots): dropping events, %u lost so far",
                        RB_EVENT_QUEUE_CAPACITY, common->dropped_events);
        }
        return;
    }
    common->dropping_events = false;
    common->events[index] = *event;
}

bool rb_platform_common_poll_event(rb_platform_common *common, rb_event *out) {
    u32 index;
    if (!rb_ring_pop_index(&common->event_ring, &index)) {
        return false;
    }
    *out = common->events[index];
    return true;
}

/*
 * Public entry points assert their required pointers: a NULL here is a bug in
 * the caller, and the assert names it instead of a crash deeper inside.
 */
bool rb_platform_poll_event(rb_platform_state *state, rb_event *out) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(out != NULL);
    return rb_platform_common_poll_event(rb_platform_common_of(state), out);
}

/* ============================================================================
 * Windows
 * ========================================================================== */

bool rb_window_is_valid(const rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    return rb_handle_pool_valid(&rb_platform_common_of_const(state)->window_pool, window.id);
}
