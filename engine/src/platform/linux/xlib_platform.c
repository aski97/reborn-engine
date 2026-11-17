/*
 * Xlib platform backend (ADR 0006).
 *
 * X11 is client/server: Xlib buffers our requests and sends them to the X
 * server, which answers with events on the same connection. rb_platform_pump
 * reads that connection and turns the events we care about into rb_event, so
 * engine code never runs inside an OS callback.
 *
 * Errors: Xlib reports protocol errors asynchronously, and its default
 * handlers print a message and terminate the process (exit), also when the
 * connection to the server is lost. No handlers are installed yet: how the
 * engine reacts to them is still to be decided.
 */
#include "platform/platform_internal.h"
#include "platform/linux/xlib_input.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h> /* XkbSetDetectableAutoRepeat: XKB ships inside libX11 */
#include <X11/Xatom.h>
#include <X11/Xutil.h> /* IconicState */

/* After Xlib: the Xlib surface types use Display, Window and VisualID. */
#include "platform/platform_vulkan.h"
#include <vulkan/vulkan_xlib.h>

/* ============================================================================
 * State
 * ========================================================================== */

typedef struct xlib_window {
    Window xid; /* 0 (None) when the slot is free */
    u32    id;  /* handle of this slot, to tag events */
    /* Something that can move the window arrived: translate its position after the events. */
    bool position_dirty;
} xlib_window;

struct rb_platform_state {
    rb_platform_common common; /* must stay first: see platform_internal.h */
    Display           *display;
    Window             root; /* positions are translated to it: the whole screen */
    /* Atoms are interned once: each XInternAtom is a round trip to the server. */
    Atom        wm_protocols;
    Atom        wm_delete_window;
    Atom        wm_state;
    Atom        net_wm_name;
    Atom        utf8_string;
    xlib_window windows[RB_MAX_WINDOWS];
};

_Static_assert(offsetof(struct rb_platform_state, common) == 0,
               "rb_platform_common must be the first member of rb_platform_state");

/*
 * The protocol carries sizes as 16-bit values and servers cap drawables at
 * 32767: a larger size would be truncated or rejected with an error, which the
 * default handler turns into exit.
 */
#define XLIB_SIZE_MAX 32767u

/*
 * Events each window subscribes to; XSelectInput replaces the whole mask, so it
 * is set once here. StructureNotify: ConfigureNotify (size). FocusChange:
 * FocusIn/FocusOut. PropertyChange: PropertyNotify, for WM_STATE (minimized).
 * KeyPress/KeyRelease, ButtonPress/ButtonRelease (the wheel too), and
 * PointerMotion: MotionNotify, with or without a button held.
 */
#define XLIB_EVENT_MASK                                                                            \
    (StructureNotifyMask | FocusChangeMask | PropertyChangeMask | KeyPressMask | KeyReleaseMask |  \
     ButtonPressMask | ButtonReleaseMask | PointerMotionMask)

/* X11 core protocol mouse buttons: 4-7 are the wheel, 8-9 the side buttons. */
#define XLIB_BUTTON_WHEEL_UP    4
#define XLIB_BUTTON_WHEEL_DOWN  5
#define XLIB_BUTTON_WHEEL_LEFT  6
#define XLIB_BUTTON_WHEEL_RIGHT 7
#define XLIB_BUTTON_BACK        8
#define XLIB_BUTTON_FORWARD     9

/* ============================================================================
 * Helpers
 * ========================================================================== */

static xlib_window *xlib_find_window(rb_platform_state *state, Window xid) {
    if (xid == None) {
        return NULL; /* would match every free slot */
    }
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].xid == xid) {
            return &state->windows[i];
        }
    }
    return NULL;
}

/*
 * _NET_WM_NAME (EWMH) is what current window managers show, and its type
 * UTF8_STRING carries engine strings as they are. WM_NAME (ICCCM) is for older
 * window managers and tools: XStoreName would type it STRING, i.e. Latin-1, and
 * "città" would show as "cittÃ ". It gets UTF8_STRING too, which they widely
 * accept; a COMPOUND_TEXT conversion would depend on the process locale.
 */
static void xlib_set_title(rb_platform_state *state, Window xid, const char *title) {
    const unsigned char *bytes = (const unsigned char *)title;
    int                  length = (int)strlen(title);
    XChangeProperty(state->display, xid, state->net_wm_name, state->utf8_string, 8, PropModeReplace,
                    bytes, length);
    XChangeProperty(state->display, xid, XA_WM_NAME, state->utf8_string, 8, PropModeReplace, bytes,
                    length);
}

static rb_window_size xlib_window_size(u32 width, u32 height) {
    /* No per-monitor scale in the core protocol: logical units are pixels (ADR 0006). */
    return (rb_window_size){width, height, width, height};
}

static void xlib_handle_configure(rb_platform_state *state, const XConfigureEvent *ev) {
    xlib_window *window = xlib_find_window(state, ev->window);
    if (window == NULL) {
        return;
    }
    /*
     * Also sent for moves and restacking: the common state drops those, since
     * the size is unchanged. Its x/y are relative to the parent, which is the
     * window manager's frame, not the screen: the position is translated later.
     */
    rb_platform_common_window_resized(&state->common, window->id,
                                      xlib_window_size((u32)ev->width, (u32)ev->height));
    window->position_dirty = true;
}

/*
 * The window manager moves its frame, not our window: our window only gets a
 * synthetic ConfigureNotify (ICCCM), whose coordinates are as right as the
 * window manager makes them. Asking the server is always right; doing it once
 * per window per pump keeps a drag's many events to one round trip (ADR 0006, X1).
 */
static void xlib_update_positions(rb_platform_state *state) {
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        xlib_window *window = &state->windows[i];
        if (window->xid == None || !window->position_dirty) {
            continue;
        }
        window->position_dirty = false;
        int    x;
        int    y;
        Window child; /* required output, unused */
        /* False only when the windows are on different screens, never for the root. */
        if (XTranslateCoordinates(state->display, window->xid, state->root, 0, 0, &x, &y, &child)) {
            rb_platform_common_window_moved(&state->common, window->id, (rb_window_position){x, y});
        }
    }
}

static void xlib_handle_focus(rb_platform_state *state, const XFocusChangeEvent *ev) {
    const xlib_window *window = xlib_find_window(state, ev->window);
    if (window == NULL) {
        return;
    }
    /*
     * A keyboard grab (the window manager during Alt+Tab or a drag) moves the
     * focus away and back without the user changing window: those come with mode
     * NotifyGrab / NotifyUngrab and are not focus changes. While the grab lasts,
     * key releases go to the grabbing client, so held keys are released now or
     * they would stay stuck (ADR 0006, T1).
     */
    if (ev->mode == NotifyGrab || ev->mode == NotifyUngrab) {
        if (ev->type == FocusOut && ev->mode == NotifyGrab) {
            rb_platform_common_release_input(&state->common, window->id);
        }
        return;
    }
    rb_platform_common_window_focused(&state->common, window->id, ev->type == FocusIn);
}

static void xlib_handle_key(rb_platform_state *state, const XKeyEvent *ev) {
    const xlib_window *window = xlib_find_window(state, ev->window);
    if (window == NULL) {
        return;
    }
    rb_key key = xlib_key_from_keycode(ev->keycode);
    if (key == RB_KEY_UNKNOWN) {
        RB_LOG_TRACE("Unmapped key: keycode %u", ev->keycode);
        return;
    }
    rb_platform_common_key(&state->common, window->id, key, ev->type == KeyPress);
}

/*
 * The core protocol reports the wheel as buttons 4-7, one press and release
 * per notch; only the press is used. Smooth scrolling needs XInput2.
 */
static void xlib_handle_button(rb_platform_state *state, const XButtonEvent *ev) {
    const xlib_window *window = xlib_find_window(state, ev->window);
    if (window == NULL) {
        return;
    }
    bool down = ev->type == ButtonPress;
    switch (ev->button) {
    case XLIB_BUTTON_WHEEL_UP:
    case XLIB_BUTTON_WHEEL_DOWN:
    case XLIB_BUTTON_WHEEL_LEFT:
    case XLIB_BUTTON_WHEEL_RIGHT:
        if (down) {
            rb_mouse_wheel wheel = {
                .x = ev->button == XLIB_BUTTON_WHEEL_RIGHT  ? 1.0f
                     : ev->button == XLIB_BUTTON_WHEEL_LEFT ? -1.0f
                                                            : 0.0f,
                .y = ev->button == XLIB_BUTTON_WHEEL_UP     ? 1.0f
                     : ev->button == XLIB_BUTTON_WHEEL_DOWN ? -1.0f
                                                            : 0.0f,
            };
            rb_platform_common_mouse_wheel(&state->common, window->id, wheel);
        }
        return;
    default:
        break;
    }

    rb_mouse_button button;
    switch (ev->button) {
    case Button1:
        button = RB_MOUSE_BUTTON_LEFT;
        break;
    case Button2:
        button = RB_MOUSE_BUTTON_MIDDLE;
        break;
    case Button3:
        button = RB_MOUSE_BUTTON_RIGHT;
        break;
    case XLIB_BUTTON_BACK:
        button = RB_MOUSE_BUTTON_X1;
        break;
    case XLIB_BUTTON_FORWARD:
        button = RB_MOUSE_BUTTON_X2;
        break;
    default:
        return; /* buttons 10+: extra buttons the enum does not name */
    }
    /*
     * No capture call needed: a button press makes the server grab the pointer
     * for this window until every button is up, so the release always arrives.
     */
    rb_platform_common_mouse_button(&state->common, window->id, button, down,
                                    (rb_mouse_position){ev->x, ev->y});
}

static void xlib_handle_motion(rb_platform_state *state, const XMotionEvent *ev) {
    const xlib_window *window = xlib_find_window(state, ev->window);
    if (window == NULL) {
        return;
    }
    rb_platform_common_mouse_moved(&state->common, window->id, (rb_mouse_position){ev->x, ev->y});
}

/*
 * X11 has no "minimized" event: the window manager writes the ICCCM WM_STATE
 * property on the client window, and IconicState means minimized. Unmap/Map is
 * not used: compositing window managers may keep minimized windows mapped.
 */
static void xlib_handle_property(rb_platform_state *state, const XPropertyEvent *ev) {
    if (ev->atom != state->wm_state || ev->state != PropertyNewValue) {
        return;
    }
    const xlib_window *window = xlib_find_window(state, ev->window);
    if (window == NULL) {
        return;
    }

    Atom           type;
    int            format;
    unsigned long  count;
    unsigned long  bytes_after;
    unsigned char *data = NULL;
    /* Offset 0, length 2 (in 32-bit units): state and icon window. A round trip. */
    int result = XGetWindowProperty(state->display, ev->window, state->wm_state, 0, 2, False,
                                    state->wm_state, &type, &format, &count, &bytes_after, &data);
    if (result == Success && type == state->wm_state && format == 32 && count >= 1) {
        /*
         * Format 32 data comes back as an array of C `long`, which is 64 bits on
         * 64-bit Linux, not as 32-bit values.
         */
        long wm_state = ((const long *)(const void *)data)[0];
        rb_platform_common_window_minimized(&state->common, window->id, wm_state == IconicState);
    }
    if (data != NULL) {
        XFree(data);
    }
}

static void xlib_handle_client_message(rb_platform_state *state, const XClientMessageEvent *msg) {
    /*
     * The window manager asks a client to close a window by sending it a
     * WM_PROTOCOLS message whose first value is WM_DELETE_WINDOW (only to
     * windows that registered it, see rb_window_create).
     */
    if (msg->message_type != state->wm_protocols || msg->format != 32 ||
        (Atom)msg->data.l[0] != state->wm_delete_window) {
        return;
    }
    const xlib_window *window = xlib_find_window(state, msg->window);
    if (window == NULL) {
        return; /* queued before rb_window_destroy: the window no longer exists for us */
    }
    RB_LOG_TRACE("WM_DELETE_WINDOW: close requested");
    rb_event event = {.type = RB_EVENT_WINDOW_CLOSE_REQUESTED, .window = {window->id}};
    rb_platform_common_push_event(&state->common, &event);
}

/* ============================================================================
 * System
 * ========================================================================== */

/* Everything but the state's memory. On failure nothing is left to undo but the free. */
static bool xlib_startup(rb_platform_state *state) {
    if (!rb_platform_common_init(&state->common)) {
        return false;
    }

    /*
     * NULL connects to the server named by $DISPLAY. Fails when there is none:
     * a text console, SSH without X forwarding, a container.
     */
    state->display = XOpenDisplay(NULL);
    if (state->display == NULL) {
        /* XDisplayName(NULL) is the name XOpenDisplay tried: $DISPLAY, or "" if unset. */
        RB_LOG_ERROR("Cannot open X display \"%s\": no X server reachable (DISPLAY unset?)",
                     XDisplayName(NULL));
        return false;
    }
    state->root = DefaultRootWindow(state->display);

    /* False: create the atom if no client has yet. */
    state->wm_protocols = XInternAtom(state->display, "WM_PROTOCOLS", False);
    state->wm_delete_window = XInternAtom(state->display, "WM_DELETE_WINDOW", False);
    state->wm_state = XInternAtom(state->display, "WM_STATE", False);
    state->net_wm_name = XInternAtom(state->display, "_NET_WM_NAME", False);
    state->utf8_string = XInternAtom(state->display, "UTF8_STRING", False);

    /*
     * By default a held key repeats as a fake KeyRelease + KeyPress pair, which
     * would look like the user tapping it. Detectable auto-repeat sends only
     * KeyPress events, which the common code reports as repeats (ADR 0006, Q1).
     */
    Bool detectable = False;
    XkbSetDetectableAutoRepeat(state->display, True, &detectable);
    if (!detectable) {
        RB_LOG_WARN("X server lacks detectable auto-repeat: held keys look like repeated taps");
    }
    RB_LOG_DEBUG("Xlib platform started on display \"%s\"", DisplayString(state->display));
    return true;
}

rb_platform_state *rb_platform_startup(rb_allocator allocator) {
    RB_ASSERT(allocator.vtable != NULL);
    /* Zeroed (ADR 0010, H1): every window slot starts empty. Failure is logged by the allocator. */
    rb_platform_state *state = RB_ALLOC_TYPE(allocator, rb_platform_state, RB_MEMORY_TAG_PLATFORM);
    if (state == NULL) {
        return NULL;
    }
    state->common.allocator = allocator;
    if (!xlib_startup(state)) {
        RB_FREE_TYPE(allocator, state, rb_platform_state, RB_MEMORY_TAG_PLATFORM);
        return NULL;
    }
    return state;
}

void rb_platform_shutdown(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].xid != None) {
            rb_window_destroy(state, (rb_window){state->windows[i].id});
        }
    }
    /* Flushes pending requests; the server also frees anything we left behind. */
    XCloseDisplay(state->display);
    RB_LOG_DEBUG("Xlib platform shut down");
    /* Copied out first: the free ends the state's life, allocator field included. */
    rb_allocator allocator = state->common.allocator;
    RB_FREE_TYPE(allocator, state, rb_platform_state, RB_MEMORY_TAG_PLATFORM);
}

/* ============================================================================
 * Windows
 * ========================================================================== */

rb_window rb_window_create(rb_platform_state *state, const rb_window_desc *desc) {
    RB_ASSERT(state != NULL);
    RB_ASSERT(desc != NULL);
    rb_window invalid = {RB_HANDLE_INVALID};
    if (desc->width == 0 || desc->height == 0 || desc->width > XLIB_SIZE_MAX ||
        desc->height > XLIB_SIZE_MAX) {
        RB_LOG_ERROR("Window size must be 1..%u per side, got %ux%u", XLIB_SIZE_MAX, desc->width,
                     desc->height);
        return invalid;
    }

    u32 id = rb_handle_pool_alloc(&state->common.window_pool);
    if (id == RB_HANDLE_INVALID) {
        RB_LOG_ERROR("No free window slot (at most %u windows)", RB_MAX_WINDOWS);
        return invalid;
    }

    /*
     * The core protocol has no per-monitor scale, so logical units are pixels
     * (ADR 0006). An X window is only the client area: the window manager draws
     * borders and title bar around it, so no adjustment is needed. The position
     * is a hint the window manager usually overrides.
     * Requests are asynchronous: Xlib picks the id before sending the request,
     * so a server-side failure arrives later as an error (see the top of the
     * file). None here only means Xlib ran out of ids.
     */
    int           screen = DefaultScreen(state->display);
    unsigned long black = BlackPixel(state->display, screen);
    /* Border width 0 (the window manager draws its own); black until the renderer draws. */
    Window xid = XCreateSimpleWindow(state->display, state->root, 0, 0, desc->width, desc->height,
                                     0, black, black);
    if (xid == None) {
        RB_LOG_ERROR("XCreateSimpleWindow failed: Xlib ran out of resource ids");
        rb_handle_pool_free(&state->common.window_pool, id);
        return invalid;
    }

    /*
     * Without WM_DELETE_WINDOW the window manager closes a window by killing
     * the client's connection, and Xlib's I/O error handler exits the process.
     * With it, the close button becomes a ClientMessage: a request, as on Win32.
     */
    Atom protocols[] = {state->wm_delete_window};
    if (!XSetWMProtocols(state->display, xid, protocols, (int)RB_ARRAY_COUNT(protocols))) {
        RB_LOG_ERROR("XSetWMProtocols failed: the close button could not be intercepted");
        XDestroyWindow(state->display, xid);
        rb_handle_pool_free(&state->common.window_pool, id);
        return invalid;
    }
    xlib_set_title(state, xid, desc->title != NULL ? desc->title : "");
    XSelectInput(state->display, xid, XLIB_EVENT_MASK);

    /*
     * The requested size and position: if the window manager picks others (it
     * places every window, a tiling one also sizes it), its ConfigureNotify
     * arrives as RB_EVENT_WINDOW_RESIZED / RB_EVENT_WINDOW_MOVED.
     */
    rb_platform_common_window_opened(&state->common, id,
                                     xlib_window_size(desc->width, desc->height),
                                     (rb_window_position){0, 0});
    xlib_window *slot = &state->windows[rb_handle_index(id)];
    slot->xid = xid;
    slot->id = id;

    /*
     * Properties are set before mapping, so the window manager finds them when
     * it takes the window. Xlib buffers requests: without the flush the window
     * would appear only at the next call that sends them.
     */
    XMapWindow(state->display, xid);
    XFlush(state->display);
    RB_LOG_INFO("Window 0x%08x created: %ux%u pixels", id, desc->width, desc->height);
    return (rb_window){id};
}

void rb_window_destroy(rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    if (!rb_handle_pool_valid(&state->common.window_pool, window.id)) {
        return;
    }
    xlib_window *slot = &state->windows[rb_handle_index(window.id)];
    rb_platform_common_window_closed(&state->common, window.id);
    XDestroyWindow(state->display, slot->xid);
    /* Without it the window would stay on screen until the next pump. */
    XFlush(state->display);
    *slot = (xlib_window){0};
    rb_handle_pool_free(&state->common.window_pool, window.id);
    RB_LOG_DEBUG("Window 0x%08x destroyed", window.id);
}

/* ============================================================================
 * Events
 * ========================================================================== */

void rb_platform_pump(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    rb_platform_common_begin_pump(&state->common);
    /*
     * XPending returns how many events are queued; when none are, it first
     * flushes our requests and reads what the server has sent, without
     * waiting. XNextEvent alone would block on an empty queue. ClientMessage
     * needs no XSelectInput: it is always delivered; the others come from
     * XLIB_EVENT_MASK. Events for windows already destroyed find no slot and
     * are dropped by the handlers.
     */
    while (XPending(state->display) > 0) {
        XEvent event;
        XNextEvent(state->display, &event);
        switch (event.type) {
        case ClientMessage:
            xlib_handle_client_message(state, &event.xclient);
            break;
        case ConfigureNotify:
            xlib_handle_configure(state, &event.xconfigure);
            break;
        case ReparentNotify: {
            /* Put into (or out of) a frame: the position on the screen changes. */
            xlib_window *window = xlib_find_window(state, event.xreparent.window);
            if (window != NULL) {
                window->position_dirty = true;
            }
            break;
        }
        case FocusIn:
        case FocusOut:
            xlib_handle_focus(state, &event.xfocus);
            break;
        case PropertyNotify:
            xlib_handle_property(state, &event.xproperty);
            break;
        case KeyPress:
        case KeyRelease:
            xlib_handle_key(state, &event.xkey);
            break;
        case ButtonPress:
        case ButtonRelease:
            xlib_handle_button(state, &event.xbutton);
            break;
        case MotionNotify:
            xlib_handle_motion(state, &event.xmotion);
            break;
        default:
            break;
        }
    }
    xlib_update_positions(state);
}

/* ============================================================================
 * Vulkan surface (ADR 0013, J1)
 * ========================================================================== */

const char *rb_platform_vulkan_surface_extension(void) {
    return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
}

bool rb_platform_vulkan_create_surface(const rb_platform_state *state, rb_window window,
                                       VkInstance                instance,
                                       PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                       VkSurfaceKHR             *surface) {
    RB_ASSERT(state != NULL && get_instance_proc_addr != NULL && surface != NULL);
    *surface = VK_NULL_HANDLE;
    if (!rb_handle_pool_valid(&state->common.window_pool, window.id)) {
        RB_LOG_ERROR("Vulkan surface: window 0x%08x does not exist", window.id);
        return false;
    }
    /* Present whenever the instance enabled VK_KHR_xlib_surface. */
    PFN_vkCreateXlibSurfaceKHR create =
        (PFN_vkCreateXlibSurfaceKHR)get_instance_proc_addr(instance, "vkCreateXlibSurfaceKHR");
    if (create == NULL) {
        RB_LOG_ERROR(
            "Vulkan instance lacks vkCreateXlibSurfaceKHR: was " VK_KHR_XLIB_SURFACE_EXTENSION_NAME
            " enabled?");
        return false;
    }
    /*
     * The driver talks to the X server through our Display connection, so the
     * connection must outlive the surface: the renderer stops before the platform.
     */
    VkXlibSurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = state->display,
        .window = state->windows[rb_handle_index(window.id)].xid,
    };
    VkResult result = create(instance, &info, NULL, surface);
    if (result != VK_SUCCESS) {
        RB_LOG_ERROR("vkCreateXlibSurfaceKHR failed: %s", rb_vk_result_name(result));
        *surface = VK_NULL_HANDLE;
        return false;
    }
    return true;
}
