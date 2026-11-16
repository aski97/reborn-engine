/*
 * Heap allocator (ADR 0010, J1): the rb_allocator interface over the
 * platform's aligned heap. Stateless: `ctx` is unused, and every call to
 * rb_heap_allocator returns the same value.
 */
#include <reborn/memory.h>

#include <reborn/log.h>

#include "platform/platform_os.h"

/*
 * Size 0 is a valid allocation for the interface but not for the platform
 * (runtimes disagree on malloc(0), and _aligned_realloc(p, 0) frees p): one
 * byte keeps it a real block, freed like any other.
 */
static usize heap_nonzero(usize size) {
    return size == 0 ? 1 : size;
}

static void *heap_alloc(void *ctx, usize size, usize align) {
    RB_UNUSED(ctx);
    void *ptr = rb_platform_heap_alloc(heap_nonzero(size), align);
    if (ptr == NULL) {
        RB_LOG_ERROR("Heap allocation of %zu bytes (alignment %zu) failed", size, align);
    }
    return ptr;
}

static void *heap_resize(void *ctx, void *ptr, usize old_size, usize new_size, usize align) {
    RB_UNUSED(ctx);
    void *moved =
        rb_platform_heap_resize(ptr, heap_nonzero(old_size), heap_nonzero(new_size), align);
    if (moved == NULL) {
        RB_LOG_ERROR("Heap resize from %zu to %zu bytes (alignment %zu) failed", old_size, new_size,
                     align);
    }
    return moved;
}

/* The runtime keeps each block's size itself: the caller's size is not needed. */
static void heap_free(void *ctx, void *ptr, usize size, usize align) {
    RB_UNUSED(ctx);
    RB_UNUSED(size);
    RB_UNUSED(align);
    rb_platform_heap_free(ptr);
}

rb_allocator rb_heap_allocator(void) {
    static const rb_allocator_vtable vtable = {
        .alloc = heap_alloc,
        .resize = heap_resize,
        .free = heap_free,
    };
    return (rb_allocator){.ctx = NULL, .vtable = &vtable};
}
