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

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

/* ============================================================================
 * State
 * ========================================================================== */

typedef struct xlib_window {
    Window xid; /* 0 (None) when the slot is free */
    u32    id;  /* handle of this slot, to tag events */
} xlib_window;

struct rb_platform_state {
    rb_platform_common common; /* must stay first: see platform_internal.h */
    Display           *display;
    /* Atoms are interned once: each XInternAtom is a round trip to the server. */
    Atom        wm_protocols;
    Atom        wm_delete_window;
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

usize rb_platform_state_size(void) {
    return sizeof(struct rb_platform_state);
}

bool rb_platform_startup(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    memset(state, 0, sizeof(*state));
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

    /* False: create the atom if no client has yet. */
    state->wm_protocols = XInternAtom(state->display, "WM_PROTOCOLS", False);
    state->wm_delete_window = XInternAtom(state->display, "WM_DELETE_WINDOW", False);
    state->net_wm_name = XInternAtom(state->display, "_NET_WM_NAME", False);
    state->utf8_string = XInternAtom(state->display, "UTF8_STRING", False);
    RB_LOG_DEBUG("Xlib platform started on display \"%s\"", DisplayString(state->display));
    return true;
}

void rb_platform_shutdown(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    /* Safe after a failed startup, as on Win32: the zeroed state has no display. */
    if (state->display == NULL) {
        return;
    }
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].xid != None) {
            rb_window_destroy(state, (rb_window){state->windows[i].id});
        }
    }
    /* Flushes pending requests; the server also frees anything we left behind. */
    XCloseDisplay(state->display);
    state->display = NULL;
    RB_LOG_DEBUG("Xlib platform shut down");
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
    Window        root = RootWindow(state->display, screen);
    unsigned long black = BlackPixel(state->display, screen);
    /* Border width 0 (the window manager draws its own); black until the renderer draws. */
    Window xid =
        XCreateSimpleWindow(state->display, root, 0, 0, desc->width, desc->height, 0, black, black);
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
    /*
     * XPending returns how many events are queued; when none are, it first
     * flushes our requests and reads what the server has sent, without
     * waiting. XNextEvent alone would block on an empty queue. ClientMessage
     * needs no XSelectInput: it is always delivered. Other events are ignored
     * until resize, focus and input.
     */
    while (XPending(state->display) > 0) {
        XEvent event;
        XNextEvent(state->display, &event);
        if (event.type == ClientMessage) {
            xlib_handle_client_message(state, &event.xclient);
        }
    }
}
