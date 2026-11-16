/*
 * Win32 platform backend (ADR 0006).
 *
 * Windows delivers messages by calling win32_window_proc from inside
 * DispatchMessageW; the procedure turns the ones we care about into rb_event
 * and queues them, so engine code never runs inside an OS callback.
 */
#include "platform/platform_internal.h"
#include "platform/win32/win32_error.h"
#include "platform/win32/win32_input.h"

#include <reborn/assert.h>
#include <reborn/log.h>

/* Trims rarely used APIs (sockets, cryptography...) out of <windows.h>: faster builds. */
#define WIN32_LEAN_AND_MEAN
/*
 * Makes generic names and macros (CreateWindowEx, IDC_ARROW...) resolve to the
 * UTF-16 -W versions, so an accidental -A call fails to compile (ADR 0006).
 */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h> /* GET_X_LPARAM / GET_Y_LPARAM */

/* ============================================================================
 * State
 * ========================================================================== */

typedef struct win32_window {
    HWND hwnd; /* NULL when the slot is free */
    u32  id;   /* handle of this slot, to tag events */
} win32_window;

struct rb_platform_state {
    rb_platform_common common; /* must stay first: see platform_internal.h */
    HINSTANCE          instance;
    win32_window       windows[RB_MAX_WINDOWS];
};

_Static_assert(offsetof(struct rb_platform_state, common) == 0,
               "rb_platform_common must be the first member of rb_platform_state");

static const wchar_t WIN32_CLASS_NAME[] = L"RebornWindow";

/* Longer titles are rejected rather than silently cut. */
#define WIN32_TITLE_MAX 256

/* ============================================================================
 * Helpers
 * ========================================================================== */

/* Engine strings are UTF-8, the -W API wants UTF-16 (ADR 0006). */
static bool win32_utf8_to_wide(const char *utf8, wchar_t *out, int out_count) {
    if (utf8 == NULL) {
        out[0] = L'\0';
        return true;
    }
    /*
     * -1 tells the API the input is NUL-terminated and makes it write the
     * terminator too. MB_ERR_INVALID_CHARS fails on malformed UTF-8 instead of
     * replacing it with U+FFFD. Returns 0 on any error, including a too small
     * buffer.
     */
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, out, out_count) != 0;
}

static win32_window *win32_find_window(rb_platform_state *state, HWND hwnd) {
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].hwnd == hwnd) {
            return &state->windows[i];
        }
    }
    return NULL;
}

/* Client area in pixels to rb_window_size; logical units at 96 DPI (ADR 0006). */
static rb_window_size win32_window_size(HWND hwnd, u32 pixel_width, u32 pixel_height) {
    int dpi = (int)GetDpiForWindow(hwnd);
    return (rb_window_size){
        .width = (u32)MulDiv((int)pixel_width, USER_DEFAULT_SCREEN_DPI, dpi),
        .height = (u32)MulDiv((int)pixel_height, USER_DEFAULT_SCREEN_DPI, dpi),
        .pixel_width = pixel_width,
        .pixel_height = pixel_height,
    };
}

static void win32_handle_size(rb_platform_state *state, u32 id, HWND hwnd, WPARAM kind,
                              LPARAM lparam) {
    switch (kind) {
    case SIZE_MINIMIZED:
        /* The client area is 0x0 now: the size is kept, so the renderer never sees zero. */
        rb_platform_common_window_minimized(&state->common, id, true);
        break;
    case SIZE_RESTORED:
    case SIZE_MAXIMIZED:
        rb_platform_common_window_minimized(&state->common, id, false);
        rb_platform_common_window_resized(&state->common, id,
                                          win32_window_size(hwnd, LOWORD(lparam), HIWORD(lparam)));
        break;
    default:
        break; /* SIZE_MAXSHOW / SIZE_MAXHIDE: sent to pop-ups only */
    }
}

/* ============================================================================
 * Keyboard
 * ========================================================================== */

static bool win32_is_key_message(UINT msg) {
    return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
}

/* lParam bits 16-23 (scancode) and 24 (extended), as win32_key_from_scancode wants them. */
static u32 win32_scancode(WPARAM wparam, LPARAM lparam) {
    u32 scancode = HIWORD(lparam) & (KF_EXTENDED | 0xFF);
    if (scancode == 0) {
        /*
         * Messages synthesized by other programs may carry no scancode: map the
         * virtual key back. The _EX variant returns extended keys as 0xE0xx.
         */
        UINT mapped = MapVirtualKeyW((UINT)wparam, MAPVK_VK_TO_VSC_EX);
        scancode = (mapped & 0xFF) | ((mapped & 0xFF00) == 0xE000 ? KF_EXTENDED : 0);
    }
    return scancode;
}

/*
 * On layouts with AltGr (Italian, German...) Windows sends Right Alt as a fake
 * Left Ctrl followed by Right Alt with the same timestamp. The fake Ctrl must
 * not reach the game: a real Left Ctrl is never followed by Right Alt with the
 * same time. Peeking does not remove the message.
 */
static bool win32_is_altgr_ctrl(void) {
    MSG  next;
    LONG time = GetMessageTime();
    if (!PeekMessageW(&next, NULL, 0, 0, PM_NOREMOVE) || !win32_is_key_message(next.message)) {
        return false;
    }
    return win32_key_from_scancode(win32_scancode(next.wParam, next.lParam)) == RB_KEY_RIGHT_ALT &&
           next.time == (DWORD)time;
}

static void win32_handle_key(rb_platform_state *state, u32 id, WPARAM wparam, LPARAM lparam) {
    u32    scancode = win32_scancode(wparam, lparam);
    rb_key key = win32_key_from_scancode(scancode);
    if (key == RB_KEY_UNKNOWN) {
        RB_LOG_TRACE("Unmapped key: scancode 0x%03x, virtual key 0x%02x", scancode,
                     (unsigned)wparam);
        return;
    }
    if (key == RB_KEY_LEFT_CTRL && win32_is_altgr_ctrl()) {
        return;
    }
    bool down = (HIWORD(lparam) & KF_UP) == 0;
    if (key == RB_KEY_PRINT_SCREEN && !down) {
        /* Windows keeps the press for its screenshot hotkey and sends only the release. */
        rb_platform_common_key(&state->common, id, key, true);
    }
    rb_platform_common_key(&state->common, id, key, down);
}

/*
 * With both Shift keys held, releasing one sends no key-up: only the second
 * release does, for the key released last. GetKeyState tells the truth as of
 * the last message read, so after the pump it can correct the state.
 */
static void win32_release_stuck_shifts(rb_platform_state *state) {
    HWND focus = GetFocus();
    if (focus == NULL) {
        return; /* win32_find_window(NULL) would match a free slot */
    }
    const win32_window *window = win32_find_window(state, focus);
    if (window == NULL) {
        return;
    }
    static const struct {
        rb_key key;
        int    vk;
    } shifts[] = {{RB_KEY_LEFT_SHIFT, VK_LSHIFT}, {RB_KEY_RIGHT_SHIFT, VK_RSHIFT}};
    for (u32 i = 0; i < RB_ARRAY_COUNT(shifts); ++i) {
        /* The high bit of GetKeyState's result is set while the key is down. */
        if (state->common.input.keys_down[shifts[i].key] &&
            (GetKeyState(shifts[i].vk) & 0x8000) == 0) {
            rb_platform_common_key(&state->common, window->id, shifts[i].key, false);
        }
    }
}

/* ============================================================================
 * Mouse
 * ========================================================================== */

/*
 * Client area pixels. GET_X_LPARAM, not LOWORD: the coordinates are signed
 * 16-bit values, negative left of or above the window while it has the capture.
 */
static rb_mouse_position win32_mouse_position(LPARAM lparam) {
    return (rb_mouse_position){GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
}

static void win32_handle_button(rb_platform_state *state, u32 id, HWND hwnd, rb_mouse_button button,
                                bool down, WPARAM wparam, LPARAM lparam) {
    /*
     * The capture keeps the mouse messages coming while a button is held
     * outside the window, so the release is never lost (ADR 0006). The low
     * word of wparam lists the buttons still down after this message.
     */
    const WPARAM all_buttons = MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2;
    if (down) {
        SetCapture(hwnd);
    } else if ((GET_KEYSTATE_WPARAM(wparam) & all_buttons) == 0) {
        ReleaseCapture();
    }
    rb_platform_common_mouse_button(&state->common, id, button, down, win32_mouse_position(lparam));
}

/* WHEEL_DELTA (120) is one notch; high-resolution wheels send smaller steps. */
static f32 win32_wheel_notches(WPARAM wparam) {
    return (f32)GET_WHEEL_DELTA_WPARAM(wparam) / (f32)WHEEL_DELTA;
}

/* ============================================================================
 * Window procedure
 * ========================================================================== */

static LRESULT CALLBACK win32_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    /*
     * The procedure is a plain function with no context argument, so the
     * state travels through CreateWindowExW's last parameter and is stored in
     * the window's user data on the first message that carries it.
     */
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    rb_platform_state *state = (rb_platform_state *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (state == NULL) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    /*
     * NULL while CreateWindowExW runs (the slot does not know its HWND yet) and
     * inside DestroyWindow (the slot is cleared first): messages sent then do not
     * become events.
     */
    const win32_window *window = win32_find_window(state, hwnd);

    switch (msg) {
    case WM_CLOSE:
        /* Not passed to DefWindowProcW, which would destroy the window: closing is a request. */
        RB_LOG_TRACE("WM_CLOSE: close requested");
        if (window != NULL) {
            rb_event event = {.type = RB_EVENT_WINDOW_CLOSE_REQUESTED, .window = {window->id}};
            rb_platform_common_push_event(&state->common, &event);
        }
        return 0;

    case WM_SIZE:
        /* Sent for every change, also redundant ones: the common state filters them. */
        if (window != NULL) {
            win32_handle_size(state, window->id, hwnd, wparam, lparam);
        }
        return 0;

    case WM_MOVE:
        /*
         * lparam holds the client area's top left in screen pixels, signed (a
         * monitor left of the primary is negative). A minimized window reports
         * -32000,-32000: ignored, the position is kept as the size is.
         */
        if (window != NULL && !IsIconic(hwnd)) {
            rb_platform_common_window_moved(
                &state->common, window->id,
                (rb_window_position){GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        }
        return 0;

    /*
     * Keyboard focus, the same notion as X11's FocusIn/FocusOut. WM_ACTIVATE is
     * about the active top-level window, which a minimized window can still be.
     */
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (window != NULL) {
            rb_platform_common_window_focused(&state->common, window->id, msg == WM_SETFOCUS);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    /*
     * The SYS variants are sent while Alt is held and for F10. They also go to
     * DefWindowProcW, which turns Alt+F4 into WM_CLOSE.
     */
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (window != NULL) {
            win32_handle_key(state, window->id, wparam, lparam);
        }
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        return 0;

    case WM_SYSCOMMAND:
        /*
         * Alt or F10 pressed alone would enter the window menu mode, which
         * swallows the next key press. Games do not want it; the cost is that
         * Alt+Space no longer opens the system menu.
         */
        if ((wparam & 0xFFF0) == SC_KEYMENU) {
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_MOUSEMOVE:
        if (window != NULL) {
            rb_platform_common_mouse_moved(&state->common, window->id,
                                           win32_mouse_position(lparam));
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (window != NULL) {
            win32_handle_button(state, window->id, hwnd, RB_MOUSE_BUTTON_LEFT,
                                msg == WM_LBUTTONDOWN, wparam, lparam);
        }
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (window != NULL) {
            win32_handle_button(state, window->id, hwnd, RB_MOUSE_BUTTON_RIGHT,
                                msg == WM_RBUTTONDOWN, wparam, lparam);
        }
        return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        if (window != NULL) {
            win32_handle_button(state, window->id, hwnd, RB_MOUSE_BUTTON_MIDDLE,
                                msg == WM_MBUTTONDOWN, wparam, lparam);
        }
        return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        if (window != NULL) {
            rb_mouse_button button =
                GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? RB_MOUSE_BUTTON_X1 : RB_MOUSE_BUTTON_X2;
            win32_handle_button(state, window->id, hwnd, button, msg == WM_XBUTTONDOWN, wparam,
                                lparam);
        }
        /* The documentation asks for TRUE here, unlike the other button messages. */
        return TRUE;

    /* Sent to the focused window; lparam holds screen coordinates, which are not needed. */
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (window != NULL) {
            f32            notches = win32_wheel_notches(wparam);
            rb_mouse_wheel delta = msg == WM_MOUSEWHEEL ? (rb_mouse_wheel){0.0f, notches}
                                                        : (rb_mouse_wheel){notches, 0.0f};
            rb_platform_common_mouse_wheel(&state->common, window->id, delta);
        }
        return 0;

    case WM_DPICHANGED: {
        /* Moved to a monitor with another scale: the suggested rect keeps the logical size. */
        const RECT *suggested = (const RECT *)lparam;
        /* The new DPI is in both halves of wparam (X and Y are always equal). */
        RB_LOG_DEBUG("Window moved to a monitor at %u DPI (%u%%)", (unsigned)LOWORD(wparam),
                     (unsigned)MulDiv(LOWORD(wparam), 100, USER_DEFAULT_SCREEN_DPI));
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

/* ============================================================================
 * System
 * ========================================================================== */

/* Everything but the state's memory. On failure nothing is left to undo but the free. */
static bool win32_startup(rb_platform_state *state) {
    if (!rb_platform_common_init(&state->common)) {
        return false;
    }
    state->instance = GetModuleHandleW(NULL);

    /*
     * Without this Windows treats us as 96-DPI and bitmap-scales the window,
     * which looks blurry. It fails if the awareness was already set (e.g. by a
     * manifest or a second startup): harmless, the process is aware either way.
     */
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = {
        .cbSize = sizeof(wc),
        .lpfnWndProc = win32_window_proc,
        .hInstance = state->instance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        /* Black until the renderer draws, instead of whatever was on screen before. */
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .lpszClassName = WIN32_CLASS_NAME,
    };
    if (RegisterClassExW(&wc) == 0) {
        WIN32_LOG_LAST_ERROR("RegisterClassExW");
        return false;
    }
    RB_LOG_DEBUG("Win32 platform started");
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
    if (!win32_startup(state)) {
        RB_FREE_TYPE(allocator, state, rb_platform_state, RB_MEMORY_TAG_PLATFORM);
        return NULL;
    }
    return state;
}

void rb_platform_shutdown(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].hwnd != NULL) {
            rb_window_destroy(state, (rb_window){state->windows[i].id});
        }
    }
    UnregisterClassW(WIN32_CLASS_NAME, state->instance);
    RB_LOG_DEBUG("Win32 platform shut down");
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
    if (desc->width == 0 || desc->height == 0) {
        RB_LOG_ERROR("Window size must not be zero, got %ux%u", desc->width, desc->height);
        return invalid;
    }

    wchar_t title[WIN32_TITLE_MAX];
    if (!win32_utf8_to_wide(desc->title, title, WIN32_TITLE_MAX)) {
        RB_LOG_ERROR("Window title is not valid UTF-8 or longer than %d UTF-16 units",
                     WIN32_TITLE_MAX - 1);
        return invalid;
    }

    u32 id = rb_handle_pool_alloc(&state->common.window_pool);
    if (id == RB_HANDLE_INVALID) {
        RB_LOG_ERROR("No free window slot (at most %u windows)", RB_MAX_WINDOWS);
        return invalid;
    }

    /* Created hidden (no WS_VISIBLE): it is resized below before anyone sees it. */
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD ex_style = WS_EX_APPWINDOW;
    HWND  hwnd =
        CreateWindowExW(ex_style, WIN32_CLASS_NAME, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, state->instance, state);
    if (hwnd == NULL) {
        WIN32_LOG_LAST_ERROR("CreateWindowExW");
        rb_handle_pool_free(&state->common.window_pool, id);
        return invalid;
    }

    /*
     * desc sizes are logical units (ADR 0006); 96 DPI is 100%. The DPI must be
     * the one of the monitor the window landed on: GetDpiForSystem is fixed at
     * logon and is wrong after a scale change or on a secondary monitor.
     */
    UINT dpi = GetDpiForWindow(hwnd);
    RECT rect = {0, 0, MulDiv((int)desc->width, (int)dpi, USER_DEFAULT_SCREEN_DPI),
                 MulDiv((int)desc->height, (int)dpi, USER_DEFAULT_SCREEN_DPI)};
    /* SetWindowPos takes the outer size: grow the rect by borders and title bar. */
    AdjustWindowRectExForDpi(&rect, style, FALSE, ex_style, dpi);
    SetWindowPos(hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    /*
     * Measured, not computed: the state and the log hold what the sizing above
     * really produced. Recorded before the slot learns its HWND, so the WM_SIZE
     * sent by ShowWindow finds it and is filtered as unchanged.
     */
    RECT client;
    GetClientRect(hwnd, &client);
    rb_window_size size = win32_window_size(hwnd, (u32)client.right, (u32)client.bottom);
    /* The client area's top left in screen pixels, where CW_USEDEFAULT placed the window. */
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin);
    rb_window_position position = {origin.x, origin.y};
    rb_platform_common_window_opened(&state->common, id, size, position);

    win32_window *slot = &state->windows[rb_handle_index(id)];
    slot->hwnd = hwnd;
    slot->id = id;
    /* Also activates the window: WM_SETFOCUS becomes RB_EVENT_WINDOW_FOCUS_GAINED. */
    ShowWindow(hwnd, SW_SHOW);

    RB_LOG_INFO("Window 0x%08x created: %ux%u logical, %ux%u pixels at %u DPI (%u%%), at %d,%d", id,
                size.width, size.height, size.pixel_width, size.pixel_height, (unsigned)dpi,
                (unsigned)MulDiv((int)dpi, 100, USER_DEFAULT_SCREEN_DPI), position.x, position.y);
    return (rb_window){id};
}

void rb_window_destroy(rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    if (!rb_handle_pool_valid(&state->common.window_pool, window.id)) {
        return;
    }
    win32_window *slot = &state->windows[rb_handle_index(window.id)];
    /*
     * The slot is cleared before DestroyWindow, which sends messages of its own
     * (WM_KILLFOCUS...): the application destroyed the window, it needs no
     * events about it.
     */
    HWND hwnd = slot->hwnd;
    rb_platform_common_window_closed(&state->common, window.id);
    *slot = (win32_window){0};
    DestroyWindow(hwnd);
    rb_handle_pool_free(&state->common.window_pool, window.id);
    RB_LOG_DEBUG("Window 0x%08x destroyed", window.id);
}

/* ============================================================================
 * Events
 * ========================================================================== */

void rb_platform_pump(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    /* Messages reach the state through each window's user data. */
    rb_platform_common_begin_pump(&state->common);

    MSG msg;
    /*
     * PeekMessageW returns at once when the queue is empty (GetMessageW would
     * wait). Known limit: while the user drags or resizes a window, Windows
     * runs its own loop inside DispatchMessageW and this call does not return.
     */
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    win32_release_stuck_shifts(state);
}
