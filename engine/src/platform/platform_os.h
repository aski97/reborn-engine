/*
 * Stateless OS services (ADR 0008): usable before rb_platform_startup and
 * without a platform state, because the logger and asserts need them from the
 * first line of main(). Implemented by win32/win32_os.c and linux/linux_os.c,
 * virtual memory and the aligned heap by win32/win32_memory.c and
 * linux/linux_memory.c.
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

/* ============================================================================
 * Virtual memory (ADR 0010, A1)
 *
 * Reserving claims a range of addresses and costs no RAM; committing backs
 * pages of a reserved range with memory, zero-filled on first touch. Touching
 * a reserved page that is not committed crashes, which is what catches
 * overruns. Sizes and pointers are whole pages (RB_ASSERT): rounding is the
 * caller's job, so it knows exactly what it got.
 * ========================================================================== */

/* Bytes per page: the unit of commit and decommit. A power of two, 4 KiB on x64. */
usize rb_platform_page_size(void);

/*
 * Alignment of every reservation's start: 64 KiB on Windows, the page size on
 * Linux. A multiple of the page size.
 */
usize rb_platform_reserve_granularity(void);

/* `size` > 0, whole pages. NULL on failure (logged): no address range that large is free. */
void *rb_platform_memory_reserve(usize size);

/*
 * Pages of a reserved range become readable and writable. Committing pages
 * already committed keeps their contents. False on failure (logged): the system
 * is out of memory (RAM plus page file / swap).
 */
bool rb_platform_memory_commit(void *ptr, usize size);

/* Gives the pages' memory back; they stay reserved and read as zero once committed again. */
void rb_platform_memory_decommit(void *ptr, usize size);

/* `ptr` and `size` exactly as reserved: the whole range is freed, committed or not. */
void rb_platform_memory_release(void *ptr, usize size);

/* ============================================================================
 * Aligned heap (ADR 0010, J1)
 *
 * The C runtime's heap with any power-of-two alignment: _aligned_malloc on
 * Windows (the UCRT lacks C11 aligned_alloc, as its aligned blocks cannot be
 * freed with plain free()), posix_memalign on Linux. Sizes are > 0: what
 * malloc does with 0 differs between runtimes. Failures are not logged here:
 * the allocator knows what was asked.
 * ========================================================================== */

/* NULL when the heap has no memory left. */
void *rb_platform_heap_alloc(usize size, usize align);

/*
 * Keeps the first min(old, new) bytes, may move. `align` as allocated. NULL on
 * failure, the old block still valid.
 */
void *rb_platform_heap_resize(void *ptr, usize old_size, usize new_size, usize align);

/* A block from rb_platform_heap_alloc or _resize, never NULL. */
void rb_platform_heap_free(void *ptr);

/* ============================================================================
 * Dynamic libraries (ADR 0013)
 *
 * Loading a DLL / shared object while running, instead of linking it: a
 * missing library is then an error the engine reports, not an executable the
 * OS refuses to start. Implemented by win32/win32_library.c and
 * linux/linux_library.c.
 * ========================================================================== */

/* Opaque: an HMODULE on Windows, a dlopen handle on Linux. */
typedef struct rb_platform_library rb_platform_library;

/*
 * Any function's address. Cast it to the real type before calling: calling
 * through a different type is undefined behaviour.
 */
typedef void (*rb_platform_proc)(void);

/*
 * `name` is a file name (UTF-8), searched where the OS looks for libraries;
 * Windows skips the current folder and PATH. NULL on failure (logged).
 */
rb_platform_library *rb_platform_library_open(const char *name);

/* NULL when the library does not export `name`; not logged, absence may be expected. */
rb_platform_proc rb_platform_library_symbol(rb_platform_library *library, const char *name);

/* Every symbol taken from `library` becomes invalid. */
void rb_platform_library_close(rb_platform_library *library);

#endif /* RB_PLATFORM_OS_H */
