/*
 * AddressSanitizer hooks for custom allocators (ADR 0010, I2).
 *
 * ASan knows which bytes are valid only for memory from malloc. An arena is
 * one big block to it, all valid, so an overflow from one allocation into the
 * next goes unnoticed. The arena marks its free and red zone bytes as poisoned
 * itself: any access to them is then reported like a heap overflow.
 *
 * Outside ASan builds the macros evaluate their arguments and do nothing.
 */
#ifndef RB_MEMORY_ASAN_H
#define RB_MEMORY_ASAN_H

/*
 * Clang reports ASan through __has_feature, GCC through __SANITIZE_ADDRESS__.
 * The nested #if matters: a compiler without __has_feature would fail to parse
 * `defined(__has_feature) && __has_feature(...)` written on one line.
 */
#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define RB_ASAN_ENABLED 1
    #endif
#endif
#if !defined(RB_ASAN_ENABLED) && defined(__SANITIZE_ADDRESS__)
    #define RB_ASAN_ENABLED 1
#endif
#ifndef RB_ASAN_ENABLED
    #define RB_ASAN_ENABLED 0
#endif

#if RB_ASAN_ENABLED
    #include <sanitizer/asan_interface.h>
    #define RB_ASAN_POISON(ptr, size)   ASAN_POISON_MEMORY_REGION((ptr), (size))
    #define RB_ASAN_UNPOISON(ptr, size) ASAN_UNPOISON_MEMORY_REGION((ptr), (size))
#else
    #define RB_ASAN_POISON(ptr, size)   ((void)(ptr), (void)(size))
    #define RB_ASAN_UNPOISON(ptr, size) ((void)(ptr), (void)(size))
#endif

#endif /* RB_MEMORY_ASAN_H */
