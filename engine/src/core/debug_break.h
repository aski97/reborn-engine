/*
 * Stops in the debugger as if a breakpoint were set on this line. Without a
 * debugger it would kill the process with a breakpoint exception: check
 * rb_platform_debugger_attached() first. Used by asserts and by the Vulkan
 * validation callback (ADR 0008, ADR 0013).
 */
#ifndef RB_DEBUG_BREAK_H
#define RB_DEBUG_BREAK_H

#include <reborn/defines.h>

#if defined(RB_COMPILER_CLANG)
    #define RB_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(RB_COMPILER_MSVC)
    #include <intrin.h>
    #define RB_DEBUG_BREAK() __debugbreak()
#else
    /* GCC has no resumable trap: execution cannot continue past it. */
    #define RB_DEBUG_BREAK() __builtin_trap()
#endif

#endif /* RB_DEBUG_BREAK_H */
