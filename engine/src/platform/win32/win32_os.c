/*
 * Stateless OS services, Win32 (ADR 0008).
 */
#include "platform/platform_os.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <stdio.h>

/* A logger console line fits easily; a longer text falls back to raw bytes. */
#define WIN32_CONSOLE_WIDE_MAX 4096

bool rb_platform_console_init(void) {
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD  mode;
    /* GetConsoleMode fails on files and pipes: it is the "is this a console?" test. */
    if (handle == NULL || handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return false;
    }
    /*
     * The console interprets ANSI escape sequences only when asked (Windows 10
     * and later). The mode belongs to the console, so it outlives the process;
     * harmless, current shells turn it on themselves.
     */
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

void rb_platform_console_write(const char *text, usize length) {
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD  mode;
    /*
     * A console decodes bytes with its code page (850 or 437, not UTF-8), so
     * "città" would print as "citt├á". SetConsoleOutputCP(CP_UTF8) would fix
     * that but change the console of the shell we were started from, also
     * after we exit. WriteConsoleW takes UTF-16 and ignores the code page.
     */
    if (length > 0 && length <= WIN32_CONSOLE_WIDE_MAX && GetConsoleMode(handle, &mode)) {
        wchar_t wide[WIN32_CONSOLE_WIDE_MAX];
        /* Flags 0: malformed UTF-8 becomes U+FFFD instead of failing the whole line. */
        int count =
            MultiByteToWideChar(CP_UTF8, 0, text, (int)length, wide, WIN32_CONSOLE_WIDE_MAX);
        if (count > 0) {
            DWORD written;
            WriteConsoleW(handle, wide, (DWORD)count, &written, NULL);
            return;
        }
    }
    /* Redirected to a file or a pipe: UTF-8 bytes as they are, which is what readers expect. */
    fwrite(text, 1, length, stderr);
}

bool rb_platform_debugger_attached(void) {
    return IsDebuggerPresent() != 0;
}
