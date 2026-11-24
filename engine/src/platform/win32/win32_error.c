/*
 * Win32 error reporting.
 */
#include "platform/win32/win32_error.h"

#include <reborn/log.h>

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

void win32_log_last_error(const char *what, const char *file, u32 line, const char *func) {
    /* First: any call below may overwrite it. */
    win32_log_error(what, GetLastError(), file, line, func);
}

void win32_log_error(const char *what, u32 error, const char *file, u32 line, const char *func) {
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
