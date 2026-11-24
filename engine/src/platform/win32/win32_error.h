/*
 * Win32 error reporting, shared by the Win32 backend files.
 */
#ifndef RB_WIN32_ERROR_H
#define RB_WIN32_ERROR_H

#include <reborn/defines.h>

/*
 * Logs `what` with the system's description of GetLastError(). A macro, so the
 * record carries the caller's location instead of the helper's.
 */
#define WIN32_LOG_LAST_ERROR(what) win32_log_last_error((what), __FILE__, __LINE__, __func__)

void win32_log_last_error(const char *what, const char *file, u32 line, const char *func);

/* The same for an error code got elsewhere: WSAGetLastError() for sockets. */
#define WIN32_LOG_ERROR(what, error) win32_log_error((what), (error), __FILE__, __LINE__, __func__)

void win32_log_error(const char *what, u32 error, const char *file, u32 line, const char *func);

#endif /* RB_WIN32_ERROR_H */
