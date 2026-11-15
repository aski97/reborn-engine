/*
 * Foundational types and macros shared by the whole engine.
 * Must stay free of platform headers: it is included everywhere, including
 * by game code, and pulling in <windows.h> here would leak into all of it.
 */
#ifndef RB_DEFINES_H
#define RB_DEFINES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Fixed-width types
 * ========================================================================== */

/*
 * Built-in types are not portable in size (`long` is 32-bit on Windows but
 * 64-bit on Linux), so engine code uses these whenever the size of a value
 * matters: file formats, GPU buffers, hashing, etc.
 */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float  f32;
typedef double f64;

typedef size_t    usize;
typedef ptrdiff_t isize;

_Static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
_Static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");
/* Memory code will rely on 64-bit pointers (e.g. large virtual reservations). */
_Static_assert(sizeof(void *) == 8, "Reborn only supports 64-bit targets");

/* ============================================================================
 * Platform detection
 * ========================================================================== */

#if defined(_WIN32)
    #define RB_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define RB_PLATFORM_LINUX 1
#else
    #error "Unsupported platform (supported: Windows, Linux)"
#endif

/* ============================================================================
 * Compiler detection
 * ========================================================================== */

/* Order matters: Clang also defines __GNUC__, and clang-cl also defines _MSC_VER. */
#if defined(__clang__)
    #define RB_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define RB_COMPILER_GCC 1
#elif defined(_MSC_VER)
    #define RB_COMPILER_MSVC 1
#endif

/* ============================================================================
 * Symbol visibility
 * ========================================================================== */

/*
 * Marks a function as part of the engine's public binary interface.
 * Empty for the static build. With REBORN_SHARED it expands to dllexport while
 * building the engine and dllimport while building its consumers; every
 * public function must carry it, or the shared build will fail to link.
 */
#if defined(RB_SHARED)
    #if defined(RB_PLATFORM_WINDOWS)
        #if defined(RB_BUILD_DLL)
            #define RB_API __declspec(dllexport)
        #else
            #define RB_API __declspec(dllimport)
        #endif
    #else
        #define RB_API __attribute__((visibility("default")))
    #endif
#else
    #define RB_API
#endif

/* ============================================================================
 * Utilities
 * ========================================================================== */

#if defined(RB_COMPILER_MSVC)
    #define RB_INLINE    static __forceinline
    #define RB_NO_INLINE __declspec(noinline)
#else
    #define RB_INLINE    static inline __attribute__((always_inline))
    #define RB_NO_INLINE __attribute__((noinline))
#endif

/*
 * Lets the compiler check a printf-style call: argument `fmt_index` is the
 * format string, variadic arguments start at `first_arg` (1-based). Without
 * it, `%u` given a pointer compiles silently and prints garbage.
 */
#if defined(RB_COMPILER_CLANG) || defined(RB_COMPILER_GCC)
    #define RB_PRINTF_FORMAT(fmt_index, first_arg)                                                 \
        __attribute__((format(printf, fmt_index, first_arg)))
#else
    #define RB_PRINTF_FORMAT(fmt_index, first_arg)
#endif

#define RB_UNUSED(x) ((void)(x))
/* Only valid on true arrays: on a pointer it silently yields a wrong count. */
#define RB_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define RB_KIB(n) ((u64)(n) << 10)
#define RB_MIB(n) ((u64)(n) << 20)
#define RB_GIB(n) ((u64)(n) << 30)

#endif /* RB_DEFINES_H */
