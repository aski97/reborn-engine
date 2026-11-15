/*
 * Stateless OS services (ADR 0008): usable before rb_platform_startup and
 * without a platform state, because the logger and asserts need them from the
 * first line of main(). Implemented by win32/win32_os.c and linux/linux_os.c.
 */
#ifndef RB_PLATFORM_OS_H
#define RB_PLATFORM_OS_H

#include <reborn/defines.h>

/*
 * Prepares stderr for log output and reports whether it is a terminal that
 * shows ANSI colors. False when redirected to a file or a pipe (ctest): escape
 * codes would end up as garbage in the captured text.
 */
bool rb_platform_console_init(void);

/* Writes UTF-8 text to stderr as is: no conversion of the caller's encoding needed. */
void rb_platform_console_write(const char *text, usize length);

bool rb_platform_debugger_attached(void);

#endif /* RB_PLATFORM_OS_H */
