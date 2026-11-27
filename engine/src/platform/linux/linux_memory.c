/*
 * Virtual memory and aligned heap, Linux (ADR 0010, A1, J1). POSIX mmap and
 * posix_memalign, not X11-specific.
 */

/*
 * MAP_ANONYMOUS is not in POSIX 2008: glibc shows it only with
 * _DEFAULT_SOURCE, which also enables POSIX 2008 (sysconf, mprotect,
 * posix_memalign).
 */
#define _DEFAULT_SOURCE

#include "platform/platform_os.h"

#include <reborn/assert.h>
#include <reborn/memory.h>
#include <reborn/log.h>

#include <errno.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* strerror is not thread-safe; fine until the engine has threads (TODO.md, thread safety). */
#define LINUX_LOG_ERRNO(what) RB_LOG_ERROR("%s failed: %s", (what), strerror(errno))

usize rb_platform_page_size(void) {
    /* Cannot fail for _SC_PAGESIZE. */
    return (usize)sysconf(_SC_PAGESIZE);
}

usize rb_platform_reserve_granularity(void) {
    return rb_platform_page_size();
}

void *rb_platform_memory_reserve(usize size) {
    RB_ASSERT(size > 0 && size % rb_platform_page_size() == 0);
    /*
     * PROT_NONE: any access crashes until committed. Not MAP_NORESERVE: the
     * kernel counts a private mapping against the commit limit only once it
     * becomes writable, so without the flag that happens in commit (mprotect)
     * and commit can fail cleanly as on Windows. With the flag, the commit
     * would always succeed and the process could be killed at a later write.
     */
    void *ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        LINUX_LOG_ERRNO("mmap(PROT_NONE)");
        return NULL;
    }
    return ptr;
}

bool rb_platform_memory_commit(void *ptr, usize size) {
    usize page = rb_platform_page_size();
    RB_ASSERT(ptr != NULL && (usize)ptr % page == 0);
    RB_ASSERT(size > 0 && size % page == 0);
    if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0) {
        LINUX_LOG_ERRNO("mprotect(PROT_READ | PROT_WRITE)");
        return false;
    }
    return true;
}

void rb_platform_memory_decommit(void *ptr, usize size) {
    usize page = rb_platform_page_size();
    RB_ASSERT(ptr != NULL && (usize)ptr % page == 0);
    RB_ASSERT(size > 0 && size % page == 0);
    /*
     * A fresh PROT_NONE mapping over the range, in one call: the old pages and
     * their commit charge are dropped and the addresses stay ours. mprotect
     * back to PROT_NONE would keep both, madvise(MADV_DONTNEED) the charge.
     */
    if (mmap(ptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        LINUX_LOG_ERRNO("mmap(MAP_FIXED, PROT_NONE)");
    }
}

void rb_platform_memory_release(void *ptr, usize size) {
    RB_ASSERT(ptr != NULL && size > 0);
    if (munmap(ptr, size) != 0) {
        LINUX_LOG_ERRNO("munmap");
    }
}

/* ============================================================================
 * Aligned heap
 *
 * posix_memalign, not C11 aligned_alloc: C11 wants the size to be a multiple
 * of the alignment (ASan enforces it), which would waste most of a page on a
 * small page-aligned block. glibc's free() and realloc() accept its blocks,
 * but realloc only guarantees malloc's alignment (alignof(max_align_t), 16 on
 * x64): above it, resize allocates, copies and frees by hand.
 * ========================================================================== */

void *rb_platform_heap_alloc(usize size, usize align) {
    RB_ASSERT(size > 0 && align > 0 && (align & (align - 1)) == 0);
    /* posix_memalign also wants a multiple of sizeof(void *): a stricter alignment is fine. */
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    void *ptr = NULL;
    /* Returns the error instead of setting errno; the only possible one here is ENOMEM. */
    if (posix_memalign(&ptr, align, size) != 0) {
        return NULL;
    }
    return ptr;
}

void *rb_platform_heap_resize(void *ptr, usize old_size, usize new_size, usize align) {
    RB_ASSERT(ptr != NULL && new_size > 0 && align > 0 && (align & (align - 1)) == 0);
    if (align <= alignof(max_align_t)) {
        return realloc(ptr, new_size);
    }
    void *moved = rb_platform_heap_alloc(new_size, align);
    if (moved == NULL) {
        return NULL;
    }
    memcpy(moved, ptr, old_size < new_size ? old_size : new_size);
    free(ptr);
    return moved;
}

void rb_platform_heap_free(void *ptr) {
    RB_ASSERT(ptr != NULL);
    free(ptr);
}

/* ============================================================================
 * Process memory (ADR 0025, M2)
 * ========================================================================== */

/*
 * /proc/self/status is text, one "Name:   value kB" per line; the kernel has
 * printed sizes in kB (KiB) there for decades. /proc/self/statm would be
 * shorter but has no peak.
 */
bool rb_process_get_memory(rb_process_memory *out) {
    RB_ASSERT(out != NULL);
    *out = (rb_process_memory){0};
    FILE *file = fopen("/proc/self/status", "r");
    if (file == NULL) {
        return false;
    }
    bool               has_rss = false;
    bool               has_hwm = false;
    char               line[256];
    unsigned long long kib = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "VmRSS: %llu kB", &kib) == 1) {
            out->resident_bytes = (usize)kib * 1024u;
            has_rss = true;
        } else if (sscanf(line, "VmHWM: %llu kB", &kib) == 1) {
            out->peak_resident_bytes = (usize)kib * 1024u;
            has_hwm = true;
        }
    }
    fclose(file);
    if (!has_rss || !has_hwm) {
        *out = (rb_process_memory){0};
        return false;
    }
    return true;
}
