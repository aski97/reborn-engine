/*
 * Platform layer: windows and OS events (ADR 0006).
 *
 *   rb_platform_state *platform = malloc(rb_platform_state_size());
 *   rb_platform_startup(platform);
 *   rb_window win = rb_window_create(platform, &(rb_window_desc){"Reborn", 1280, 720});
 *
 *   for (bool running = true; running;) {
 *       rb_platform_pump(platform);
 *       rb_event ev;
 *       while (rb_platform_poll_event(platform, &ev)) {
 *           if (ev.type == RB_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
 *       }
 *   }
 *
 *   rb_window_destroy(platform, win);
 *   rb_platform_shutdown(platform);
 *   free(platform);
 *
 * No OS header may ever be included here: this file reaches game code.
 */
#ifndef RB_PLATFORM_H
#define RB_PLATFORM_H

#include <reborn/defines.h>

/* ============================================================================
 * System
 * ========================================================================== */

/* Opaque: its fields differ per backend and only the backend knows them. */
typedef struct rb_platform_state rb_platform_state;

/* Bytes the caller must provide to rb_platform_startup (ADR 0006, B2). */
RB_API usize rb_platform_state_size(void);

/*
 * `state` must point to rb_platform_state_size() bytes aligned for any type
 * (malloc guarantees it). The memory stays owned by the caller.
 */
RB_API bool rb_platform_startup(rb_platform_state *state);

/* Destroys any window still open. Does not free `state`: the caller does. */
RB_API void rb_platform_shutdown(rb_platform_state *state);

/* ============================================================================
 * Windows
 * ========================================================================== */

#define RB_MAX_WINDOWS 8

/* Generational handle (ADR 0007). A zero-initialized rb_window is never valid. */
typedef struct rb_window {
    u32 id;
} rb_window;

typedef struct rb_window_desc {
    const char *title; /* UTF-8 */
    u32         width; /* client area, without borders and title bar */
    u32         height;
} rb_window_desc;

/* Returns a zero handle on failure (no free slot, OS error). The window is shown at once. */
RB_API rb_window rb_window_create(rb_platform_state *state, const rb_window_desc *desc);

/* Stale or zero handles are ignored. */
RB_API void rb_window_destroy(rb_platform_state *state, rb_window window);

RB_API bool rb_window_is_valid(const rb_platform_state *state, rb_window window);

/* ============================================================================
 * Events
 * ========================================================================== */

typedef enum rb_event_type {
    RB_EVENT_NONE = 0,
    /*
     * The user clicked X or pressed Alt+F4. The window is NOT destroyed: the
     * application decides (close it, ask to save, ignore).
     */
    RB_EVENT_WINDOW_CLOSE_REQUESTED,
} rb_event_type;

/*
 * Every event names its window: the OS delivers one stream for all windows
 * (per thread on Win32, per connection on X11). Payloads for resize, keys and
 * mouse will be a union selected by `type`.
 */
typedef struct rb_event {
    rb_event_type type;
    rb_window     window;
} rb_event;

/* Reads pending OS messages into the event queue. Never blocks. */
RB_API void rb_platform_pump(rb_platform_state *state);

/* Pops the oldest queued event into *out. False when the queue is empty. */
RB_API bool rb_platform_poll_event(rb_platform_state *state, rb_event *out);

#endif /* RB_PLATFORM_H */
