/*
 * OS-independent part of the platform layer: event queue, window handle
 * validation and window state. Input state is in input.c. Must not include any
 * OS header (ADR 0006).
 */
#include "platform/platform_internal.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

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
    common->input = (rb_input_state){0};
    return true;
}

/* ============================================================================
 * Events
 * ========================================================================== */

/*
 * Merges `event` into the newest queued event when that has the same type and
 * window: a resize, a window move or a mouse move replaces it (a drag sends one
 * per mouse move and only the final value matters), a wheel event adds to it (summing
 * loses nothing). Only the newest is checked, so the order of different events
 * never changes.
 */
static bool rb_platform_common_merge_event(rb_platform_common *common, const rb_event *event) {
    if (event->type != RB_EVENT_WINDOW_RESIZED && event->type != RB_EVENT_WINDOW_MOVED &&
        event->type != RB_EVENT_MOUSE_MOVED && event->type != RB_EVENT_MOUSE_WHEEL) {
        return false;
    }
    u32 index;
    if (!rb_ring_newest_index(&common->event_ring, &index)) {
        return false;
    }
    rb_event *newest = &common->events[index];
    if (newest->type != event->type || newest->window.id != event->window.id) {
        return false;
    }
    if (event->type == RB_EVENT_MOUSE_WHEEL) {
        newest->wheel.x += event->wheel.x;
        newest->wheel.y += event->wheel.y;
    } else {
        *newest = *event;
    }
    return true;
}

void rb_platform_common_push_event(rb_platform_common *common, const rb_event *event) {
    if (rb_platform_common_merge_event(common, event)) {
        return;
    }
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

rb_window_size rb_window_get_size(const rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    const rb_window_state *ws =
        rb_platform_common_window_state(rb_platform_common_of_const(state), window.id);
    return ws != NULL ? ws->size : (rb_window_size){0};
}

rb_window_position rb_window_get_position(const rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    const rb_window_state *ws =
        rb_platform_common_window_state(rb_platform_common_of_const(state), window.id);
    return ws != NULL ? ws->position : (rb_window_position){0};
}

bool rb_window_is_focused(const rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    const rb_window_state *ws =
        rb_platform_common_window_state(rb_platform_common_of_const(state), window.id);
    return ws != NULL && ws->focused;
}

bool rb_window_is_minimized(const rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    const rb_window_state *ws =
        rb_platform_common_window_state(rb_platform_common_of_const(state), window.id);
    return ws != NULL && ws->minimized;
}

/* ============================================================================
 * Window state
 * ========================================================================== */

const rb_window_state *rb_platform_common_window_state(const rb_platform_common *common, u32 id) {
    if (!rb_handle_pool_valid(&common->window_pool, id)) {
        return NULL;
    }
    return &common->window_states[rb_handle_index(id)];
}

/*
 * Backends pass ids of their own live slots: a stale one here is a backend bug
 * that would write into the state of the window now using that slot.
 */
static rb_window_state *rb_platform_common_live_state(rb_platform_common *common, u32 id) {
    RB_ASSERT(rb_handle_pool_valid(&common->window_pool, id));
    return &common->window_states[rb_handle_index(id)];
}

static void rb_platform_common_push_window_event(rb_platform_common *common, u32 id,
                                                 rb_event_type type) {
    rb_event event = {.type = type, .window = {id}};
    rb_platform_common_push_event(common, &event);
}

bool rb_platform_common_icon_usable(const rb_window_desc *desc) {
    RB_ASSERT(desc->icons != NULL || desc->icon_count == 0);
    if (desc->icon_count == 0) {
        return false;
    }
    if (desc->icon_count > RB_WINDOW_ICON_MAX_COUNT) {
        RB_LOG_ERROR("Window icon: %u images, at most %u: the system's icon is used",
                     desc->icon_count, RB_WINDOW_ICON_MAX_COUNT);
        return false;
    }
    for (u32 i = 0; i < desc->icon_count; ++i) {
        const rb_image *image = &desc->icons[i];
        RB_ASSERT(image->pixels != NULL);
        if (image->width == 0 || image->height == 0 || image->width > RB_WINDOW_ICON_MAX_SIDE ||
            image->height > RB_WINDOW_ICON_MAX_SIDE) {
            RB_LOG_ERROR("Window icon: image %u is %ux%u, sides must be 1..%u: the system's icon "
                         "is used",
                         i, image->width, image->height, RB_WINDOW_ICON_MAX_SIDE);
            return false;
        }
    }
    return true;
}

void rb_platform_common_window_opened(rb_platform_common *common, u32 id, rb_window_size size,
                                      rb_window_position position) {
    *rb_platform_common_live_state(common, id) =
        (rb_window_state){.size = size, .position = position};
}

void rb_platform_common_window_closed(rb_platform_common *common, u32 id) {
    const rb_window_state *ws = rb_platform_common_live_state(common, id);
    rb_input_state        *input = &common->input;
    /* Only the down state: the edges still tell what happened during the last pump. */
    if (ws->focused) {
        memset(input->keys_down, 0, sizeof(input->keys_down));
        memset(input->keys_injected, 0, sizeof(input->keys_injected));
        memset(input->buttons_down, 0, sizeof(input->buttons_down));
        memset(input->buttons_injected, 0, sizeof(input->buttons_injected));
    }
    /* A held button keeps the mouse in its window even without focus. */
    if (input->mouse_window == id) {
        memset(input->buttons_down, 0, sizeof(input->buttons_down));
        memset(input->buttons_injected, 0, sizeof(input->buttons_injected));
        input->mouse_window = RB_HANDLE_INVALID;
        input->mouse_position = (rb_mouse_position){0};
    }
}

void rb_platform_common_window_resized(rb_platform_common *common, u32 id, rb_window_size size) {
    rb_window_state *ws = rb_platform_common_live_state(common, id);
    /* Field by field, not memcmp: memcmp would also compare padding bytes. */
    if (ws->size.width == size.width && ws->size.height == size.height &&
        ws->size.pixel_width == size.pixel_width && ws->size.pixel_height == size.pixel_height) {
        return;
    }
    ws->size = size;
    rb_event event = {.type = RB_EVENT_WINDOW_RESIZED, .window = {id}, .resize = size};
    rb_platform_common_push_event(common, &event);
}

void rb_platform_common_window_moved(rb_platform_common *common, u32 id,
                                     rb_window_position position) {
    rb_window_state *ws = rb_platform_common_live_state(common, id);
    if (ws->position.x == position.x && ws->position.y == position.y) {
        return;
    }
    ws->position = position;
    rb_event event = {.type = RB_EVENT_WINDOW_MOVED, .window = {id}, .position = position};
    rb_platform_common_push_event(common, &event);
}

void rb_platform_common_window_focused(rb_platform_common *common, u32 id, bool focused) {
    rb_window_state *ws = rb_platform_common_live_state(common, id);
    if (ws->focused == focused) {
        return;
    }
    ws->focused = focused;
    if (!focused) {
        /* Before FOCUS_LOST: the releases of held keys will reach another window. */
        rb_platform_common_release_input(common, id);
    }
    rb_platform_common_push_window_event(
        common, id, focused ? RB_EVENT_WINDOW_FOCUS_GAINED : RB_EVENT_WINDOW_FOCUS_LOST);
}

void rb_platform_common_window_minimized(rb_platform_common *common, u32 id, bool minimized) {
    rb_window_state *ws = rb_platform_common_live_state(common, id);
    if (ws->minimized == minimized) {
        return;
    }
    ws->minimized = minimized;
    rb_platform_common_push_window_event(
        common, id, minimized ? RB_EVENT_WINDOW_MINIMIZED : RB_EVENT_WINDOW_RESTORED);
}
