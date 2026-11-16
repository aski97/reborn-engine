/*
 * Virtual memory, Linux (ADR 0010, A1). POSIX mmap, not X11-specific.
 */

/*
 * MAP_ANONYMOUS is not in POSIX 2008: glibc shows it only with
 * _DEFAULT_SOURCE, which also enables POSIX 2008 (sysconf, mprotect).
 */
#define _DEFAULT_SOURCE

#include "platform/platform_os.h"

#include <reborn/assert.h>
#include <reborn/log.h>

#include <errno.h>
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
