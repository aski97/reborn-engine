/*
 * Assertions (ADR 0008): checks of conditions that can only be false because
 * of a bug in the code, never because of input or the environment.
 *
 *   RB_ASSERT(index < count);
 *   RB_ASSERT_MSG(window.id != 0, "window %u was never created", slot);
 *
 * On failure: FATAL log with the condition's text, then a breakpoint if a
 * debugger is attached, otherwise abort(). Disabled in release: the condition
 * is NOT evaluated there, so it must have no side effects.
 */
#ifndef RB_ASSERT_H
#define RB_ASSERT_H

#include <reborn/log.h>

/* Define it (0 or 1) before including this header to override the default. */
#ifndef RB_ASSERT_ENABLED
    #if defined(RB_DEBUG)
        #define RB_ASSERT_ENABLED 1
    #else
        #define RB_ASSERT_ENABLED 0
    #endif
#endif

/* Called by the macros; never return. */
RB_API _Noreturn void rb_assert_fail(const char *expr, const char *file, u32 line,
                                     const char *func);
RB_API _Noreturn void rb_assert_fail_msg(const char *expr, const char *file, u32 line,
                                         const char *func, const char *fmt, ...)
    RB_PRINTF_FORMAT(5, 6);

/*
 * `#cond` turns the condition into a string literal ("index < count").
 * With RB_ASSERT_ENABLED 0, `&&` never evaluates `cond`, but it still has to
 * compile, so a broken assert is noticed in release too.
 */
#define RB_ASSERT(cond)                                                                            \
    do {                                                                                           \
        if (RB_ASSERT_ENABLED && !(cond)) {                                                        \
            rb_assert_fail(#cond, __FILE__, __LINE__, __func__);                                   \
        }                                                                                          \
    } while (0)

#define RB_ASSERT_MSG(cond, ...)                                                                   \
    do {                                                                                           \
        if (RB_ASSERT_ENABLED && !(cond)) {                                                        \
            rb_assert_fail_msg(#cond, __FILE__, __LINE__, __func__, __VA_ARGS__);                  \
        }                                                                                          \
    } while (0)

#endif /* RB_ASSERT_H */
