/*
 * Win32 platform backend (ADR 0006).
 *
 * Windows delivers messages by calling win32_window_proc from inside
 * DispatchMessageW; the procedure turns the ones we care about into rb_event
 * and queues them, so engine code never runs inside an OS callback.
 */
#include "platform/platform_internal.h"

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

/*
 * Logs `what` with the system's description of GetLastError(). A macro, so the
 * record carries the caller's location instead of this helper's.
 */
#define WIN32_LOG_LAST_ERROR(what) win32_log_last_error((what), __FILE__, __LINE__, __func__)

static void win32_log_last_error(const char *what, const char *file, u32 line, const char *func) {
    DWORD   error = GetLastError();
    wchar_t wide[512];
    char    text[1024];
    /* In the user's language: the text is for whoever reads the log on this machine. */
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                  error, 0, wide, (DWORD)RB_ARRAY_COUNT(wide), NULL);
    /* System messages end with "\r\n", which would break the log line. */
    while (length > 0 &&
           (wide[length - 1] == L'\r' || wide[length - 1] == L'\n' || wide[length - 1] == L' ')) {
        --length;
    }
    int bytes = length > 0 ? WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, text,
                                                 (int)sizeof(text) - 1, NULL, NULL)
                           : 0;
    text[bytes > 0 ? bytes : 0] = '\0';
    rb_log_write(RB_LOG_LEVEL_ERROR, file, line, func, "%s failed: error %lu: %s", what,
                 (unsigned long)error, bytes > 0 ? text : "(no description)");
}

static win32_window *win32_find_window(rb_platform_state *state, HWND hwnd) {
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].hwnd == hwnd) {
            return &state->windows[i];
        }
    }
    return NULL;
}

static void win32_push_window_event(rb_platform_state *state, HWND hwnd, rb_event_type type) {
    const win32_window *window = win32_find_window(state, hwnd);
    if (window == NULL) {
        return; /* still inside CreateWindowExW: the slot does not know its HWND yet */
    }
    rb_event event = {.type = type, .window = {window->id}};
    rb_platform_common_push_event(&state->common, &event);
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

    switch (msg) {
    case WM_CLOSE:
        /* Not passed to DefWindowProcW, which would destroy the window: closing is a request. */
        RB_LOG_TRACE("WM_CLOSE: close requested");
        win32_push_window_event(state, hwnd, RB_EVENT_WINDOW_CLOSE_REQUESTED);
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

usize rb_platform_state_size(void) {
    return sizeof(struct rb_platform_state);
}

bool rb_platform_startup(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    ZeroMemory(state, sizeof(*state));
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

void rb_platform_shutdown(rb_platform_state *state) {
    RB_ASSERT(state != NULL);
    for (u32 i = 0; i < RB_MAX_WINDOWS; ++i) {
        if (state->windows[i].hwnd != NULL) {
            rb_window_destroy(state, (rb_window){state->windows[i].id});
        }
    }
    UnregisterClassW(WIN32_CLASS_NAME, state->instance);
    RB_LOG_DEBUG("Win32 platform shut down");
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

    win32_window *slot = &state->windows[rb_handle_index(id)];
    slot->hwnd = hwnd;
    slot->id = id;
    ShowWindow(hwnd, SW_SHOW);

    /* Measured, not computed: the log shows what the sizing above really produced. */
    RECT client;
    GetClientRect(hwnd, &client);
    RB_LOG_INFO("Window 0x%08x created: %ux%u logical, %ldx%ld pixels at %u DPI (%u%%)", id,
                desc->width, desc->height, client.right, client.bottom, (unsigned)dpi,
                (unsigned)MulDiv((int)dpi, 100, USER_DEFAULT_SCREEN_DPI));
    return (rb_window){id};
}

void rb_window_destroy(rb_platform_state *state, rb_window window) {
    RB_ASSERT(state != NULL);
    if (!rb_handle_pool_valid(&state->common.window_pool, window.id)) {
        return;
    }
    win32_window *slot = &state->windows[rb_handle_index(window.id)];
    DestroyWindow(slot->hwnd);
    *slot = (win32_window){0};
    rb_handle_pool_free(&state->common.window_pool, window.id);
    RB_LOG_DEBUG("Window 0x%08x destroyed", window.id);
}

/* ============================================================================
 * Events
 * ========================================================================== */

void rb_platform_pump(rb_platform_state *state) {
    RB_UNUSED(state); /* messages reach the state through each window's user data */

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
}
