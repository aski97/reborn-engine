/*
 * Assertion failures (ADR 0008): log, then stop where the bug is.
 */
#include <reborn/assert.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "platform/platform_os.h"

/*
 * Stops in the debugger as if a breakpoint were set on this line. Without a
 * debugger it would kill the process with a breakpoint exception, hence the
 * check before using it.
 */
#if defined(RB_COMPILER_CLANG)
    #define RB_ASSERT_BREAK_() __builtin_debugtrap()
#elif defined(RB_COMPILER_MSVC)
    #include <intrin.h>
    #define RB_ASSERT_BREAK_() __debugbreak()
#else
    /* GCC has no resumable trap; execution does not resume past an assert anyway. */
    #define RB_ASSERT_BREAK_() __builtin_trap()
#endif

static _Noreturn void assert_stop(void) {
    if (rb_platform_debugger_attached()) {
        RB_ASSERT_BREAK_();
    }
    /*
     * abort, not exit: no atexit handlers run on state we know is broken, and
     * the OS records a crash (core dump on Linux, Windows Error Reporting).
     */
    abort();
}

void rb_assert_fail(const char *expr, const char *file, u32 line, const char *func) {
    rb_log_write(RB_LOG_LEVEL_FATAL, file, line, func, "Assertion failed: %s", expr);
    assert_stop();
}

void rb_assert_fail_msg(const char *expr, const char *file, u32 line, const char *func,
                        const char *fmt, ...) {
    char    message[RB_LOG_MESSAGE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    rb_log_write(RB_LOG_LEVEL_FATAL, file, line, func, "Assertion failed: %s: %s", expr, message);
    assert_stop();
}
