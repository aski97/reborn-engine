/*
 * Platform layer: windows and OS events (ADR 0006). A game runs under the
 * application layer (reborn/app.h), which owns the platform and the main
 * window and pumps them; the game reaches them through rb_app_platform. Driving
 * the platform directly, as below, is for tools and tests.
 *
 *   rb_platform_state *platform = rb_platform_startup(rb_arena_allocator(&engine_arena));
 *   rb_window win = rb_window_create(
 *       platform, &(rb_window_desc){.title = "Reborn", .width = 1280, .height = 720});
 *
 *   for (bool running = true; running;) {
 *       rb_platform_pump(platform);
 *       rb_event ev;
 *       while (rb_platform_poll_event(platform, &ev)) {
 *           if (ev.type == RB_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
 *       }
 *       if (rb_key_is_down(platform, RB_KEY_W)) { ... }
 *   }
 *
 *   rb_window_destroy(platform, win);
 *   rb_platform_shutdown(platform);   // frees the state through the same allocator
 *
 * No OS header may ever be included here: this file reaches game code.
 * Pointer parameters must not be NULL unless documented (RB_ASSERT in debug builds).
 */
#ifndef RB_PLATFORM_H
#define RB_PLATFORM_H

#include <reborn/defines.h>
#include <reborn/image.h>
#include <reborn/input.h>
#include <reborn/memory.h>

/* ============================================================================
 * System
 * ========================================================================== */

/* Opaque: its fields differ per backend and only the backend knows them. */
typedef struct rb_platform_state rb_platform_state;

/*
 * Takes its state from `allocator` and keeps a copy of it, so the state behind
 * the allocator (an arena) must outlive the platform (ADR 0010, K1). NULL on
 * failure (logged), with nothing left allocated: no allocator memory, no X
 * display, no window class.
 */
RB_API rb_platform_state *rb_platform_startup(rb_allocator allocator);

/* Destroys any window still open, then frees `state` through the startup allocator. */
RB_API void rb_platform_shutdown(rb_platform_state *state);

/* ============================================================================
 * Windows
 * ========================================================================== */

#define RB_MAX_WINDOWS 8

/* Generational handle (ADR 0007). A zero-initialized rb_window is never valid. */
typedef struct rb_window {
    u32 id;
} rb_window;

/* Limits of an icon's images (ADR 0020): 256 is the largest Windows icon. */
#define RB_WINDOW_ICON_MAX_SIDE  256u
#define RB_WINDOW_ICON_MAX_COUNT 8u

typedef struct rb_window_desc {
    const char *title; /* UTF-8 */
    u32         width; /* client area, without borders and title bar */
    u32         height;
    /*
     * The window's icon (ADR 0020): one icon at several sizes (square, as the
     * systems show them), in any order; the system shows the closest one (title bar 16+, Alt+Tab
     * and taskbar 32+). NULL / 0: the system's icon. Read by rb_window_create only, which sets it
     * before the window appears. Images of a zero size or larger than RB_WINDOW_ICON_MAX_SIDE, or
     * more than RB_WINDOW_ICON_MAX_COUNT of them, are logged and ignored: the window still opens,
     * with the system's icon.
     */
    const rb_image *icons;
    u32             icon_count;
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
 * Top-left corner of the client area on the virtual screen (every monitor on
 * one plane, origin at the primary monitor's top left), in pixels: negative on
 * a monitor left of or above the primary. Kept while the window is minimized.
 */
typedef struct rb_window_position {
    i32 x;
    i32 y;
} rb_window_position;

/*
 * State as of the last rb_platform_pump, which may be newer than the event being
 * handled. Stale or zero handles give a zero size and position, and false.
 */
RB_API rb_window_size     rb_window_get_size(const rb_platform_state *state, rb_window window);
RB_API rb_window_position rb_window_get_position(const rb_platform_state *state, rb_window window);
RB_API bool               rb_window_is_focused(const rb_platform_state *state, rb_window window);
RB_API bool               rb_window_is_minimized(const rb_platform_state *state, rb_window window);

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
    /*
     * The client area moved on the screen (`position`). Consecutive moves of one
     * window are merged into the newest. Windows delivers a drag's moves when it
     * ends (rb_platform_pump does not return during the drag).
     */
    RB_EVENT_WINDOW_MOVED,
    /* Keyboard focus. */
    RB_EVENT_WINDOW_FOCUS_GAINED,
    RB_EVENT_WINDOW_FOCUS_LOST,
    /* Restored means "no longer minimized"; maximizing is just a resize. */
    RB_EVENT_WINDOW_MINIMIZED,
    RB_EVENT_WINDOW_RESTORED,
    /*
     * A key went down or up (`key`). Held keys are re-sent as KEY_PRESSED with
     * `repeat` set. When the window loses focus, every held key and button gets
     * a release before RB_EVENT_WINDOW_FOCUS_LOST: nothing stays stuck.
     */
    RB_EVENT_KEY_PRESSED,
    RB_EVENT_KEY_RELEASED,
    /* `button`. While a button is held the window keeps the mouse, even outside it. */
    RB_EVENT_MOUSE_BUTTON_PRESSED,
    RB_EVENT_MOUSE_BUTTON_RELEASED,
    /* `move`. Consecutive moves of one window are merged into the newest. */
    RB_EVENT_MOUSE_MOVED,
    /* `wheel`. Consecutive wheel events of one window are summed. */
    RB_EVENT_MOUSE_WHEEL,
} rb_event_type;

typedef struct rb_key_event {
    rb_key key;
    u32    mods;   /* rb_key_mod bits after this event: a Shift press includes SHIFT */
    bool   repeat; /* auto-repeat of a held key: games usually skip these */
} rb_key_event;

/*
 * Client area pixels (what the renderer draws, not logical units), origin top
 * left. Negative or beyond the size while a held button keeps the mouse
 * outside the window.
 */
typedef struct rb_mouse_position {
    i32 x;
    i32 y;
} rb_mouse_position;

typedef struct rb_mouse_button_event {
    rb_mouse_button button;
    i32             x; /* where it happened, as rb_mouse_position */
    i32             y;
    u32             mods; /* rb_key_mod bits */
} rb_mouse_button_event;

/* In notches: one click of a classic wheel is 1; high-resolution wheels give fractions. */
typedef struct rb_mouse_wheel {
    f32 x; /* positive: right */
    f32 y; /* positive: up, away from the user */
} rb_mouse_wheel;

/*
 * Every event names its window: the OS delivers one stream for all windows
 * (per thread on Win32, per connection on X11). Only the union member named
 * after `type` is meaningful; types without one carry no payload.
 */
typedef struct rb_event {
    rb_event_type type;
    rb_window     window;
    union {
        rb_window_size        resize;   /* RB_EVENT_WINDOW_RESIZED: the new size */
        rb_window_position    position; /* RB_EVENT_WINDOW_MOVED: the new position */
        rb_key_event          key;      /* RB_EVENT_KEY_* */
        rb_mouse_button_event button;   /* RB_EVENT_MOUSE_BUTTON_* */
        rb_mouse_position     move;     /* RB_EVENT_MOUSE_MOVED: the new position */
        rb_mouse_wheel        wheel;    /* RB_EVENT_MOUSE_WHEEL */
    };
} rb_event;

/*
 * A new window starts with its created size and position, not focused, not
 * minimized (Linux: the requested position; the window manager's placement
 * follows as RB_EVENT_WINDOW_MOVED); events
 * report real changes from there (the OS's redundant messages are filtered out).
 * Focus usually arrives right after creation as RB_EVENT_WINDOW_FOCUS_GAINED.
 */

/* Reads pending OS messages into the event queue and updates window state. Never blocks. */
RB_API void rb_platform_pump(rb_platform_state *state);

/* Pops the oldest queued event into *out. False when the queue is empty. */
RB_API bool rb_platform_poll_event(rb_platform_state *state, rb_event *out);

/* ============================================================================
 * Input state
 *
 * Derived from the events, as of the last rb_platform_pump. Under the
 * application layer the pump runs once per update step (ADR 0011, D1), so the
 * edges cover exactly the time since the previous step. One keyboard and one
 * mouse for all windows. `was_pressed` and `was_released` count transitions
 * during the last pump, so a key pressed and released within it reports both
 * while `is_down` is already false. Repeats are not presses. Values outside the
 * enums give false.
 * ========================================================================== */

RB_API bool rb_key_is_down(const rb_platform_state *state, rb_key key);
RB_API bool rb_key_was_pressed(const rb_platform_state *state, rb_key key);
RB_API bool rb_key_was_released(const rb_platform_state *state, rb_key key);
/* rb_key_mod bits of the modifier keys down now. */
RB_API u32 rb_key_mods(const rb_platform_state *state);

RB_API bool rb_mouse_button_is_down(const rb_platform_state *state, rb_mouse_button button);
RB_API bool rb_mouse_button_was_pressed(const rb_platform_state *state, rb_mouse_button button);
RB_API bool rb_mouse_button_was_released(const rb_platform_state *state, rb_mouse_button button);

/*
 * Window of the last mouse event, and the position inside it. The mouse
 * leaving a window sends nothing: both keep their last value. A zero handle
 * and (0, 0) before the first mouse event or after that window is destroyed.
 */
RB_API rb_window         rb_mouse_get_window(const rb_platform_state *state);
RB_API rb_mouse_position rb_mouse_get_position(const rb_platform_state *state);
/* Sum of the wheel events during the last pump. */
RB_API rb_mouse_wheel rb_mouse_get_wheel(const rb_platform_state *state);

#endif /* RB_PLATFORM_H */
