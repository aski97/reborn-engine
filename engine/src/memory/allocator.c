/*
 * Allocator interface helpers (ADR 0010, C1, D2, H1): what every allocator
 * would otherwise repeat (zeroing, overflow checks, the NULL cases) is done
 * once here, so a vtable only implements the three raw operations. Tracking by
 * tag (ADR 0012) happens here too, so every allocator is counted.
 */
#include <reborn/memory.h>

#include <reborn/assert.h>
#include <reborn/log.h>

#include <string.h>

#include "memory/memory_internal.h"

/* Bugs in the caller, not failures: asserted, never logged. */
#define ALLOCATOR_CHECK(allocator, align, tag)                                                     \
    do {                                                                                           \
        RB_ASSERT((allocator).vtable != NULL);                                                     \
        RB_ASSERT((align) > 0 && ((align) & ((align) - 1)) == 0);                                  \
        RB_ASSERT((unsigned)(tag) < RB_MEMORY_TAG_COUNT);                                          \
    } while (0)

/* ============================================================================
 * Allocation
 * ========================================================================== */

void *rb_alloc_uninit(rb_allocator allocator, usize size, usize align, rb_memory_tag tag) {
    ALLOCATOR_CHECK(allocator, align, tag);
    void *ptr = allocator.vtable->alloc(allocator.ctx, size, align);
    if (ptr != NULL) {
        rb_memory_track_alloc(tag, size);
    }
    return ptr;
}

void *rb_alloc(rb_allocator allocator, usize size, usize align, rb_memory_tag tag) {
    void *ptr = rb_alloc_uninit(allocator, size, align, tag);
    /* Zeroing here, not in each allocator: one memset whatever the source. */
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void *rb_alloc_array(rb_allocator allocator, usize elem_size, usize align, usize count,
                     rb_memory_tag tag) {
    /* A count read from a file can make elem_size * count wrap to a small, "valid" size. */
    if (elem_size != 0 && count > SIZE_MAX / elem_size) {
        RB_LOG_ERROR("Array of %zu elements of %zu bytes overflows", count, elem_size);
        return NULL;
    }
    return rb_alloc(allocator, elem_size * count, align, tag);
}

/* ============================================================================
 * Resize and free
 * ========================================================================== */

void *rb_resize_uninit(rb_allocator allocator, void *ptr, usize old_size, usize new_size,
                       usize align, rb_memory_tag tag) {
    /* As realloc(NULL, n): lets a growable array start empty without a special case. */
    if (ptr == NULL) {
        RB_ASSERT_MSG(old_size == 0, "resize of a NULL block of %zu bytes", old_size);
        return rb_alloc_uninit(allocator, new_size, align, tag);
    }
    ALLOCATOR_CHECK(allocator, align, tag);
    void *moved = allocator.vtable->resize(allocator.ctx, ptr, old_size, new_size, align);
    if (moved != NULL) {
        rb_memory_track_resize(tag, old_size, new_size);
    }
    return moved;
}

void *rb_resize(rb_allocator allocator, void *ptr, usize old_size, usize new_size, usize align,
                rb_memory_tag tag) {
    u8 *result = rb_resize_uninit(allocator, ptr, old_size, new_size, align, tag);
    /* Only the grown part: the rest is the caller's data, kept by the resize. */
    if (result != NULL && new_size > old_size) {
        memset(result + old_size, 0, new_size - old_size);
    }
    return result;
}

void rb_free(rb_allocator allocator, void *ptr, usize size, usize align, rb_memory_tag tag) {
    ALLOCATOR_CHECK(allocator, align, tag);
    /* As free(NULL): cleanup code can free whatever it got, even a failed allocation. */
    if (ptr == NULL) {
        return;
    }
    /* First: a size or tag passed back wrong asserts before the block is gone. */
    rb_memory_track_free(tag, size);
    allocator.vtable->free(allocator.ctx, ptr, size, align);
}
