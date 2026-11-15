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
 * Pointer parameters must not be NULL unless documented (RB_ASSERT in debug builds).
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

/*
 * Client area size. Logical units are what rb_window_desc uses; pixels are what
 * the renderer draws to (equal on Linux, scaled by the monitor's DPI on Windows).
 * While the window is minimized the size keeps its last value, it never drops to
 * zero. Windows can still report 0 pixels for a window shrunk to its title bar.
 */
typedef struct rb_window_size {
    u32 width; /* logical units */
    u32 height;
    u32 pixel_width;
    u32 pixel_height;
} rb_window_size;

/*
 * State as of the last rb_platform_pump, which may be newer than the event being
 * handled. Stale or zero handles give a zero size and false.
 */
RB_API rb_window_size rb_window_get_size(const rb_platform_state *state, rb_window window);
RB_API bool           rb_window_is_focused(const rb_platform_state *state, rb_window window);
RB_API bool           rb_window_is_minimized(const rb_platform_state *state, rb_window window);

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
    /*
     * Logical or pixel size changed (`resize`). Consecutive resizes of one window
     * are merged into the newest, so a drag does not flood the queue. A DPI change
     * alters the pixels only. Not sent on minimize: the size is kept.
     */
    RB_EVENT_WINDOW_RESIZED,
    /* Keyboard focus. */
    RB_EVENT_WINDOW_FOCUS_GAINED,
    RB_EVENT_WINDOW_FOCUS_LOST,
    /* Restored means "no longer minimized"; maximizing is just a resize. */
    RB_EVENT_WINDOW_MINIMIZED,
    RB_EVENT_WINDOW_RESTORED,
} rb_event_type;

/*
 * Every event names its window: the OS delivers one stream for all windows
 * (per thread on Win32, per connection on X11). Only the union member named
 * after `type` is meaningful; types without one carry no payload.
 */
typedef struct rb_event {
    rb_event_type type;
    rb_window     window;
    union {
        rb_window_size resize; /* RB_EVENT_WINDOW_RESIZED: the new size */
    };
} rb_event;

/*
 * A new window starts with its created size, not focused, not minimized; events
 * report real changes from there (the OS's redundant messages are filtered out).
 * Focus usually arrives right after creation as RB_EVENT_WINDOW_FOCUS_GAINED.
 */

/* Reads pending OS messages into the event queue and updates window state. Never blocks. */
RB_API void rb_platform_pump(rb_platform_state *state);

/* Pops the oldest queued event into *out. False when the queue is empty. */
RB_API bool rb_platform_poll_event(rb_platform_state *state, rb_event *out);

#endif /* RB_PLATFORM_H */
