/*
 * Virtual memory, Win32 (ADR 0010, A1).
 */
#include "platform/platform_os.h"
#include "platform/win32/win32_error.h"

#include <reborn/assert.h>

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

/* Read on every call, as the clock does (ADR 0009, C1): stateless and cheap. */
usize rb_platform_page_size(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

/*
 * Larger than a page for historical reasons (Alpha CPUs): VirtualAlloc
 * rounds a reservation's start down to it, so the space between two small
 * reservations is lost address space.
 */
usize rb_platform_reserve_granularity(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwAllocationGranularity;
}

void *rb_platform_memory_reserve(usize size) {
    RB_ASSERT(size > 0 && size % rb_platform_page_size() == 0);
    /*
     * PAGE_NOACCESS is ignored for a reservation (its pages cannot be touched
     * anyway) but the parameter must be a valid protection.
     */
    void *ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    if (ptr == NULL) {
        WIN32_LOG_LAST_ERROR("VirtualAlloc(MEM_RESERVE)");
    }
    return ptr;
}

bool rb_platform_memory_commit(void *ptr, usize size) {
    usize page = rb_platform_page_size();
    RB_ASSERT(ptr != NULL && (usize)ptr % page == 0);
    RB_ASSERT(size > 0 && size % page == 0);
    /*
     * Charged at once against the commit limit (RAM plus page file) even
     * though the RAM itself is taken on first touch: this is where Windows says
     * "out of memory", not later at an unlucky write.
     */
    if (VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        WIN32_LOG_LAST_ERROR("VirtualAlloc(MEM_COMMIT)");
        return false;
    }
    return true;
}

void rb_platform_memory_decommit(void *ptr, usize size) {
    usize page = rb_platform_page_size();
    RB_ASSERT(ptr != NULL && (usize)ptr % page == 0);
    RB_ASSERT(size > 0 && size % page == 0);
    if (!VirtualFree(ptr, size, MEM_DECOMMIT)) {
        WIN32_LOG_LAST_ERROR("VirtualFree(MEM_DECOMMIT)");
    }
}

void rb_platform_memory_release(void *ptr, usize size) {
    RB_ASSERT(ptr != NULL && size > 0);
    /* Windows remembers the reservation's size: MEM_RELEASE requires 0. */
    RB_UNUSED(size);
    if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
        WIN32_LOG_LAST_ERROR("VirtualFree(MEM_RELEASE)");
    }
}
